#include "pwm_floor.h"
#include "tag_recovery.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <math.h>
#include "policy_ids.h"
#include "signal_sequences.h"
extern String phase,mode;
extern JsonDocument config,observation;
extern uint32_t lastUnoAt;
extern uint32_t sendUno(const String &);
extern void resendScanStart(uint32_t,const String &);
extern void clearTx();
extern void logLine(const char *,const String &);
extern void requestVision(uint32_t,uint16_t,uint32_t,const char *);
extern void cancelVision();
extern bool stopReady();
extern void resetLinkState();
extern void hostLedResponse(bool on,bool active);
namespace {
String action="IDLE",afterFace="OBSERVE",afterStop,afterSettle,scanKind,scanNext;
bool returnToPatrol=true,startCat=false;
int balanceRequestedMs=200,balanceConfirmedMs=0;
int robotActions=0;String robotAction;
bool robotFast=false,robotSlowFlash=false,robotFound=false;
bool interactionStopPending=false;
uint32_t interactionStopAt=0,frontUnavailableAt=0;
String approachEndReason;
bool linkRecovering=false;uint8_t linkRecoveryAttempts=0;
String linkResumePhase,linkResumeNext,linkResumeFault;
int pendingFirstTouch=-1,escapeRounds=0,exploreRounds=0,catDistance=-1;
uint32_t interactionId=0,exploreAt=0,leaveFrame=0;
uint32_t touchSignalUntil=0;
uint32_t chaseUnavailableAt=0;
bool responseActive=false,responseStopPending=false,responseCompleted=false;uint32_t responseAt=0;uint8_t responsePulse=0;
using namespace SignalSequence;
const auto &responseOffsets=offsets;
const auto &responseBeeps=beeps;
const auto &responseCounts=counts;
const char *lightNames[4]={"不亮","常亮","慢闪","双闪"};
const char *soundNames[4]={"静音","长响","两声短响","三声短响"};
extern int lastPattern;
// Encoded combinations 1..15 exclude light OFF + sound OFF (zero).
int responseLight(){return (lastPattern+1)/4;}
int responseSound(){return (lastPattern+1)%4;}
String responseName(){return lastPattern<0?String("未选择"):String(lightNames[responseLight()])+"＋"+soundNames[responseSound()];}
bool exploring=false,chaseDetected=false,chaseWasOutside=false,touchPaused=false,pauseScan=false,directionLocated=false;
String pausedAction,interactionExitReason;
float resumeCm=0,resumeDegrees=0;int resumePwm=0;
String initiator,escape="SLOW_STRAIGHT_ESCAPE",touchNext,pendingCommand,lastFault,lastMotionFailure;
String catFailure,catFailureReason;
uint32_t actionAt=0,sessionAt=0,lastTouch=0,due=0,farAt=0,feedbackAt=0;
uint32_t motorId=0,sessionId=0,stopId=0,sessionCommandAt=0,lastRobotProbe=0,lastCatProbe=0;
bool scanConfigRetryPending=false;
uint32_t scanConfigRetryAt=0;
String scanConfigRetryReason;
String scanStartCommand;
uint8_t scanStartRetries=0;
uint8_t scanRecoveryRetries=0;
uint32_t scanProgressAt=0,scanRecoveryAt=0;
uint32_t generation=1,scanId=0,scanAt=0,scanStep=0,scanFrame=0,lastVqUsed=0,checkStep=0,checkAt=0;
uint32_t lastDecisionFrame=0,arrivalFrame=0,catFailureAt=0;
uint32_t encoder[2]={},encoderAt[2]={},quietEncoder[2]={},quietAt=0;
float encoderRpm[2]={};
uint32_t firstTouchUnoAt=0,telemetryUnoAt=0;bool touchTie=false;
uint32_t firstSettleSampleAt=0;
uint32_t catCheckLastFrame=0;int catCheckSamples=0,catCheckLimit=3;
uint32_t scanTagLastFrame=0,scanTagWindowAt=0;int scanTagSamples=0;
uint32_t sideAt[2]={};float side[2]={-1,-1},safe[2]={1,1};
bool touch[4]={},motorConfirmed=false,motorDispatched=false,motorRunning=false;
bool scanMeanProgress=false;
bool geometryConfirmed=false,scanCapable=false,followPending=false,scanReplyFound=false;
bool checkPending=false,checkReady=false,needRearm=false,multiTouch=false,touchSearch=false,touchCue=false,scanWaiting=false;
int rounds=0,routeIndex=0,leftPwm=0,rightPwm=0,branch=-1,patterns=0,lastPattern=-1,probeStep=0,sessionRetries=0,firstTouch=-1;
int scanReplyResult=0;float scanOrigin=0;bool scanDoneSeen=false;int scanFlags=0,scanDirection=1,scanBudget=0,alignSteps=0;float scanUsed=0;
TargetRecoveryMemory tagMemory,catMemory;
uint8_t catRecoveryStage=0;int catRecoveryDirection=1;float catRecoverySpent=0;
uint8_t tagRecoveryStage=0;int tagRecoveryDirection=1;float tagRecoverySpent=0;
uint32_t tagRecheckLastFrame=0,tagRecheckEncoder[2]={};int tagRecheckSamples=0;
float escapeTargetCm=0;
int escapeTurns=0,escapeDirection=1;
float escapeRemainingCm=0;
uint32_t escapeObserveAt=0,escapeObserveFrame=0;
float catFailureAngle=0;float wheel=67.5f,track=120,segmentCm=0,remainingCm=0,driveCm=0,turnDeg=0,pulsesL=0,pulsesR=0;
float setting(const char *k){return config[k].as<float>();}
uint32_t now(){return millis();}
bool elapsed(uint32_t start,uint32_t ms){return uint32_t(now()-start)>=ms;}
bool reached(uint32_t time){return int32_t(now()-time)>=0;}
bool touching(){return touch[0]||touch[1]||touch[2]||touch[3];}
bool frontEnabled(){return config["frontAvoidanceEnabled"]|true;}
int draw(){return esp_random()%100;}
bool fresh(){return observation["at"].is<uint32_t>()&&!elapsed(observation["at"].as<uint32_t>(),setting("visionMaxAgeMs"))&&((mode=="SIM_UNO")==(String(observation["source"]|"")=="SIM_CAMERA"));}
JsonVariant cat(){return observation["cat"];}
bool seen(){return fresh()&&cat()["valid"].as<bool>()&&cat()["detected"].as<bool>();}
float angle(){return (pulsesL+pulsesR)*wheel/20.0f/track*180.0f;}
float scanAngle(){return scanUsed*wheel/20.0f/track*360.0f;}
float travel(){return (pulsesL+pulsesR)*PI*wheel/400.0f;}
bool frontClear(){return !frontEnabled()||fresh()&&String(observation["front"]|"INVALID")=="CLEAR";}
bool approachFrontValid(){return fresh()&&(observation["frameSeq"]|0u)&&config["frontCalibrated"].as<bool>()&&observation["frontCalibrated"].as<bool>();}
bool approachFrontBlocked(){return frontEnabled()&&approachFrontValid()&&String(observation["front"]|"INVALID")=="BLOCKED";}
bool approachFrontClear(){return !frontEnabled()||approachFrontValid()&&String(observation["front"]|"INVALID")=="CLEAR";}
void approachEnded(const char *reason){approachEndReason=reason;logLine("APPROACH_END",approachEndReason+" frame="+String(observation["frameSeq"]|0u));}
bool closeSide(int i){return sideAt[i]&&!elapsed(sideAt[i],2500)&&side[i]>=0&&side[i]<safe[i];}
bool escapeFrontClear(){return fresh()&&(observation["frameSeq"]|0u)&&config["frontCalibrated"].as<bool>()&&observation["frontCalibrated"].as<bool>()&&String(observation["front"]|"INVALID")=="CLEAR";}
bool escapeSideFresh(int i){return sideAt[i]&&int32_t(sideAt[i]-escapeObserveAt)>0&&!elapsed(sideAt[i],2500)&&isfinite(side[i])&&side[i]>=0;}
bool escapeSideOpen(int i){return escapeSideFresh(i)&&side[i]>max(setting("escapeTurnClearanceCm"),safe[i]);}
bool escapeChecking(){return action=="ESCAPE_SIDE_SETTLE"||action=="ESCAPE_SIDE_CHECK"||action=="ESCAPE_FRONT_SETTLE"||action=="ESCAPE_FRONT_CHECK";}
void state(const String &s){action=s;actionAt=now();logLine("DECISION",phase+" "+s);}
void resetMotor(){motorId=0;motorConfirmed=motorDispatched=motorRunning=false;leftPwm=rightPwm=0;pulsesL=pulsesR=0;}
void invalidate(){interactionStopPending=false;generation=generation%2147483647+1;scanId=scanStep=scanFrame=0;scanConfigRetryPending=false;checkPending=checkReady=false;cancelVision();}
void cancelResponse(){responseActive=false;responseStopPending=false;responseCompleted=false;hostLedResponse(false,false);}
void responseTick(){
  if(!responseActive)return;
  uint32_t t=now()-responseAt;int light=responseLight(),sound=responseSound();
  if(t>=max(lightLengths[light],soundLengths[sound])){cancelResponse();exploreRounds++;responseCompleted=true;return;}
  bool on=SignalSequence::lightOn(light,t);
  hostLedResponse(on,true);
  if(!stopReady())return;
  if(responseStopPending){sendUno("BEEP 0");responseStopPending=false;}
  // Skip expired pulses after a delayed loop; never replay a burst of sounds.
  while(responsePulse<responseCounts[sound]&&t>=responseOffsets[sound][responsePulse]){
    uint32_t end=responseOffsets[sound][responsePulse]+responseBeeps[sound][responsePulse];
    if(t<end)sendUno("BEEP "+String(end-t));
    ++responsePulse;
  }
}
void quietOutputs(bool force=false){if(!force&&responseActive)return;cancelResponse();if(force)touchCue=false;if(!force&&touchSignalUntil&&!reached(touchSignalUntil))return;touchSignalUntil=0;sendUno("LED OFF");sendUno("BEEP 0");}
void clearTagRecovery(){catMemory.clear();catRecoveryStage=0;catRecoverySpent=0;tagMemory.clear();tagRecoveryStage=0;tagRecoverySpent=0;tagRecheckLastFrame=0;tagRecheckSamples=0;}
void fail(const String &reason){clearTagRecovery();linkRecovering=false;lastFault=reason;invalidate();clearTx();resetMotor();stopId=sessionId=0;sendUno("STOP");sendUno("SESSION_STOP");quietOutputs(true);phase="STOPPED";state("FAULT");logLine("FAULT",reason);}
void retryLink(){clearTagRecovery();
  if(linkRecoveryAttempts>=4){fail("Uno link recovery failed after 4 attempts");return;}
  ++linkRecoveryAttempts;cancelResponse();invalidate();clearTx();resetLinkState();resetMotor();sessionId=0;geometryConfirmed=false;
  observation.clear();touchSignalUntil=0;touchCue=false;stopId=sendUno("STOP");afterStop="LINK_RECOVERY_STOPPED";
  phase="RECOVERING";state("LINK_RECOVERY_STOP");logLine("LINK_RECOVERY",String(linkRecoveryAttempts)+"/4: confirm stop before resynchronizing");
}
void recoverLink(){
  linkResumePhase=phase;linkResumeFault=lastFault;linkResumeNext=initiator=="CAT_TOUCH"?(branch<0?"CHOOSE_TOUCH":"CHASE_ALIGN"):"CHOOSE_ROBOT";
  linkRecovering=true;linkRecoveryAttempts=0;retryLink();
}
void retryScanConfig(const String &reason){
  if(scanConfigRetryPending)return;
  if(sessionRetries>=3){fail("scan configuration failed after 3 retries: "+reason);return;}
  scanConfigRetryPending=true;scanConfigRetryAt=now();scanConfigRetryReason=reason;
  logLine("SCAN_CONFIG_RETRY_WAIT",String(sessionRetries+1)+"/3 in 100ms: "+reason);
}
void retryScanStart(const String &reason){
  if(!scanId||motorConfirmed||!motorDispatched)return;
  if(scanStartRetries>=3){fail("scan start failed after 3 retries: "+reason);return;}
  ++scanStartRetries;motorDispatched=false;
  resendScanStart(scanId,scanStartCommand);
  logLine("SCAN_START_RETRY",String(scanStartRetries)+"/3 command="+String(scanId)+" reason="+reason);
}
void waitCommand(const String &next,const String &command){scanConfigRetryPending=false;pendingCommand=command;sessionId=sendUno(command);sessionCommandAt=now();sessionRetries=0;state(next);}
void awaitStop(const String &next){invalidate();clearTx();if(responseActive){responsePulse=0;responseStopPending=true;}sessionId=0;resetMotor();afterStop=next;stopId=sendUno("STOP");quietOutputs();state("WAIT_STOP");}
void requestBalance(const String &next){balanceConfirmedMs=0;balanceRequestedMs=config["straightBalanceIntervalMs"]|200;waitCommand(next,"SB "+String(balanceRequestedMs));}
void requestConfig(){geometryConfirmed=false;waitCommand("WAIT_PATROL_CONFIG","GET_CONFIG");}
void beginSession(){phase="STARTING";waitCommand("WAIT_SESSION_START","SESSION_START");}
void finish(){clearTagRecovery();touchPaused=false;invalidate();quietOutputs(true);phase="ENDING";awaitStop("END_SESSION");}
void exitInteraction(const String &reason){interactionExitReason=reason;logLine("INTERACTION_EXIT",reason);finish();}
void newInteraction(){escapeTurns=0;escapeRemainingCm=0;frontUnavailableAt=0;approachEndReason="";clearTagRecovery();robotActions=0;robotAction="";robotFast=robotSlowFlash=robotFound=false;escapeTargetCm=0;interactionId++;escapeRounds=exploreRounds=0;exploring=false;exploreAt=0;catDistance=-1;farAt=leaveFrame=0;touchPaused=false;chaseDetected=false;directionLocated=false;interactionExitReason="";}
bool completeRobotAction(){
  if(initiator!="ROBOT_SEARCH"||!robotAction.length())return false;
  ++robotActions;logLine("ROBOT_ACTION_COMPLETE",robotAction+" "+String(robotActions)+"/"+String((int)setting("robotActionLimit")));robotAction="";
  if(robotActions>=(int)setting("robotActionLimit")){exitInteraction("robot action limit reached");return true;}return false;
}
int distanceClass(){if(!seen())return -1;float w=cat()["width"].as<float>();if(w<=setting("catFarWidth"))catDistance=0;else if(w>=setting("catNearWidth"))catDistance=1;return catDistance;}
bool departed(bool allowAbsent,bool absentOnly=false){
  if(touching()||!fresh()||!cat()["valid"].as<bool>()){farAt=leaveFrame=0;return false;}
  bool away=cat()["detected"].as<bool>()?!absentOnly&&cat()["width"].as<float>()<=setting("catFarWidth"):allowAbsent;
  if(!away){farAt=leaveFrame=0;return false;}uint32_t frame=observation["frameSeq"]|0u;if(!frame||frame==leaveFrame)return false;leaveFrame=frame;
  if(!farAt)farAt=now();return elapsed(farAt,setting("catLeaveSeconds")*1000);
}
void catFailed(const String &reason){catFailure="找猫失败";catFailureReason=reason;catFailureAt=now();catFailureAngle=(scanId||action=="SEARCH_CAT_ACTIVE"||action=="BOTH_SIDE_SCAN")?scanAngle():0;logLine("CAT_SEARCH_FAILED",catFailure+" "+reason+" angle="+String(scanAngle(),1));finish();}
void hold(const String &s,uint32_t ms){state(s);due=now()+ms;}
void settle(const String &next){afterSettle=next;firstSettleSampleAt=0;quietAt=now();quietEncoder[0]=encoder[0];quietEncoder[1]=encoder[1];state("SETTLE");}
void oneFrame(const String &next,const String &checkState="CAT_CHECK"){afterSettle=next;checkStep=checkStep%65535+1;checkAt=now();catCheckLastFrame=0;catCheckSamples=0;catCheckLimit=config["catCheckMaxFrames"]|3;checkPending=true;checkReady=false;requestVision(0,checkStep,generation,"CAT");state(checkState);}
void move(int l,int r){if(motorId&&leftPwm==l&&rightPwm==r)return;if(motorId){awaitStop("APPROACH_TAG");return;}leftPwm=l;rightPwm=r;pulsesL=pulsesR=0;motorId=sendUno("MOTOR "+String(l)+" "+String(r));motorDispatched=motorConfirmed=motorRunning=false;feedbackAt=now();}
void drive(float cm,int pwm,const String &s){driveCm=cm;leftPwm=rightPwm=pwm;pulsesL=pulsesR=0;motorId=sendUno("DRIVE "+String(cm,2)+" "+String(pwm));motorConfirmed=motorDispatched=motorRunning=false;feedbackAt=now();state(s);}
void rotate(float degrees,int pwm,const String &s){turnDeg=degrees;leftPwm=degrees>0?pwm:-pwm;rightPwm=-leftPwm;pulsesL=pulsesR=0;motorId=sendUno("ROTATE "+String(degrees,1)+" "+String(pwm));motorConfirmed=motorDispatched=motorRunning=false;feedbackAt=now();state(s);}
void startEscapeSearch(){
  escapeTurns=0;escapeDirection=turnDeg<0?-1:1;
  escapeRemainingCm=0;escapeTargetCm=0;
  awaitStop("ESCAPE_SIDE_SETTLE");
}
void beginEscapeFrontMonitor(){
  // Continuous FLOOR observations need no scan ticket or generation/step match.
  // Cancel older requested work and require a capture after this settled barrier.
  cancelVision();checkPending=checkReady=false;
  escapeObserveAt=now();escapeObserveFrame=observation["frameSeq"]|0u;
  state("ESCAPE_FRONT_CHECK");
}
void continueEscapeSearch(){
  if(escapeTurns>=3){exitInteraction("escape search exhausted three directions");return;}
  awaitStop("ESCAPE_SIDE_SETTLE");
}
uint32_t scanVisionWindow(){return min(uint32_t(60000),uint32_t(setting("scanVisionTimeoutMs"))*(config[scanKind=="TAG"?"tagScanMaxFrames":"catCheckMaxFrames"]|3));}
void beginScan(const String &kind,const String &next,int flags=0,float spent=0,int localPulses=0){
  invalidate();clearTx();if(responseActive){responsePulse=0;responseStopPending=true;}scanKind=kind;scanNext=next;scanFlags=flags;scanOrigin=spent;scanUsed=spent;scanMeanProgress=false;lastVqUsed=0;scanDoneSeen=false;scanBudget=max(1,(int)lroundf(720.0f*20*track/(360*wheel)));scanAt=now();scanProgressAt=now();scanRecoveryRetries=0;
  if(localPulses>0)scanBudget=min(scanBudget,(int)floorf(spent)+localPulses);
  if(scanBudget-scanUsed<1){if(kind=="CAT")catFailed("remaining scan budget below one pulse");else fail("remaining tag scan budget below one pulse");return;}
  int steps=(int)ceilf(2*(scanBudget-scanUsed)/setting("scanStepPulses"));
  uint32_t total=min(uint32_t(600000),(uint32_t)(steps*(scanVisionWindow()+setting("scanSettleTimeoutMs")+10000)+1000));
  waitCommand("WAIT_SCAN_CONFIG","SC "+String((int)setting("scanMinimumMs"))+" "+String((int)setting("scanQuietMs"))+" "+String((int)scanVisionWindow())+" "+String(total)+" "+String((int)setting("scanSettleTimeoutMs")));
  // clearTx may have removed a just-queued contact cue. Restore only its
  // remaining duration, without extending it or blocking the scan.
  if(touchSignalUntil&&!reached(touchSignalUntil)){
    sendUno("LED FLASH 120 "+String(touchSignalUntil-now()));
    uint32_t beepUntil=touchSignalUntil-(uint32_t)setting("signalFlashMs")+(uint32_t)setting("touchFeedbackBeepMs");
    if(!reached(beepUntil))sendUno("BEEP "+String(beepUntil-now()));
  }
}
void startTagRecheck(){
  checkStep=checkStep%65535+1;checkAt=now();tagRecheckLastFrame=observation["frameSeq"]|0u;tagRecheckSamples=0;
  tagRecheckEncoder[0]=encoder[0];tagRecheckEncoder[1]=encoder[1];
  checkPending=true;checkReady=false;requestVision(0,checkStep,generation,"TAGS");state("TAG_RECHECK");
}
int recoveryLocalPulses(){return max(1,(int)lroundf((config["tagRecoveryAngleDeg"]|60.0f)*20*track/(360*wheel)));}
void startTargetRecovery(const String &kind,const String &next,TargetRecoveryMemory &memory,int target,uint8_t &stage,int &direction,float &spent,int flags=0,float used=0){
  direction=memory.direction(target,now(),scanDirection);stage=1;spent=used;scanDirection=direction;
  logLine(kind=="CAT"?"CAT_RECOVERY":"TAG_RECOVERY","local target="+String(target)+" direction="+String(direction)+" memoryAgeMs="+String(memory.frame?now()-memory.at:UINT32_MAX)+" spent="+String(used,1));
  beginScan(kind,next,flags,used,recoveryLocalPulses());
}
bool advanceTargetRecovery(const String &kind,const String &next,uint8_t &stage,int direction,float &spent,int result){
  if(!stage)return false;spent=scanUsed;
  if(result==0&&stage<3){++stage;scanDirection=-direction;
    logLine(kind=="CAT"?"CAT_RECOVERY":"TAG_RECOVERY",String(stage==2?"reverse local":"full remaining budget")+" direction="+String(scanDirection)+" spent="+String(spent,1));
    beginScan(kind,next,scanFlags,spent,stage==2?recoveryLocalPulses()*2:0);return true;
  }
  if(result==1){stage=0;spent=0;}return false;
}
void startTagRecovery(){startTargetRecovery("TAG","APPROACH_TAG",tagMemory,config["route"][routeIndex].as<int>(),tagRecoveryStage,tagRecoveryDirection,tagRecoverySpent);}
void searchCatTarget(const String &next,int flags=0,float spent=0){
  if(catMemory.target==0&&catMemory.frame&&!elapsed(catMemory.at,5001))startTargetRecovery("CAT",next,catMemory,0,catRecoveryStage,catRecoveryDirection,catRecoverySpent,flags,spent);
  else{catRecoveryStage=0;beginScan("CAT",next,flags,spent);}
}
void stopForTagRecheck(const String &reason){
  logLine("TAG_TRACK_LOST",reason);tagMemory.losing=false;tagMemory.lostAt=0;
  firstSettleSampleAt=0;quietAt=now();quietEncoder[0]=encoder[0];quietEncoder[1]=encoder[1];
  awaitStop("TAG_SETTLE");
}
void search(const String &next){afterFace=next;awaitStop("SEARCH_CAT");}
void confirmCat(const String &next){farAt=leaveFrame=0;afterFace=next;firstSettleSampleAt=0;quietAt=now();quietEncoder[0]=encoder[0];quietEncoder[1]=encoder[1];due=0;awaitStop("CAT_CONFIRM");}
void face(const String &next){confirmCat(next);}
void prey(bool signal){resetMotor();quietOutputs(true);hold("PREY_WAIT",setting("captureWaitMaxSeconds")*1000);farAt=leaveFrame=0;logLine("PREY","stationary; MazeBot LED is manual");}
void playExplorationResponse(){
  if(!exploring){exploring=true;exploreAt=now();lastTouch=now();}
  cancelResponse();touchSignalUntil=0;
  if(!patterns)patterns=32767;
  int picks[15],n=0;for(int i=0;i<15;i++)if((patterns&(1<<i))&&(i!=lastPattern||patterns==(1<<i)))picks[n++]=i;
  int p=picks[esp_random()%n];patterns&=~(1<<p);lastPattern=p;
  responseAt=now();responsePulse=0;responseStopPending=true;responseActive=true;responseTick();
  logLine("EXPLORE_RESPONSE",String(p+1)+"/15 "+responseName()+"; parallel to "+action);
}
void signal(bool exploration=false){
  uint32_t duration=setting("signalFlashMs");
  if(exploration){playExplorationResponse();duration=max(lightLengths[responseLight()],soundLengths[responseSound()]);}
  else{sendUno("LED FLASH 200 "+String(duration));sendUno("BEEP "+String((int)setting("signalBeepMs")));duration=max(duration,(uint32_t)setting("signalBeepMs"));}
  hold("SIGNAL",duration);
}
void chooseEscapeDistance(){float low=setting("escapeMinDistanceCm"),high=setting("escapeMaxDistanceCm");escapeTargetCm=low==high?low:low+(high-low)*(double(esp_random())/4294967295.0);segmentCm=escapeTargetCm;logLine("ESCAPE_DISTANCE",String(escapeTargetCm,2)+" cm");}
void behavior(const String &s){
  if(s=="FAST_APPROACH"||s=="SLOW_APPROACH"||s=="PROBE_APPROACH"){frontUnavailableAt=0;approachEndReason="";}
  if(initiator=="ROBOT_SEARCH"&&(s=="FAST_APPROACH"||s=="SLOW_APPROACH"||s=="PROBE_APPROACH"||s=="STAY_SIGNAL"||s=="OBSERVE"||s=="STAY")){robotAction=s;robotFound=true;}
  if(s=="FAST_APPROACH"){robotFast=true;robotSlowFlash=false;remainingCm=setting("robotFastApproachCm");state("APPROACH_CAT");}
  else if(s=="SLOW_APPROACH"){robotFast=false;robotSlowFlash=initiator=="ROBOT_SEARCH"&&draw()<setting("robotSlowFlashPercent");remainingCm=max(0.0f,setting("slowApproachCm")-setting("stopMarginCm"));state("APPROACH_CAT");}
  else if(s=="RESUME_APPROACH")state("APPROACH_CAT");
  else if(s=="PROBE_APPROACH"){probeStep=1;segmentCm=setting("probeStep1Cm");state("PROBE_DRIVE");}
  else if(s=="STAY_SIGNAL")signal();else if(s=="TOUCH_SIGNAL")signal(true);
  else if(s=="END")finish();else if(s=="PREY_SIGNAL")prey(true);else if(s=="PREY")prey(false);
  else if(s=="PROBE_DONE")hold("PROBE_PAUSE",setting(probeStep==1?"probePause1Ms":probeStep==2?"probePause2Ms":"probePause3Ms"));
  else if(s=="CHASE_ALIGN"){confirmCat("WAIT_CHASE");}
  else if(s=="WAIT_CHASE"){farAt=leaveFrame=chaseUnavailableAt=0;chaseDetected=false;chaseWasOutside=!seen()||cat()["width"].as<float>()<setting("catNearWidth");hold("WAIT_CHASE",setting("catObserveTimeoutSeconds")*1000);}
  else if(s=="ESCAPE_DIRECTION_FOUND"){directionLocated=true;turnDeg=180;state("ESCAPE_DIRECTION");}
  else if(s=="ESCAPE_DRIVE"){state("ESCAPE_DRIVE");}
  else hold("OBSERVE",setting("initialLookSeconds")*1000);
}
void robotBehavior(const String &next,bool confirmed){afterFace=next;if(confirmed)behavior(next);else face(next);}
void chooseStoppedRobot(bool confirmed=false){int n=draw();robotBehavior(n<setting("robotNearSignalPercent")?"STAY_SIGNAL":n<setting("robotNearSignalPercent")+setting("robotNearObservePercent")?"OBSERVE":"STAY",confirmed);}
void interactAtFrontObstacle(){
  approachEnded("FRONT_BLOCKED");frontUnavailableAt=0;
  remainingCm=segmentCm=0;probeStep=0;robotFast=robotSlowFlash=false;touchPaused=false;
  // The STOP reply releases only the motion gate, not the interaction
  // already started. Queue STOP before emitting light and sound commands.
  awaitStop("OBSERVE");
  if(completeRobotAction())return;
  interactionStopPending=true;interactionStopAt=now();
  if(initiator=="ROBOT_SEARCH")chooseStoppedRobot(true);else behavior("OBSERVE");
}
void robotObserveWithoutCat(){robotFast=robotSlowFlash=false;remainingCm=0;quietOutputs();logLine("ROBOT_CAT_MISSING","stay stopped and continue action rounds");behavior("OBSERVE");}
void chooseRobot(bool confirmed=false){if(!seen()){if(robotFound)robotObserveWithoutCat();else search("CHOOSE_ROBOT");return;}robotFound=true;if(approachFrontBlocked()){catDistance=1;chooseStoppedRobot(confirmed);return;}int range=distanceClass();if(range<0){robotBehavior("SLOW_APPROACH",confirmed);return;}if(range==0){bool probe=draw()<setting("robotFarProbePercent")&&elapsed(lastRobotProbe,setting("robotProbeCooldownSeconds")*1000);if(probe)lastRobotProbe=now();robotBehavior(probe?"PROBE_APPROACH":"FAST_APPROACH",confirmed);}else chooseStoppedRobot(confirmed);}
// The stopped cat frame authorizes the next behavior regardless of centerX.
// Reuse this confirmation when choosing a robot invitation; do not stop/check twice.
void chooseConfirmedTouch();
void catLocated(){if(!seen()){searchCatTarget(afterFace);return;}if(afterFace=="CHOOSE_TOUCH")chooseConfirmedTouch();else if(afterFace=="CHOOSE_ROBOT")chooseRobot(true);else behavior(afterFace);}
void pauseForTouch(){
  if(touchPaused||(!motorId&&action!="WAIT_SCAN_CONFIG"))return;
  touchPaused=true;pausedAction=action;pauseScan=scanId||action=="WAIT_SCAN_CONFIG";
  if(pauseScan){if(scanId&&!scanWaiting&&!scanDoneSeen)scanUsed=min(float(scanBudget),scanUsed+pulsesL/2.0f+pulsesR/2.0f);}
  else{resumeCm=max(0.0f,driveCm-travel());resumeDegrees=turnDeg>0?max(0.0f,turnDeg-angle()):min(0.0f,turnDeg+angle());resumePwm=abs(leftPwm);if(action=="CAT_DRIVE")remainingCm=max(0.0f,remainingCm-travel());}
  awaitStop("WAIT_TOUCH_RESUME");
}
void firstTouchPlan(int area,uint32_t stamp){
  firstTouch=area;firstTouchUnoAt=stamp;touchTie=multiTouch=false;rounds=1;followPending=true;
  branch=-1;touchNext="CONFIRM_TOUCH";
  touchCue=draw()<setting("touchFlashPercent");
  turnDeg=area==0?90:area==1?-90:0;scanDirection=area==1?-1:1;
  touchSearch=true;
  awaitStop("WAIT_RELEASE");
}
void chooseConfirmedTouch(){
  int n=draw();touchSearch=false;
  if(n<setting("catConfirmPreyPercent")){branch=2;prey(false);}
  else if(n<setting("catConfirmPreyPercent")+setting("catConfirmExplorePercent")){branch=0;signal(true);}
  else{branch=1;escape=draw()<50?"SLOW_STRAIGHT_ESCAPE":"FAST_STRAIGHT_ESCAPE";turnDeg=180;touchNext="ESCAPE";directionLocated=true;state("ESCAPE_DIRECTION");}
}
void onTouch(int area,uint32_t stamp=0){
  if(touch[area])return;bool wasHeld=touching();touch[area]=true;lastTouch=now();farAt=leaveFrame=0;
  if(phase=="INTERACTION"){
    if(initiator=="ROBOT_SEARCH"){
      // The first attack replaces the robot approach, including any paused
      // motion/scan. Later contacts remain in the selected cat interaction.
      newInteraction();initiator="CAT_TOUCH";sessionAt=now();
      firstTouchPlan(area,stamp);logLine("SEARCH_ATTACK","enter patrol touch interaction");return;
    }
    if(wasHeld&&stamp&&stamp==firstTouchUnoAt&&rounds==1){touchTie=true;multiTouch=true;touchSearch=true;}
    if(initiator=="CAT_TOUCH"&&branch==0&&action!="PREY_WAIT"){
      pauseForTouch();playExplorationResponse();
      if(action=="SIGNAL")due=responseAt+max(lightLengths[responseLight()],soundLengths[responseSound()]);
      logLine("TOUCH_CONTINUED","parallel exploration response; search/confirmation preserved");return;
    }
    if(action=="WAIT_CHASE")chaseDetected=true;
    pauseForTouch();logLine("TOUCH_CONTINUED","same interaction; no redraw");return;
  }
  if(phase=="PATROL"){returnToPatrol=true;phase="INTERACTION";initiator="CAT_TOUCH";sessionAt=now();newInteraction();firstTouchPlan(area,stamp);return;}
  if((phase=="IDLE"||phase=="STOPPED")&&stopReady()){
    pendingFirstTouch=area;firstTouchUnoAt=stamp;startCat=true;returnToPatrol=false;newInteraction();initiator="CAT_TOUCH";
    invalidate();clearTx();resetMotor();geometryConfirmed=false;lastFault=lastMotionFailure="";phase="STARTING";waitCommand("WAIT_PATROL_STOP","STOP");
  }
}
void motionFailure(const String &reason){if(reason.startsWith("TOUCH")&&phase=="INTERACTION"){pauseForTouch();return;}if(reason.startsWith("SIDE_")&&scanId&&scanKind=="CAT"&&!scanFlags&&closeSide(0)&&closeSide(1)){sessionAt+=now()-scanAt;scanUsed=min(float(scanBudget),scanUsed+pulsesL/2.0f+pulsesR/2.0f);awaitStop("BOTH_SIDE_SCAN");return;}lastMotionFailure=reason;resetMotor();invalidate();logLine("MOTION_FAILED",reason);if(reason.startsWith("TOUCH"))return;if(reason.startsWith("SIDE_")&&phase=="INTERACTION")finish();else fail(reason);}
}
void policyCommandSent(uint32_t id){if(matchesPending(id,motorId)){motorDispatched=true;feedbackAt=now();}}
void policyStopUnconfirmed(){clearTagRecovery();cancelResponse();if(linkRecovering){retryLink();return;}if(mode=="UART"&&phase!="IDLE"&&(phase!="STOPPED"||lastFault!="operator_stop"&&!lastFault.startsWith("Uno link recovery failed")&&!lastFault.startsWith("STOP_UNCONFIRMED"))){recoverLink();return;}lastFault="STOP_UNCONFIRMED: motor stop has not been confirmed";invalidate();clearTx();resetMotor();stopId=sessionId=0;phase="STOPPED";state("FAULT");}
void policyResetTagRecovery(){clearTagRecovery();}
uint32_t policyTelemetryInterval(){return escapeChecking()||action=="ESCAPE_TURN"||linkRecovering||action=="SETTLE"||action=="CAT_CONFIRM"||action=="TAG_SETTLE"||action=="TAG_RECHECK"?250:2000;}
bool policyInteractionStopping(){return interactionStopPending&&phase=="INTERACTION";}
bool policyPreparing(){return phase=="STARTING"||phase=="ENDING"||action=="WAIT_SCAN_CONFIG"||(scanId&&!motorConfirmed);}
int policyTargetId(){return phase=="PATROL"&&config["route"].size()?config["route"][routeIndex].as<int>():-1;}
String policyVisionMode(){
  // Front sensing stays active, but side clearance alone authorizes a turn.
  if(escapeChecking()||action=="ESCAPE_DIRECTION"||action=="ESCAPE_TURN"||action=="ESCAPE_DRIVE"||action=="ESCAPE_MOVE"||action=="WAIT_STOP"&&(afterStop=="ESCAPE_SIDE_SETTLE"||afterStop=="ESCAPE_FRONT_SETTLE"))return "FLOOR";
  if(action=="CAT_DRIVE"||action=="PROBE_MOVE")return frontEnabled()?"FLOOR":"OFF";
  if(action=="APPROACH_CAT"||action=="PROBE_DRIVE")return "CAT";
  // During any scan/slow approach/alignment, CAT is only requested while stopped.
  if(action=="CAT_CONFIRM"&&due&&!checkPending&&!checkReady)return "CAT";
  if(action=="TAG_SETTLE"||action=="TAG_RECHECK")return "OFF";
  if(action=="CAT_CONFIRM"||scanId||action=="WAIT_SCAN_CONFIG"||action=="SCAN_TAG"||action=="SEARCH_CAT"||action=="ESCAPE_TURN"||action=="APPROACH_CAT"||action=="CAT_DRIVE"||action=="CAT_CHECK"||action=="SETTLE"||action=="WAIT_STOP"||action=="PROBE_DRIVE"||action=="PROBE_MOVE")return "OFF";
  if(phase=="PATROL")return "TAGS";
  if(phase=="INTERACTION")return "CAT";
  return "USER";
}
void policyFlowSnapshot(JsonDocument &d){d["action"]=action;d["approachEndReason"]=approachEndReason;d["interactionStopPending"]=interactionStopPending;d["motorId"]=motorId;d["motorConfirmed"]=motorConfirmed;d["leftPwm"]=motorId?drivePwmFloor(leftPwm,(int)setting("minimumDrivePwm")):0;d["rightPwm"]=motorId?drivePwmFloor(rightPwm,(int)setting("minimumDrivePwm")):0;}
void policySnapshot(JsonDocument &d){auto p=d["patrolStatus"].to<JsonObject>();p["straightBalanceConfirmedMs"]=balanceConfirmedMs;p["robotActionsCompleted"]=robotActions;p["robotActionLimit"]=(int)setting("robotActionLimit");p["robotAction"]=robotAction;p["robotSlowFlash"]=robotSlowFlash;p["robotFastApproach"]=robotFast;p["interactionInitiator"]=initiator;p["action"]=action;p["approachEndReason"]=approachEndReason;p["interactionStopPending"]=interactionStopPending;p["touchSignalActive"]=touchSignalUntil&&!reached(touchSignalUntil);p["confirmationStage"]=action=="CAT_CONFIRM"?(!due?"SETTLE":checkPending||checkReady?"FRAME":"WAIT"):"NONE";p["confirmationRemainingMs"]=action=="CAT_CONFIRM"&&!reached(due)?due-now():0;p["nextAction"]=afterStop;p["scanPhase"]=scanId?(motorRunning?"TURN":scanWaiting?"VISION":"SETTLE"):"NONE";p["targetId"]=config["route"].size()?config["route"][routeIndex].as<int>():-1;p["visionAgeMs"]=observation["at"].is<uint32_t>()?now()-observation["at"].as<uint32_t>():UINT32_MAX;p["frameSeq"]=observation["frameSeq"]|0u;p["tagStopWidth"]=setting("tagStopWidth");p["routeCount"]=config["route"].size();float width=-1;if(fresh()&&observation["tagsValid"].as<bool>())for(JsonVariant t:observation["tags"].as<JsonArray>())if(t["id"].as<int>()==policyTargetId()){width=t["width"].as<float>();break;}p["tagWidth"]=width;p["targetIndex"]=routeIndex;p["rounds"]=rounds;p["selectedBehavior"]=afterFace;p["interactionBranch"]=branch;p["lightPattern"]=lastPattern;p["responseActive"]=responseActive;p["responseName"]=responseName();p["responseLight"]=lastPattern>=0?responseLight():-1;p["responseSound"]=lastPattern>=0?responseSound():-1;p["probeStep"]=probeStep;p["escape"]=escape;p["firstTouch"]=firstTouch;p["multipleTouches"]=multiTouch;p["simultaneousDetection"]=touchTie;p["touchSearchRequired"]=touchSearch;p["lastFault"]=lastFault;p["lastMotionFailure"]=lastMotionFailure;p["motorId"]=motorId;p["motorConfirmed"]=motorConfirmed;p["motorRunning"]=motorRunning;p["minimumDrivePwm"]=(int)setting("minimumDrivePwm");p["requestedLeftPwm"]=leftPwm;p["requestedRightPwm"]=rightPwm;p["leftPwm"]=drivePwmFloor(leftPwm,(int)setting("minimumDrivePwm"));p["rightPwm"]=drivePwmFloor(rightPwm,(int)setting("minimumDrivePwm"));p["scanId"]=scanId;p["scanStep"]=scanStep;p["generation"]=generation;p["scanAngle"]=scanAngle();p["scanAngleBasis"]=scanStep&&!scanMeanProgress?"LEGACY_MAX_WHEEL":"MEAN_WHEEL_PULSES";p["scanMeanProgressConfirmed"]=scanMeanProgress;p["tagScanMaxFrames"]=config["tagScanMaxFrames"]|3;p["tagScanSamples"]=scanId&&scanKind=="TAG"?scanTagSamples:0;p["scanBudgetDegrees"]=scanBudget*wheel/20/track*360;p["remainingApproachCm"]=remainingCm;p["scanCapability"]=scanCapable;p["catFailure"]["message"]=catFailure;p["catFailure"]["reason"]=catFailureReason;p["catFailure"]["at"]=catFailureAt;p["catFailure"]["angle"]=catFailureAngle;p["unsupportedSettings"][0]="farCatThresholdCm";p["unsupportedSettings"][1]="catLeaveDistanceCm";p["unsupportedSettings"][2]="finalSearchTimeoutMs";p["unsupportedSettings"][3]="searchTimeoutMs";p["unsupportedSettings"][4]="postEscapeWaitSeconds";p["unsupportedSettings"][5]="centerTolerancePercent";p["unsupportedSettings"][6]="alignPwm";p["unsupportedSettings"][7]="catApproachStepCm";p["unsupportedSettings"][8]="catConfirmWaitSeconds";p["escapeTurns"]=escapeTurns;p["escapeSearchLimit"]=3;p["escapeTurnClearanceCm"]=setting("escapeTurnClearanceCm");p["escapeRemainingCm"]=escapeRemainingCm;p["escapeStage"]=(escapeChecking()||action=="ESCAPE_DIRECTION"||action=="ESCAPE_TURN"||action=="WAIT_RELEASE"||scanId&&scanNext=="ESCAPE_DIRECTION_FOUND")?"DIRECTION":(action=="ESCAPE_DRIVE"||action=="ESCAPE_MOVE")?"RUN":"NONE";p["escapeTargetCm"]=escapeTargetCm;p["escapeDistanceCm"]=driveCm;p["escapeTravelCm"]=travel();p["interactionId"]=interactionId;p["escapeRounds"]=escapeRounds;p["exploreRounds"]=exploreRounds;p["catDistance"]=distanceClass();p["interactionExitReason"]=interactionExitReason;
  if(action!="ESCAPE_MOVE"){p["escapeTravelCm"]=0;p["escapeDistanceCm"]=action=="ESCAPE_DRIVE"?segmentCm:0;}
  p["linkRecovering"]=linkRecovering;p["linkRecoveryAttempt"]=linkRecoveryAttempts;p["encoderLeftRpm"]=encoderRpm[0];p["encoderRightRpm"]=encoderRpm[1];
  p["catRecoveryStage"]=catRecoveryStage==1?"LOCAL_FIRST":catRecoveryStage==2?"LOCAL_REVERSE":catRecoveryStage==3?"FULL":"NONE";p["catRecoveryDirection"]=catRecoveryStage?scanDirection:0;p["catMemoryDirection"]=catMemory.direction(0,now(),0);p["catMemoryAgeMs"]=catMemory.frame?now()-catMemory.at:UINT32_MAX;p["catScanSamples"]=scanId&&scanKind=="CAT"?scanTagSamples:0;p["catRecoveryRemainingDegrees"]=max(0.0f,(lroundf(720.0f*20*track/(360*wheel))-scanUsed)*wheel/20/track*360);
  p["tagRecoveryStage"]=tagRecoveryStage==1?"LOCAL_FIRST":tagRecoveryStage==2?"LOCAL_REVERSE":tagRecoveryStage==3?"FULL":action=="TAG_RECHECK"?"RECHECK":action=="TAG_SETTLE"?"SETTLE":tagMemory.losing?"GRACE":"NONE";
  p["tagRecoveryDirection"]=tagRecoveryStage?scanDirection:0;p["tagMemoryDirection"]=tagMemory.direction(policyTargetId(),now(),0);p["tagMemoryAgeMs"]=tagMemory.frame?now()-tagMemory.at:UINT32_MAX;p["tagLossRemainingMs"]=tagMemory.losing?max(0,int(config["tagLossGraceMs"]|1000)-int(now()-tagMemory.lostAt)):0;
  p["tagRecoveryRemainingDegrees"]=max(0.0f,(lroundf(720.0f*20*track/(360*wheel))-(tagRecoveryStage?scanUsed:tagRecoverySpent))*wheel/20/track*360);p["tagRecheckSamples"]=tagRecheckSamples;
  p["catWidth"]=seen()?cat()["width"].as<float>():-1;p["settleElapsedMs"]=(action=="SETTLE"||action=="CAT_CONFIRM"&&!due)?now()-actionAt:0;p["settleQuietMs"]=(action=="SETTLE"||action=="CAT_CONFIRM"&&!due)?now()-quietAt:0;
  p["catCheckMaxFrames"]=config["catCheckMaxFrames"]|3;p["catCheckSamples"]=(action=="CAT_CHECK"||action=="CAT_CONFIRM")?catCheckSamples:0;p["settleSamples"]=(action=="SETTLE"||action=="CAT_CONFIRM"&&!due)?(firstSettleSampleAt?(int32_t(encoderAt[0]-firstSettleSampleAt)>0&&int32_t(encoderAt[1]-firstSettleSampleAt)>0?2:1):0):0;
  uint32_t explorationMs=exploring?now()-exploreAt:0,minimumMs=setting("explorePreyMinimumSeconds")*1000;p["exploreElapsedMs"]=explorationMs;p["exploreRemainingMs"]=explorationMs>=minimumMs?0:minimumMs-explorationMs;p["remainingWaitMs"]=(action=="CAT_CONFIRM"||action=="PREY_WAIT"||action=="WAIT_CHASE"||action=="SIGNAL"||action=="OBSERVE"||action=="FOLLOW_WAIT"||action=="PROBE_PAUSE")&&!reached(due)?due-now():0;p["departureElapsedMs"]=farAt?now()-farAt:0;}
void policyResumeFrom(int index){
  if(!stopReady()){logLine("INPUT_ERROR","wait for confirmed STOP before starting");return;}
  if(index<0||index>=int(config["route"].size())){logLine("INPUT_ERROR","invalid patrol route index");return;}
  newInteraction();pendingFirstTouch=-1;startCat=false;returnToPatrol=true;invalidate();clearTagRecovery();clearTx();resetMotor();routeIndex=index;rounds=0;branch=-1;initiator="";followPending=needRearm=false;lastDecisionFrame=arrivalFrame=0;lastFault=lastMotionFailure="";geometryConfirmed=false;phase="STARTING";waitCommand("WAIT_PATROL_STOP","STOP");
}
void policyControl(const String &control){
  if(control=="stop")linkRecovering=false;
  if(control=="stop"){touchPaused=false;pendingFirstTouch=-1;startCat=false;fail("operator_stop");return;}
  if(control=="start"||control=="resume"){policyResumeFrom(0);return;}
  if(control=="alert"){
    if(phase=="PATROL"){newInteraction();pendingFirstTouch=-1;returnToPatrol=true;phase="INTERACTION";initiator="ROBOT_SEARCH";sessionAt=now();rounds=0;branch=-1;search("CHOOSE_ROBOT");}
    else if((phase=="IDLE"||phase=="STOPPED")&&stopReady()){newInteraction();pendingFirstTouch=-1;startCat=true;returnToPatrol=true;routeIndex=0;needRearm=false;lastDecisionFrame=arrivalFrame=0;invalidate();clearTx();resetMotor();initiator="ROBOT_SEARCH";rounds=0;branch=-1;geometryConfirmed=false;lastFault=lastMotionFailure="";phase="STARTING";waitCommand("WAIT_PATROL_STOP","STOP");}
  }
}
void policyVision(){
  uint32_t frame=observation["frameSeq"]|0u;
  if(!frame||!fresh())return;
  uint32_t gen=observation["generation"]|0u,sid=observation["scanId"]|0u,step=observation["stepId"]|0u;
  if(gen!=generation)return;
  if(scanId&&scanWaiting&&scanStep&&sid==scanId&&step==scanStep){
    if(scanFrame==frame)return;bool valid=scanKind=="CAT"?cat()["valid"].as<bool>():observation["tagsValid"].as<bool>();bool found=false;
    if(valid){if(scanKind=="CAT")found=cat()["detected"].as<bool>();else{int target=config["route"][routeIndex];for(JsonVariant t:observation["tags"].as<JsonArray>())if(t["id"].as<int>()==target)found=true;}}
    if(scanKind=="TAG"||scanKind=="CAT"){
      if(scanFrame)return; // decision already sent; only VQ may resend the cached reply
      if(scanTagLastFrame&&int32_t(frame-scanTagLastFrame)<=0)return;
      if(elapsed(scanTagWindowAt,scanVisionWindow()))return;
      scanTagLastFrame=frame;++scanTagSamples;
      logLine(scanKind=="CAT"?"CAT_SCAN_SAMPLE":"TAG_SCAN_SAMPLE",String(scanTagSamples)+"/"+String(config[scanKind=="TAG"?"tagScanMaxFrames":"catCheckMaxFrames"]|3)+" frame="+String(frame)+(found?" found":valid?" absent":" invalid"));
      if(valid&&!found&&scanTagSamples<(config[scanKind=="TAG"?"tagScanMaxFrames":"catCheckMaxFrames"]|3)){requestVision(scanId,scanStep,generation,scanKind=="CAT"?"CAT":"TAGS");return;}
    }
    if(scanKind=="CAT"&&found)catMemory.observe(0,frame,observation["at"].as<uint32_t>(),cat()["centerX"].as<float>(),cat()["width"].as<float>());
    scanFrame=frame;scanReplyFound=found;scanReplyResult=valid?(found?1:0):2;sendUno("VR "+String(scanId)+" "+String(scanStep)+" "+String(generation)+" "+String(valid?(found?1:0):2)+" "+String(frame));
  }else if(checkPending&&!sid&&step==checkStep){
    if(action=="TAG_RECHECK"){
      if(elapsed(checkAt,uint32_t(setting("scanVisionTimeoutMs"))*(config["tagScanMaxFrames"]|3))||tagRecheckLastFrame&&int32_t(frame-tagRecheckLastFrame)<=0)return;
      tagRecheckLastFrame=frame;++tagRecheckSamples;bool valid=observation["tagsValid"].as<bool>(),found=false;
      if(valid)for(JsonVariant t:observation["tags"].as<JsonArray>())if(t["id"].as<int>()==config["route"][routeIndex].as<int>())found=true;
      logLine("TAG_RECHECK_SAMPLE",String(tagRecheckSamples)+" frame="+String(frame)+(found?" found":valid?" absent":" invalid"));
      if(valid&&!found&&tagRecheckSamples<(config["tagScanMaxFrames"]|3)){requestVision(0,checkStep,generation,"TAGS");return;}
    }
    if(action=="CAT_CHECK"||action=="CAT_CONFIRM"){
      if(elapsed(checkAt,setting("scanVisionTimeoutMs"))||catCheckLastFrame&&int32_t(frame-catCheckLastFrame)<=0)return;
      catCheckLastFrame=frame;++catCheckSamples;
      bool valid=cat()["valid"].as<bool>(),found=valid&&cat()["detected"].as<bool>();
      if(found)catMemory.observe(0,frame,observation["at"].as<uint32_t>(),cat()["centerX"].as<float>(),cat()["width"].as<float>());
      logLine("CAT_CHECK_SAMPLE",String(catCheckSamples)+"/"+String(catCheckLimit)+" frame="+String(frame)+(found?" found":valid?" absent":" invalid"));
      // Reuse the same stopped check. VisionGate requires a fresh capture for
      // every new ticket; repeated callbacks cannot count the same frame twice.
      if(valid&&!found&&catCheckSamples<catCheckLimit){requestVision(0,checkStep,generation,"CAT");return;}
    }
    checkPending=false;checkReady=true;
  }
}
void policyInput(const String &line){
  unsigned id=0;unsigned long l=0,r=0,gen=0,step=0;int result=0;unsigned used=0,halfUsed=0;char extra=0;
  if(line.startsWith("READY")){if(phase!="IDLE"&&phase!="STOPPED")fail("Uno reboot");geometryConfirmed=scanCapable=false;balanceConfirmedMs=0;invalidate();memset(touch,0,sizeof(touch));return;}
  if(line=="EVENT CAPABILITIES SCAN_V1"){scanCapable=true;return;}
  if(linkRecovering&&sessionId){
    String expected="DONE "+String(sessionId);
    if(action=="LINK_RECOVERY_SESSION_STOP"&&line==expected+" SESSION_STOPPED"){waitCommand("LINK_RECOVERY_CONFIG","GET_CONFIG");return;}
    if(action=="LINK_RECOVERY_CONFIG"&&(line==expected||line==expected+" CONFIG")){
      if(!geometryConfirmed){retryLink();return;}
      if(linkResumePhase=="STOPPED"){linkRecovering=false;sessionId=0;phase="STOPPED";lastFault=linkResumeFault;state("FAULT");logLine("LINK_RECOVERED","checked stop/config; original fault remains stopped");return;}
      requestBalance("LINK_RECOVERY_BALANCE");return;
    }
    if(action=="LINK_RECOVERY_BALANCE"&&line==expected){
      balanceConfirmedMs=balanceRequestedMs;waitCommand("LINK_RECOVERY_SESSION_START","SESSION_START");return;
    }
    if(action=="LINK_RECOVERY_SESSION_START"&&line==expected+" SESSION_STARTED"){
      linkRecovering=false;lastFault="";phase=linkResumePhase;logLine("LINK_RECOVERED","checked stop/config/session; restart with fresh observations");
      if(phase=="STARTING"){state("WAIT_SESSION_START");policyInput(line);return;}sessionId=0;
      if(phase=="STOPPED"){lastFault=linkResumeFault;state("FAULT");return;}
      if(phase=="INTERACTION"){alignSteps=0;search(linkResumeNext);}else if(phase=="ENDING")finish();else{phase="PATROL";state("SCAN_TAG");}return;
    }
  }
  bool extendedVq=sscanf(line.c_str(),"VQ %u %lu %lu %u %u %c",&id,&step,&gen,&used,&halfUsed,&extra)==5;
  bool legacyVq=!extendedVq&&sscanf(line.c_str(),"VQ %u %lu %lu %u %c",&id,&step,&gen,&used,&extra)==4;
  if((extendedVq||legacyVq)&&matchesPending(id,scanId)&&gen==generation&&step>0&&step<=uint32_t(2*scanBudget)){
    if(!extendedVq)halfUsed=used*2;
    if(halfUsed/2!=used||halfUsed>uint32_t(2*(scanBudget-scanOrigin)))return;
    if(step<scanStep||step==scanStep&&halfUsed!=lastVqUsed||step>scanStep&&halfUsed<lastVqUsed)return;
    if(!extendedVq&&!scanStep)logLine("SCAN_COMPAT","legacy VQ; paired Uno 6.11 needed for mean-wheel progress");
    scanMeanProgress=extendedVq;scanDoneSeen=true;lastVqUsed=halfUsed;scanUsed=scanOrigin+halfUsed/2.0f;pulsesL=pulsesR=0;
    if(step==scanStep&&scanFrame){sendUno("VR "+String(scanId)+" "+String(scanStep)+" "+String(generation)+" "+String(scanReplyResult)+" "+String(scanFrame));return;}
    if(step>scanStep){scanStep=step;scanFrame=0;scanTagSamples=0;scanTagLastFrame=0;scanTagWindowAt=now();scanProgressAt=now();scanRecoveryRetries=0;}
    scanWaiting=true;motorConfirmed=true;motorRunning=false;leftPwm=rightPwm=0;requestVision(scanId,scanStep,generation,scanKind=="CAT"?"CAT":"TAGS");return;
  }
  if(sscanf(line.c_str(),"SD %u %lu %d %c",&id,&gen,&result,&extra)==3&&matchesPending(id,scanId)&&gen==generation&&result>=0&&result<=5){
    sessionAt+=now()-scanAt;String kind=scanKind,next=scanNext;scanId=0;resetMotor();cancelVision();
    if(kind=="TAG"&&advanceTargetRecovery(kind,"APPROACH_TAG",tagRecoveryStage,tagRecoveryDirection,tagRecoverySpent,result))return;
    if(kind=="CAT"&&advanceTargetRecovery(kind,next,catRecoveryStage,catRecoveryDirection,catRecoverySpent,result))return;
    if(result==1){if(kind=="TAG"){alignSteps=0;state("APPROACH_TAG");lastDecisionFrame=0;}else confirmCat(next=="CHASE_ALIGN"?"WAIT_CHASE":next);}
    else if(kind=="CAT")catFailed("scan result="+String(result));else fail("tag search failed result="+String(result));return;
  }
  if(sscanf(line.c_str(),"MS %u %lu %lu %c",&id,&l,&r,&extra)==3&&matchesPending(id,motorId)){scanWaiting=false;scanDoneSeen=false;pulsesL=pulsesR=0;motorConfirmed=motorRunning=true;if(scanId){scanProgressAt=now();scanRecoveryRetries=0;int p=(int)setting(scanKind=="TAG"?"patrolSearchPwm":"searchPwm");leftPwm=scanDirection*p;rightPwm=-leftPwm;}feedbackAt=now();return;}
  if(sscanf(line.c_str(),"MP %u %lu %lu %c",&id,&l,&r,&extra)==3&&matchesPending(id,motorId)){if(scanId&&scanWaiting)return;pulsesL=l;pulsesR=r;motorConfirmed=motorRunning=true;feedbackAt=now();return;}
  if(sscanf(line.c_str(),"MD %u %lu %lu %c",&id,&l,&r,&extra)==3&&matchesPending(id,motorId)){
    pulsesL=l;pulsesR=r;motorRunning=false;leftPwm=rightPwm=0;feedbackAt=now();
    if(scanId){if(!scanDoneSeen){scanUsed=min(float(scanBudget),scanUsed+l/2.0f+r/2.0f);scanProgressAt=now();scanRecoveryRetries=0;}scanDoneSeen=true;return;}
    String previous=action;if(previous=="ESCAPE_MOVE")escapeRemainingCm=max(0.0f,driveCm-travel());if(previous=="CAT_DRIVE")remainingCm=max(0.0f,remainingCm-travel());resetMotor();
    awaitStop(previous=="CAT_DRIVE"?"CHECK_APPROACH":previous=="PROBE_MOVE"?"CHECK_PROBE":previous=="ESCAPE_MOVE"?"POST_ESCAPE":previous=="ESCAPE_TURN"?"ESCAPE_FRONT_SETTLE":"APPROACH_TAG");return;
  }
  if(line.startsWith("AK ")||line.startsWith("ACK ")){if(sscanf(line.c_str(),line.startsWith("AK ")?"AK %u %c":"ACK %u %c",&id,&extra)==1&&matchesPending(id,motorId))motorConfirmed=true;return;}
  if(line.startsWith("MA ")){char reason[100];if(sscanf(line.c_str(),"MA %u %99s %lu %lu %c",&id,reason,&l,&r,&extra)==4&&matchesPending(id,motorId)){pulsesL=l;pulsesR=r;motionFailure(reason);}return;}
  if(line.startsWith("ERROR ")){char reason[100];if(sscanf(line.c_str(),"ERROR %u %99s %c",&id,reason,&extra)==2){
    if(scanId&&!motorConfirmed&&(!id||matchesPending(id,scanId))&&(!strcmp(reason,"BAD_CHECKSUM")||!strcmp(reason,"BAD_ENVELOPE"))){retryScanStart(reason);return;}
    if(action=="WAIT_SCAN_CONFIG"&&sessionId&&(!id||matchesPending(id,sessionId))&&(!strcmp(reason,"BAD_CHECKSUM")||!strcmp(reason,"BAD_ENVELOPE"))){retryScanConfig(reason);return;}
    if(!id)logLine("UNO_PROTOCOL_ERROR",reason);else if(matchesPending(id,motorId))motionFailure(reason);else if(matchesPending(id,sessionId)||matchesPending(id,stopId))fail(String("required command: ")+reason);}return;}
  if((action=="WAIT_PATROL_CONFIG"||action=="LINK_RECOVERY_CONFIG")&&line.indexOf(" CONFIG ")>=0){
    unsigned configId=0,slots=0;float w=0,t=0,sl=0,sr=0;char tail=0,capability[11]={};
    int fields=sscanf(line.c_str(),"DATA %u CONFIG WHEEL_MM=%f TRACK_MM=%f SLOTS_PER_REV=%u SAFE_LEFT_CM=%f SAFE_RIGHT_CM=%f %10s %c",&configId,&w,&t,&slots,&sl,&sr,capability,&tail);
    if((fields==6||fields==7&&!strcmp(capability,"REMOTE_V1"))&&matchesPending(configId,sessionId)&&isfinite(w)&&isfinite(t)&&isfinite(sl)&&isfinite(sr)&&w>=20&&w<=150&&t>=50&&t<=300&&slots==20&&sl>=1&&sl<=100&&sr>=1&&sr<=100){wheel=w;track=t;safe[0]=sl;safe[1]=sr;geometryConfirmed=true;}
  }
  if(line.startsWith("EVENT")&&line.indexOf(" TOUCH ")>=0){String rest=line.substring(line.indexOf(" TOUCH ")+7);int a=rest.startsWith("LEFT ")?0:rest.startsWith("RIGHT ")?1:rest.startsWith("REAR ")?2:rest.startsWith("TOP ")?3:-1;if(a>=0){if(rest.indexOf(" PRESS")>=0)onTouch(a,rest.substring(rest.indexOf(" PRESS")+7).toInt());else if(rest.indexOf(" RELEASE")>=0)touch[a]=false;}return;}
  if(line.startsWith("DATA ")&&line.indexOf(" HW ")>=0){unsigned long uptime=0;if(sscanf(line.c_str(),"DATA %u HW STATE %lu",&id,&uptime)==2)telemetryUnoAt=uptime;char kind[4],area[8],src[12];int en,raw,pressed;unsigned long count;float rpm;
    if(sscanf(line.c_str(),"DATA %u HW %3s %d %d %lu %f %11s %c",&id,kind,&en,&raw,&count,&rpm,src,&extra)==7&&isfinite(rpm)&&rpm>=0&&(!strcmp(kind,"EL")||!strcmp(kind,"ER"))){int s=!strcmp(kind,"ER");encoder[s]=count;encoderRpm[s]=rpm;encoderAt[s]=now();if(action=="TAG_RECHECK"&&(count!=tagRecheckEncoder[s]||rpm>0)){fail("tag recheck wheels moved");return;}if(count!=quietEncoder[s]||rpm>0){quietAt=now();quietEncoder[s]=count;}}
    float distance,physical;int echo;if(sscanf(line.c_str(),"DATA %u HW %3s %d %d %d %f %f %11s %c",&id,kind,&en,&raw,&echo,&physical,&distance,src,&extra)==8&&isfinite(physical)&&isfinite(distance)&&distance>=-1&&distance<=400&&(!strcmp(kind,"SL")||!strcmp(kind,"SR"))){int s=!strcmp(kind,"SR");side[s]=distance;sideAt[s]=now();}
    if(sscanf(line.c_str(),"DATA %u HW T %7s %d %d %d %11s %c",&id,area,&en,&raw,&pressed,src,&extra)==6&&(pressed==0||pressed==1)){int a=!strcmp(area,"LEFT")?0:!strcmp(area,"RIGHT")?1:!strcmp(area,"REAR")?2:!strcmp(area,"TOP")?3:-1;if(a>=0){if(pressed&&!touch[a])onTouch(a,telemetryUnoAt);else if(!pressed)touch[a]=false;}}
  }
  if(line.startsWith("DONE ")){sscanf(line.c_str(),"DONE %u",&id);
    if(matchesPending(id,stopId)&&line=="DONE "+String(stopId)+" MOTOR_STOPPED"){stopId=0;if(interactionStopPending){interactionStopPending=false;logLine("APPROACH_STOP_CONFIRMED","interaction continues");return;}if(touchCue){touchCue=false;touchSignalUntil=now()+(uint32_t)setting("signalFlashMs");sendUno("LED FLASH 120 "+String((int)setting("signalFlashMs")));sendUno("BEEP "+String((int)setting("touchFeedbackBeepMs")));logLine("CONTACT_SIGNAL","parallel with search");}if(afterStop=="ESCAPE_SIDE_SETTLE"||afterStop=="ESCAPE_FRONT_SETTLE"){firstSettleSampleAt=0;quietAt=now();quietEncoder[0]=encoder[0];quietEncoder[1]=encoder[1];}state(afterStop);return;}
    if(!matchesPending(id,sessionId))return;if(action=="WAIT_PATROL_STOP"&&line!="DONE "+String(sessionId)+" MOTOR_STOPPED")return;
    if(action=="WAIT_PATROL_BALANCE"&&line!="DONE "+String(sessionId))return;
    if(action=="WAIT_SCAN_CONFIG"&&line!="DONE "+String(sessionId)+" SCAN_CONFIGURED")return;
    if(action=="WAIT_SESSION_START"&&line!="DONE "+String(sessionId)+" SESSION_STARTED")return;
    if(action=="WAIT_SESSION_STOP"&&line!="DONE "+String(sessionId)+" SESSION_STOPPED")return;
    if(action=="WAIT_PATROL_GEOMETRY"&&line!="DONE "+String(sessionId)+" GEOMETRY_SET")return;
    if(action=="WAIT_PATROL_CONFIG"&&line!="DONE "+String(sessionId)&&line!="DONE "+String(sessionId)+" CONFIG")return;
    scanConfigRetryPending=false;sessionId=0;
    if(action=="WAIT_PATROL_STOP"&&line.endsWith(" MOTOR_STOPPED")){if(mode=="SIM_UNO")waitCommand("WAIT_PATROL_GEOMETRY","SET_GEOMETRY "+String(setting("wheelMm"))+" "+String(setting("trackMm")));else requestConfig();}
    else if(action=="WAIT_PATROL_GEOMETRY")requestConfig();
    else if(action=="WAIT_PATROL_CONFIG"){if(!geometryConfirmed)fail("geometry not confirmed");else requestBalance("WAIT_PATROL_BALANCE");}
    else if(action=="WAIT_PATROL_BALANCE"){balanceConfirmedMs=balanceRequestedMs;beginSession();}
    else if(action=="WAIT_SESSION_STOP"){rounds=0;branch=-1;followPending=false;if(returnToPatrol){if(!config["route"].size())fail("empty route: save a tag route before returning to patrol");else beginSession();}else{phase="IDLE";state("IDLE");}}
    else if(action=="WAIT_SESSION_START"){if(!geometryConfirmed)fail("geometry not confirmed");else if(startCat){startCat=false;phase="INTERACTION";sessionAt=now();if(pendingFirstTouch>=0){int area=pendingFirstTouch;pendingFirstTouch=-1;firstTouchPlan(area,firstTouchUnoAt);}else search("CHOOSE_ROBOT");}else{scanDirection=1;alignSteps=0;lastDecisionFrame=0;phase="PATROL";state(needRearm?"TAG_REARM":"SCAN_TAG");}}
    else if(action=="WAIT_SCAN_CONFIG"){
      int pwm=scanKind=="TAG"?(int)setting("patrolSearchPwm"):(int)setting("searchPwm");
      scanStartCommand="SR "+String(generation)+" "+String(scanDirection*(int)setting("scanStepPulses"))+" "+String(drivePwmFloor(pwm,(int)setting("minimumDrivePwm")))+" "+String((int)floorf(scanBudget-scanUsed))+" "+String(scanFlags);
      scanStartRetries=scanRecoveryRetries=0;scanProgressAt=now();scanWaiting=false;scanId=motorId=sendUno(scanStartCommand);motorConfirmed=motorDispatched=motorRunning=false;feedbackAt=now();state(scanKind=="TAG"?"SCAN_TAG_ACTIVE":"SEARCH_CAT_ACTIVE");
    }
  }
}
void policyTick(){
  if(phase=="IDLE"||phase=="STOPPED"||phase=="REMOTE")return;
  if(linkRecovering){
    if(action=="LINK_RECOVERY_STOPPED"){waitCommand("LINK_RECOVERY_SESSION_STOP","SESSION_STOP");return;}
    if(elapsed(actionAt,2500))retryLink();return;
  }
  if(mode=="UART"&&(!lastUnoAt||elapsed(lastUnoAt,2500))){recoverLink();return;}
  responseTick();
  if(responseCompleted&&branch==0&&initiator=="CAT_TOUCH"&&!motorId&&!scanId&&!sessionId&&!stopId&&stopReady()&&(action=="SIGNAL"||action=="OBSERVE"||action=="FOLLOW_WAIT"||action=="WAIT_CHASE")){
    responseCompleted=false;
    if(elapsed(exploreAt,setting("explorePreyMinimumSeconds")*1000)&&draw()<setting("explorePreyPercent")){prey(false);return;}
  }
  if(action=="WAIT_STOP"){if(elapsed(actionAt,2500))fail("STOP confirmation timeout");return;}
  if(interactionStopPending){if(elapsed(interactionStopAt,2500))fail("STOP confirmation timeout during interaction");return;}
  if(sessionId){
    if(action=="WAIT_SCAN_CONFIG"){
      if(scanConfigRetryPending){if(elapsed(scanConfigRetryAt,100)){scanConfigRetryPending=false;++sessionRetries;sessionId=sendUno(pendingCommand);sessionCommandAt=now();logLine("SCAN_CONFIG_RETRY",String(sessionRetries)+"/3 command="+String(sessionId)+" reason="+scanConfigRetryReason);}return;}
      if(elapsed(sessionCommandAt,2500))retryScanConfig("confirmation timeout");return;
    }
    if(elapsed(sessionCommandAt,2500)){if(scanId){fail("scan configuration timeout");return;}if(sessionRetries++<2){sessionId=sendUno(pendingCommand);sessionCommandAt=now();if(action=="WAIT_PATROL_CONFIG")geometryConfirmed=false;}else fail("confirmation timeout: "+action);}return;
  }
  if(motorId&&motorDispatched&&motorRunning&&elapsed(feedbackAt,750)){fail("no checked encoder feedback for 750ms");return;}
  if(scanId&&!motorConfirmed){if(motorDispatched&&elapsed(feedbackAt,100))retryScanStart("no start confirmation for 100ms");return;}
  if(motorId&&motorDispatched&&!motorConfirmed&&elapsed(feedbackAt,2500)){fail("motion start not confirmed");return;}
  if(scanId){
    if(scanWaiting&&!scanFrame&&elapsed(scanTagWindowAt,scanVisionWindow())){if(scanKind=="CAT")catFailed("cat stopped frame window timeout");else fail("tag stopped frame window timeout");return;}
    if(elapsed(scanAt,600000)){fail("scan transaction timeout");return;}
    // Diagnostic traffic/ACK/duplicate VQ cannot renew step progress.
    uint32_t waitMs=(uint32_t)(setting("scanSettleTimeoutMs")+scanVisionWindow())+1500;
    if(!motorRunning&&elapsed(scanProgressAt,waitMs)&&(!scanRecoveryRetries||elapsed(scanRecoveryAt,1000))){
      if(scanRecoveryRetries>=3){fail("scan handoff timeout: no step progress after 3 retries");return;}
      ++scanRecoveryRetries;scanRecoveryAt=now();
      // Same id/generation/frozen command only asks Uno to repeat VQ/SD.
      resendScanStart(scanId,scanStartCommand);
      logLine("SCAN_HANDOFF_RETRY",String(scanRecoveryRetries)+"/3 scan="+String(scanId)+" step="+String(scanStep));
    }
    return;
  }
  if(action=="WAIT_TOUCH_RESUME"){
    if(touching())return;touchPaused=false;
    if(pauseScan){if(scanUsed>=scanBudget)catFailed("two turns exhausted after contact");else beginScan(scanKind,scanNext,scanFlags,scanUsed);return;}
    if(pausedAction=="ESCAPE_TURN"){exitInteraction("escape turn interrupted by contact");return;}
    if(pausedAction=="ESCAPE_MOVE"){
      escapeRemainingCm=resumeCm;if(resumeCm<=0){awaitStop("POST_ESCAPE");return;}awaitStop("ESCAPE_FRONT_SETTLE");return;
    }
    if(pausedAction=="CAT_DRIVE"){settle("APPROACH_CAT");return;}
    if(!frontClear()){if(elapsed(actionAt,2000))exitInteraction("front unavailable after contact");return;}
    if(resumeCm<=0){awaitStop(pausedAction=="ESCAPE_MOVE"?"POST_ESCAPE":"CHECK_PROBE");return;}drive(resumeCm,resumePwm,pausedAction);return;
  }
  if(action=="BOTH_SIDE_SCAN"){if(scanUsed>=scanBudget)catFailed("two turns exhausted before side handoff");else beginScan(scanKind,scanNext,1,scanUsed);return;}
  if(action=="END_SESSION"||action=="WAIT_INTERACTION_RELEASE"){if(touching()){if(action!="WAIT_INTERACTION_RELEASE")state("WAIT_INTERACTION_RELEASE");return;}waitCommand("WAIT_SESSION_STOP","SESSION_STOP");return;}
  if(action=="TOUCH_SIGNAL"){signal(true);return;}
  if(action=="PREY_WAIT"){if(departed(true)||reached(due)){logLine("PREY_COMPLETE","search and face cat");search("CHASE_ALIGN");}return;}
  if(action=="WAIT_RELEASE"){
    if(touching())return;
    if(touchNext=="CONFIRM_TOUCH"){beginScan("CAT","CHOOSE_TOUCH",closeSide(0)&&closeSide(1)?1:0);return;}
    if(touchNext=="TOUCH_SIGNAL"){
      if(touchSearch)beginScan("CAT","TOUCH_SIGNAL",closeSide(0)&&closeSide(1)?1:0);else signal(true);return;
    }
    state("ESCAPE_DIRECTION");return;
  }
  if(action=="ESCAPE_DIRECTION"){
    if(touching())return;
    startEscapeSearch();return;
  }
  if(action=="ESCAPE_SIDE_SETTLE"||action=="ESCAPE_FRONT_SETTLE"){
    if(touching())return;
    if(elapsed(actionAt,setting("catSettleTimeoutMs"))){exitInteraction("escape encoder settle unavailable");return;}
    bool sampled=int32_t(encoderAt[0]-actionAt)>0&&int32_t(encoderAt[1]-actionAt)>0;
    if(sampled&&!firstSettleSampleAt){firstSettleSampleAt=quietAt=now();quietEncoder[0]=encoder[0];quietEncoder[1]=encoder[1];return;}
    bool second=firstSettleSampleAt&&int32_t(encoderAt[0]-firstSettleSampleAt)>0&&int32_t(encoderAt[1]-firstSettleSampleAt)>0;
    if(sampled&&second&&encoderRpm[0]==0&&encoderRpm[1]==0&&elapsed(actionAt,setting("scanMinimumMs"))&&elapsed(quietAt,setting("scanQuietMs"))){
      if(action=="ESCAPE_FRONT_SETTLE")beginEscapeFrontMonitor();
      else{escapeObserveAt=now();state("ESCAPE_SIDE_CHECK");}
    }
    return;
  }
  if(action=="ESCAPE_SIDE_CHECK"){
    if(touching())return;
    int selected=escapeDirection>0?1:0;
    if(!escapeSideOpen(selected)&&escapeTurns==0&&escapeSideOpen(1-selected)){escapeDirection=-escapeDirection;selected=1-selected;}
    if(escapeSideOpen(selected)){
      if(escapeTurns>=3){exitInteraction("escape search exhausted three directions");return;}
      ++escapeTurns;logLine("ESCAPE_SIDE_GATE",String(selected?"RIGHT":"LEFT")+" cm="+String(side[selected],1)+" threshold="+String(max(setting("escapeTurnClearanceCm"),safe[selected]),1)+" turn="+String(escapeTurns)+"/3");
      rotate(escapeDirection*90.0f,(int)setting("searchPwm"),"ESCAPE_TURN");return;
    }
    if(escapeSideFresh(selected)&&(escapeTurns>0||escapeSideFresh(1-selected))){exitInteraction("escape side below turn clearance");return;}
    if(elapsed(actionAt,2500))exitInteraction("escape side observation unavailable");
    return;
  }
  if(action=="ESCAPE_TURN"){
    int selected=escapeDirection>0?1:0;
    if(touching()){pauseForTouch();return;}
    if(!sideAt[selected]||elapsed(sideAt[selected],2500)||side[selected]<0||closeSide(selected)){exitInteraction("escape turn side protection unavailable or blocked");return;}
    return;
  }
  if(action=="ESCAPE_FRONT_CHECK"){
    if(touching())return;
    bool newFloor=String(observation["mode"]|"")=="FLOOR"&&
      int32_t((observation["frameSeq"]|0u)-escapeObserveFrame)>0&&
      observation["at"].is<uint32_t>()&&int32_t(observation["at"].as<uint32_t>()-escapeObserveAt)>0;
    if(newFloor){
      if(!fresh()||!config["frontCalibrated"].as<bool>()||!observation["frontCalibrated"].as<bool>()){exitInteraction("escape front observation unavailable");return;}
      String front=observation["front"]|"INVALID";
      logLine("ESCAPE_FRONT_GATE",front+" turn="+String(escapeTurns)+"/3 frame="+String(observation["frameSeq"]|0u));
      if(front=="CLEAR"){state("ESCAPE_DRIVE");return;}
      if(front=="BLOCKED"){continueEscapeSearch();return;}
      exitInteraction("escape front observation invalid");return;
    }
    if(elapsed(actionAt,setting("scanVisionTimeoutMs")))exitInteraction("escape front vision timeout");
    return;
  }
  if(action=="WAIT_CHASE"){
    if(!chaseDetected&&(!fresh()||!cat()["valid"].as<bool>())){farAt=leaveFrame=0;if(!chaseUnavailableAt)chaseUnavailableAt=now();if(elapsed(chaseUnavailableAt,setting("scanVisionTimeoutMs")))exitInteraction("cat observation unavailable");return;}
    chaseUnavailableAt=0;
    if(!chaseDetected){if(departed(true,true)){exitInteraction("cat absent for configured leave interval");return;}if(farAt)return;}
    if(!chaseDetected&&fresh()&&cat()["valid"].as<bool>()){
      if(!cat()["detected"].as<bool>()||cat()["width"].as<float>()<setting("catNearWidth"))chaseWasOutside=true;
      else if(chaseWasOutside)chaseDetected=true;
    }
    if(chaseDetected){logLine("CHASE_DETECTED","next interaction");if(branch==0){signal(true);return;}escape=draw()<50?"SLOW_STRAIGHT_ESCAPE":"FAST_STRAIGHT_ESCAPE";turnDeg=180;touchNext="ESCAPE";touchSearch=false;directionLocated=true;state("WAIT_RELEASE");}else if(reached(due))exitInteraction("chase wait timeout");return;
  }
  if(touching()&&action!="SIGNAL"&&action!="OBSERVE"&&action!="FOLLOW_WAIT"){if(motorId)pauseForTouch();return;}
  if(action=="CAT_CONFIRM"&&due&&(encoderRpm[0]>0||encoderRpm[1]>0)){logLine("CONFIRM_MOVING","wheel RPM nonzero; stop and restart confirmation");confirmCat(afterFace);return;}
  if(action=="SETTLE"||action=="TAG_SETTLE"||action=="CAT_CONFIRM"&&!due){
    if(elapsed(actionAt,setting(action=="TAG_SETTLE"?"scanSettleTimeoutMs":"catSettleTimeoutMs"))){if(action=="TAG_SETTLE")fail("tag encoder settle unavailable");else catFailed("encoder settle unavailable");return;}
    bool sampled=int32_t(encoderAt[0]-actionAt)>0&&int32_t(encoderAt[1]-actionAt)>0;
    if(sampled&&!firstSettleSampleAt){firstSettleSampleAt=quietAt=now();quietEncoder[0]=encoder[0];quietEncoder[1]=encoder[1];return;}
    bool second=firstSettleSampleAt&&int32_t(encoderAt[0]-firstSettleSampleAt)>0&&int32_t(encoderAt[1]-firstSettleSampleAt)>0;
    if(sampled&&second&&encoderRpm[0]==0&&encoderRpm[1]==0&&elapsed(actionAt,setting("scanMinimumMs"))&&elapsed(quietAt,setting("scanQuietMs"))){if(action=="CAT_CONFIRM"){due=now();if(!due)due=1;}else if(action=="TAG_SETTLE")startTagRecheck();else oneFrame(afterSettle);}return;
  }
  if(action=="TAG_RECHECK"){
    if(encoderRpm[0]>0||encoderRpm[1]>0||encoder[0]!=tagRecheckEncoder[0]||encoder[1]!=tagRecheckEncoder[1]){fail("tag recheck wheels moved");return;}
    if(checkReady&&approachFrontBlocked()){arrivalFrame=observation["frameSeq"]|0u;approachEnded("FRONT_BLOCKED");awaitStop("TAG_ARRIVAL");return;}
    if(checkReady&&!approachFrontClear()){
      if(!frontUnavailableAt){frontUnavailableAt=now();approachEnded("FRONT_UNAVAILABLE");}
      awaitStop("APPROACH_TAG");return;
    }
    if(checkReady){
      checkReady=false;if(!fresh()||!observation["tagsValid"].as<bool>()){fail("tag recheck invalid or expired frame");return;}
      bool found=false;for(JsonVariant t:observation["tags"].as<JsonArray>())if(t["id"].as<int>()==config["route"][routeIndex].as<int>()){tagMemory.observe(t["id"].as<int>(),observation["frameSeq"]|0u,observation["at"].as<uint32_t>(),t["centerX"].as<float>(),t["width"].as<float>());found=true;}
      if(found){tagRecoveryStage=0;alignSteps=0;lastDecisionFrame=0;logLine("TAG_RECOVERY","stationary recheck found target");state("APPROACH_TAG");}
      else startTagRecovery();
    }else if(elapsed(checkAt,uint32_t(setting("scanVisionTimeoutMs"))*(config["tagScanMaxFrames"]|3)))fail("tag recheck new frames timeout");
    return;
  }
  if(action=="CAT_CONFIRM"&&!checkPending&&!checkReady){if(reached(due)){oneFrame("CAT_CONFIRMED","CAT_CONFIRM");}return;}
  if(action=="CHECK_APPROACH"||action=="CHECK_PROBE"){settle(action=="CHECK_APPROACH"?"APPROACH_CAT":"PROBE_DONE");return;}
  if(action=="POST_ESCAPE"){escapeRounds++;if(escapeRounds>=(int)setting("escapePreyAfterRounds")||draw()<setting("escapePreyPercent")){prey(false);return;}search("CHASE_ALIGN");return;}
  if(action=="CAT_CHECK"||action=="CAT_CONFIRM"){
    if(action=="CAT_CHECK"&&(afterSettle=="APPROACH_CAT"||afterSettle=="PROBE_DONE")&&checkReady&&approachFrontBlocked()){interactAtFrontObstacle();return;}
    if(action=="CAT_CHECK"&&(afterSettle=="APPROACH_CAT"||afterSettle=="PROBE_DONE")&&checkReady&&!approachFrontClear()){
      if(!frontUnavailableAt){frontUnavailableAt=now();approachEnded("FRONT_UNAVAILABLE");}
      awaitStop(afterSettle=="PROBE_DONE"?"PROBE_DRIVE":"APPROACH_CAT");return;
    }
    if(checkReady){checkReady=false;if(!fresh()){if(action=="CAT_CHECK")fail("cat check stopped frame expired");else catFailed("stopped frame expired");return;}if(!cat()["valid"].as<bool>()){if(action=="CAT_CHECK")fail("cat check invalid cat frame");else catFailed("invalid cat frame");return;}if(!cat()["detected"].as<bool>()){
      if(initiator=="ROBOT_SEARCH"&&robotFound&&afterSettle!="APPROACH_CAT"&&afterSettle!="PROBE_DONE"){robotObserveWithoutCat();return;}
      if(afterSettle=="CAT_CONFIRMED"){if(scanUsed>=max(1,(int)lroundf(720.0f*20*track/(360*wheel)))){catFailed("confirmation lost cat; scan budget exhausted");return;}searchCatTarget(afterFace,scanFlags,scanUsed);}
      else searchCatTarget(afterSettle=="APPROACH_CAT"?"RESUME_APPROACH":afterSettle=="PROBE_DONE"?"PROBE_DONE":afterFace);return;
    }String next=afterSettle;if(next=="CAT_CONFIRMED"){logLine("CAT_CONFIRMED","new stationary frame="+String(observation["frameSeq"]|0u));catLocated();}else if(next=="CAT_LOCATED")catLocated();else if(next=="PROBE_DONE"){hold("PROBE_PAUSE",setting(probeStep==1?"probePause1Ms":probeStep==2?"probePause2Ms":"probePause3Ms"));}else state(next);}
    else if(elapsed(checkAt,setting("scanVisionTimeoutMs"))){if(action=="CAT_CHECK")fail("cat check new stopped frames timeout");else catFailed("new stopped cat frame timeout");}return;
  }

  if(action=="SEARCH_CAT"){searchCatTarget(afterFace);return;}
  if(initiator=="CAT_TOUCH"&&branch==0&&!touching()&&(action=="SIGNAL"||action=="OBSERVE"||action=="FOLLOW_WAIT")&&elapsed(lastTouch,setting("catInteractionIdleSeconds")*1000)){search("CHASE_ALIGN");return;}
  if(action=="SIGNAL"){if(reached(due)){quietOutputs();if(completeRobotAction())return;if(branch==0&&initiator=="CAT_TOUCH"){hold("OBSERVE",setting("initialLookSeconds")*1000);return;}face("OBSERVE");}return;}
  if(action=="OBSERVE"){if(reached(due)){if(completeRobotAction())return;hold("FOLLOW_WAIT",setting("decisionIntervalMs"));}return;}
  if(action=="FOLLOW_WAIT"){if(!reached(due))return;
    if(initiator=="CAT_TOUCH"){
      if(branch==0)return;
      else{escape=draw()<50?"SLOW_STRAIGHT_ESCAPE":"FAST_STRAIGHT_ESCAPE";turnDeg=180;touchNext="ESCAPE";touchSearch=false;directionLocated=false;state("WAIT_RELEASE");}
    }else chooseRobot();return;
  }
  if(action=="PROBE_PAUSE"){if(!reached(due))return;probeStep++;if(probeStep>3){if(completeRobotAction())return;if(initiator=="ROBOT_SEARCH")chooseStoppedRobot();else face("OBSERVE");}else{segmentCm=setting(probeStep==2?"probeStep2Cm":"probeStep3Cm");state("PROBE_DRIVE");}return;}
  if(action=="APPROACH_CAT"||action=="PROBE_DRIVE"||action=="ESCAPE_DRIVE"){
    if(action=="APPROACH_CAT"||action=="PROBE_DRIVE"){
      if(approachFrontBlocked()){interactAtFrontObstacle();return;}
      if(!approachFrontClear()){
        if(!frontUnavailableAt){frontUnavailableAt=now();approachEnded("FRONT_UNAVAILABLE");}
        if(elapsed(frontUnavailableAt,setting("scanVisionTimeoutMs")))fail("front observation unavailable while approaching cat");
        return;
      }
      frontUnavailableAt=0;
    }
    if(action=="APPROACH_CAT"){if(remainingCm<=0||robotFast&&seen()&&cat()["width"].as<float>()>setting("catFarWidth")){approachEnded(remainingCm<=0?"DISTANCE_COMPLETE":"WIDTH_REACHED");bool wasFast=robotFast;robotFast=robotSlowFlash=false;if(completeRobotAction())return;if(initiator=="ROBOT_SEARCH"){if(wasFast)face("SLOW_APPROACH");else chooseStoppedRobot();}else face("OBSERVE");return;}if(!seen()){if(initiator=="ROBOT_SEARCH"&&robotFound)robotObserveWithoutCat();else beginScan("CAT","RESUME_APPROACH");return;}if(robotSlowFlash)sendUno("LED FLASH 200 "+String((int)setting("signalFlashMs")));drive(remainingCm,(int)setting(robotFast?"robotFastApproachPwm":"slowApproachPwm"),"CAT_DRIVE");}
    else if(action=="PROBE_DRIVE"){if(initiator=="ROBOT_SEARCH"&&robotFound&&!seen())robotObserveWithoutCat();else drive(segmentCm,(int)setting("probePwm"),"PROBE_MOVE");}
    else{if(!escapeFrontClear()){exitInteraction("escape front unavailable before drive");return;}if(escapeRemainingCm<=0){chooseEscapeDistance();escapeRemainingCm=segmentCm;}drive(escapeRemainingCm,(int)setting(escape=="FAST_STRAIGHT_ESCAPE"?"fastEscapePwm":"slowEscapePwm"),"ESCAPE_MOVE");}return;
  }

  if(action=="ESCAPE_MOVE"){
    if(!fresh()||!config["frontCalibrated"].as<bool>()||!observation["frontCalibrated"].as<bool>()){exitInteraction("escape front expired or uncalibrated");return;}
    String front=observation["front"]|"INVALID";
    if(front=="BLOCKED"){
      escapeRemainingCm=max(0.0f,driveCm-travel());
      if(escapeRemainingCm<=0){awaitStop("POST_ESCAPE");return;}
      continueEscapeSearch();return;
    }
    if(front!="CLEAR")exitInteraction("escape front invalid during drive");
    return;
  }
  if(action=="CAT_DRIVE"||action=="PROBE_MOVE"){
    if(approachFrontBlocked()){interactAtFrontObstacle();return;}
    if(!approachFrontClear()){
      if(!frontUnavailableAt){frontUnavailableAt=now();approachEnded("FRONT_UNAVAILABLE");}
      awaitStop(action=="CAT_DRIVE"?"APPROACH_CAT":"PROBE_DRIVE");
    }
    return;
  }
  if(phase=="PATROL"){
    auto route=config["route"].as<JsonArray>();if(!route.size()){fail("empty route");return;}routeIndex%=route.size();int target=route[routeIndex];
    if(action=="SCAN_TAG"){beginScan("TAG","APPROACH_TAG");return;}
    if(action=="TAG_ARRIVAL"){clearTagRecovery();logLine("TAG_REACHED",String(target));int previous=target;routeIndex=(routeIndex+1)%route.size();needRearm=route[routeIndex].as<int>()==previous;state(needRearm?"TAG_REARM":"SCAN_TAG");return;}
    // Front safety wins over target loss, alignment and target width.
    if(action=="APPROACH_TAG"||action=="TAG_ALIGN"){
      uint32_t f=observation["frameSeq"]|0u;
      bool newFrame=f&&(!lastDecisionFrame||int32_t(f-lastDecisionFrame)>0)&&f!=arrivalFrame;
      if(newFrame&&approachFrontBlocked()){arrivalFrame=f;approachEnded("FRONT_BLOCKED");awaitStop("TAG_ARRIVAL");return;}
      if(!approachFrontClear()){
        if(!frontUnavailableAt){frontUnavailableAt=now();approachEnded("FRONT_UNAVAILABLE");}
        if(elapsed(frontUnavailableAt,setting("scanVisionTimeoutMs"))){fail("front observation unavailable while approaching tag");return;}
        if(motorId)awaitStop("APPROACH_TAG");return;
      }
      frontUnavailableAt=0;
    }
    if(!fresh()||!observation["tagsValid"].as<bool>()){if(motorId)stopForTagRecheck("camera invalid or stale");else if(elapsed(actionAt,setting("scanVisionTimeoutMs")))stopForTagRecheck("camera unavailable while approaching");return;}
    uint32_t frame=observation["frameSeq"]|0u;
    if(!frame||lastDecisionFrame&&int32_t(frame-lastDecisionFrame)<=0||frame==arrivalFrame){if(tagMemory.expired(now(),config["tagLossGraceMs"]|1000))stopForTagRecheck("loss grace expired");return;}lastDecisionFrame=frame;
    JsonVariant tag;for(JsonVariant t:observation["tags"].as<JsonArray>())if(t["id"].as<int>()==target){tag=t;break;}
    if(action=="TAG_REARM"){if(tag.isNull()||tag["width"].as<float>()<setting("tagStopWidth")*.8f){needRearm=false;state("SCAN_TAG");}return;}
    if(tag.isNull()){
      bool forward=action=="APPROACH_TAG"&&motorId&&motorConfirmed&&motorRunning&&leftPwm>0&&leftPwm==rightPwm;
      if(forward){bool first=!tagMemory.losing;tagMemory.miss(now());if(first)logLine("TAG_TRACK_LOST","grace started; target="+String(target)+" frame="+String(frame)+" graceMs="+String(config["tagLossGraceMs"]|1000)+" hold current forward command");if(!tagMemory.expired(now(),config["tagLossGraceMs"]|1000))return;}
      stopForTagRecheck(forward?"loss grace expired":"target missing while stopped or aligning");return;
    }
    if(tagMemory.losing)logLine("TAG_RECOVERY","reacquired during grace; frame="+String(frame)+" lossMs="+String(now()-tagMemory.lostAt));
    tagMemory.observe(target,frame,observation["at"].as<uint32_t>(),tag["centerX"].as<float>(),tag["width"].as<float>());
    // Arrival is checked before alignment and needs a checked STOP before advance.
    if(tag["width"].as<float>()>=setting("tagStopWidth")){approachEnded("WIDTH_REACHED");arrivalFrame=frame;awaitStop("TAG_ARRIVAL");return;}
    if(action=="TAG_ALIGN"){if(elapsed(actionAt,setting("alignTimeoutMs")))fail("tag alignment motion timeout");return;}
    float e=tag["centerX"].as<float>()-.5f;
    if(fabs(e)>setting("tagCenterTolerancePercent")/100){if(motorId){awaitStop("APPROACH_TAG");return;}if(alignSteps++>=8){fail("tag alignment budget exhausted");return;}rotate(e<0?-20.25f:20.25f,(int)setting("patrolAlignPwm"),"TAG_ALIGN");return;}
    if(!approachFrontClear())return;
    move((int)setting("slowApproachPwm"),(int)setting("slowApproachPwm"));
  }
}
