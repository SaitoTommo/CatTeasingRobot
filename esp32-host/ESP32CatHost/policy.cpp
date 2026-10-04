#include "pwm_floor.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <math.h>
#include "policy_ids.h"
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
namespace {
String action="IDLE",afterFace="OBSERVE",afterStop,afterSettle,scanKind,scanNext;
bool returnToPatrol=true,startCat=false;
int pendingFirstTouch=-1,escapeRounds=0,exploreRounds=0,catDistance=-1;
uint32_t interactionId=0,exploreAt=0,leaveFrame=0;
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
uint32_t generation=1,scanId=0,scanAt=0,scanStep=0,scanFrame=0,lastVqUsed=0,checkStep=0,checkAt=0;
uint32_t lastDecisionFrame=0,arrivalFrame=0,catFailureAt=0;
uint32_t encoder[2]={},encoderAt[2]={},quietEncoder[2]={},quietAt=0;
uint32_t firstTouchUnoAt=0,telemetryUnoAt=0;bool touchTie=false;
uint32_t firstSettleSampleAt=0;
uint32_t sideAt[2]={};float side[2]={-1,-1},safe[2]={1,1};
bool touch[4]={},motorConfirmed=false,motorDispatched=false,motorRunning=false;
bool geometryConfirmed=false,scanCapable=false,followPending=false,scanReplyFound=false;
bool checkPending=false,checkReady=false,needRearm=false,multiTouch=false,touchSearch=false,touchCue=false,scanWaiting=false;
int rounds=0,routeIndex=0,leftPwm=0,rightPwm=0,branch=-1,patterns=0,lastPattern=-1,probeStep=0,sessionRetries=0,firstTouch=-1;
int scanReplyResult=0,scanOrigin=0;bool scanDoneSeen=false;int scanFlags=0,scanDirection=1,scanBudget=0,scanUsed=0,alignSteps=0;
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
bool closeSide(int i){return sideAt[i]&&!elapsed(sideAt[i],2500)&&side[i]>=0&&side[i]<safe[i];}
void state(const String &s){action=s;actionAt=now();logLine("DECISION",phase+" "+s);}
void resetMotor(){motorId=0;motorConfirmed=motorDispatched=motorRunning=false;leftPwm=rightPwm=0;pulsesL=pulsesR=0;}
void invalidate(){generation=generation%2147483647+1;scanId=scanStep=scanFrame=0;scanConfigRetryPending=false;checkPending=checkReady=false;cancelVision();}
void quietOutputs(){sendUno("LED OFF");sendUno("BEEP 0");}
void fail(const String &reason){lastFault=reason;invalidate();clearTx();resetMotor();stopId=sessionId=0;sendUno("STOP");sendUno("SESSION_STOP");quietOutputs();phase="STOPPED";state("FAULT");logLine("FAULT",reason);}
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
void awaitStop(const String &next){invalidate();clearTx();sessionId=0;resetMotor();afterStop=next;stopId=sendUno("STOP");quietOutputs();state("WAIT_STOP");}
void requestConfig(){geometryConfirmed=false;waitCommand("WAIT_PATROL_CONFIG","GET_CONFIG");}
void beginSession(){phase="STARTING";waitCommand("WAIT_SESSION_START","SESSION_START");}
void finish(){touchPaused=false;invalidate();quietOutputs();phase="ENDING";awaitStop("END_SESSION");}
void exitInteraction(const String &reason){interactionExitReason=reason;logLine("INTERACTION_EXIT",reason);finish();}
void newInteraction(){interactionId++;escapeRounds=exploreRounds=0;exploring=false;exploreAt=0;catDistance=-1;farAt=leaveFrame=0;touchPaused=false;chaseDetected=false;directionLocated=false;interactionExitReason="";}
int distanceClass(){if(!seen())return -1;float w=cat()["width"].as<float>();if(w<=setting("catFarWidth"))catDistance=0;else if(w>=setting("catNearWidth"))catDistance=1;return catDistance;}
bool departed(bool allowAbsent){
  if(touching()||!fresh()||!cat()["valid"].as<bool>()){farAt=leaveFrame=0;return false;}
  bool away=cat()["detected"].as<bool>()?cat()["width"].as<float>()<=setting("catFarWidth"):allowAbsent;
  if(!away){farAt=leaveFrame=0;return false;}uint32_t frame=observation["frameSeq"]|0u;if(!frame||frame==leaveFrame)return false;leaveFrame=frame;
  if(!farAt)farAt=now();return elapsed(farAt,setting("catLeaveSeconds")*1000);
}
void catFailed(const String &reason){catFailure="找猫失败";catFailureReason=reason;catFailureAt=now();catFailureAngle=(scanId||action=="SEARCH_CAT_ACTIVE"||action=="BOTH_SIDE_SCAN")?scanAngle():0;logLine("CAT_SEARCH_FAILED",catFailure+" "+reason+" angle="+String(scanAngle(),1));finish();}
void hold(const String &s,uint32_t ms){state(s);due=now()+ms;}
void settle(const String &next){afterSettle=next;firstSettleSampleAt=0;quietAt=now();quietEncoder[0]=encoder[0];quietEncoder[1]=encoder[1];state("SETTLE");}
void oneFrame(const String &next){afterSettle=next;checkStep=checkStep%65535+1;checkAt=now();checkPending=true;checkReady=false;requestVision(0,checkStep,generation,"CAT");state("CAT_CHECK");}
void move(int l,int r){if(motorId&&leftPwm==l&&rightPwm==r)return;if(motorId){awaitStop("APPROACH_TAG");return;}leftPwm=l;rightPwm=r;pulsesL=pulsesR=0;motorId=sendUno("MOTOR "+String(l)+" "+String(r));motorDispatched=motorConfirmed=motorRunning=false;feedbackAt=now();}
void drive(float cm,int pwm,const String &s){driveCm=cm;leftPwm=rightPwm=pwm;pulsesL=pulsesR=0;motorId=sendUno("DRIVE "+String(cm,2)+" "+String(pwm));motorConfirmed=motorDispatched=motorRunning=false;feedbackAt=now();state(s);}
void rotate(float degrees,int pwm,const String &s){turnDeg=degrees;leftPwm=degrees>0?pwm:-pwm;rightPwm=-leftPwm;pulsesL=pulsesR=0;motorId=sendUno("ROTATE "+String(degrees,1)+" "+String(pwm));motorConfirmed=motorDispatched=motorRunning=false;feedbackAt=now();state(s);}
void beginScan(const String &kind,const String &next,int flags=0,int spent=0){
  invalidate();clearTx();scanKind=kind;scanNext=next;scanFlags=flags;scanOrigin=spent;scanUsed=spent;lastVqUsed=0;scanDoneSeen=false;scanBudget=max(1,(int)lroundf(720.0f*20*track/(360*wheel)));scanAt=now();
  int steps=(scanBudget-scanUsed+(int)setting("scanStepPulses")-1)/(int)setting("scanStepPulses");
  uint32_t total=min(uint32_t(600000),(uint32_t)(steps*(setting("scanVisionTimeoutMs")+setting("scanSettleTimeoutMs")+10000)+1000));
  waitCommand("WAIT_SCAN_CONFIG","SC "+String((int)setting("scanMinimumMs"))+" "+String((int)setting("scanQuietMs"))+" "+String((int)setting("scanVisionTimeoutMs"))+" "+String(total)+" "+String((int)setting("scanSettleTimeoutMs")));
}
void search(const String &next){afterFace=next;awaitStop("SEARCH_CAT");}
void face(const String &next){afterFace=next;alignSteps=0;awaitStop("FACE_CAT");}
void prey(bool signal){resetMotor();quietOutputs();hold("PREY_WAIT",setting("captureWaitMaxSeconds")*1000);farAt=leaveFrame=0;logLine("PREY","stationary; MazeBot LED is manual");}
void signal(bool exploration=false){if(exploration&&!exploring){exploring=true;exploreAt=now();}uint32_t duration=setting("signalFlashMs");if(exploration){if(!patterns)patterns=15;int picks[4],n=0;for(int i=0;i<4;i++)if((patterns&(1<<i))&&(i!=lastPattern||patterns==(1<<i)))picks[n++]=i;int p=picks[esp_random()%n];patterns&=~(1<<p);lastPattern=p;int interval[]={0,120,600,1400},lengths[]={1800,2400,3600,5600};duration=lengths[p];sendUno(p==0?String("LED ON"):"LED FLASH "+String(interval[p])+" "+String(duration));sendUno("BEEP "+String(120+p*60));}else{sendUno("LED FLASH 200 "+String(duration));sendUno("BEEP "+String((int)setting("signalBeepMs")));}hold("SIGNAL",duration);}
void behavior(const String &s){
  if(s=="SLOW_APPROACH"){remainingCm=max(0.0f,setting("slowApproachCm")-setting("stopMarginCm"));state("APPROACH_CAT");}
  else if(s=="RESUME_APPROACH")state("APPROACH_CAT");
  else if(s=="PROBE_APPROACH"){probeStep=1;segmentCm=setting("probeStep1Cm");state("PROBE_DRIVE");}
  else if(s=="STAY_SIGNAL")signal();else if(s=="TOUCH_SIGNAL")signal(true);
  else if(s=="END")finish();else if(s=="PREY_SIGNAL")prey(true);else if(s=="PREY")prey(false);
  else if(s=="PROBE_DONE")hold("PROBE_PAUSE",setting(probeStep==1?"probePause1Ms":probeStep==2?"probePause2Ms":"probePause3Ms"));
  else if(s=="WAIT_CHASE"){chaseDetected=false;chaseWasOutside=!seen()||cat()["width"].as<float>()<setting("catNearWidth");hold("WAIT_CHASE",setting("postEscapeWaitSeconds")*1000);}
  else if(s=="ESCAPE_DIRECTION_FOUND"){directionLocated=true;turnDeg=180;state("ESCAPE_DIRECTION");}
  else if(s=="ESCAPE_DRIVE"){segmentCm=max(0.0f,setting("escapeDistanceCm")-setting("stopMarginCm"));state("ESCAPE_DRIVE");}
  else hold("OBSERVE",setting("initialLookSeconds")*1000);
}
void chooseRobot(){if(!seen()){search("CHOOSE_ROBOT");return;}int range=distanceClass();if(range<0){face("OBSERVE");return;}bool far=range==0;if(far){bool probe=draw()<setting("robotFarProbePercent")&&elapsed(lastRobotProbe,setting("robotProbeCooldownSeconds")*1000);if(probe)lastRobotProbe=now();face(probe?"PROBE_APPROACH":"SLOW_APPROACH");}else{int n=draw();face(n<setting("robotNearSignalPercent")?"STAY_SIGNAL":n<setting("robotNearSignalPercent")+setting("robotNearObservePercent")?"OBSERVE":"STAY");}}
void orient(){if(!seen()){beginScan("CAT",afterFace);return;}float e=cat()["centerX"].as<float>()-.5f;if(fabs(e)<=setting("centerTolerancePercent")/100){if(afterFace=="CHOOSE_ROBOT")chooseRobot();else behavior(afterFace);return;}if(alignSteps++>=8){catFailed("alignment budget exhausted");return;}rotate(e<0?-20.25f:20.25f,(int)setting("alignPwm"),"ALIGN_CAT");}
void pauseForTouch(){
  if(touchPaused||(!motorId&&action!="WAIT_SCAN_CONFIG"))return;
  touchPaused=true;pausedAction=action;pauseScan=scanId||action=="WAIT_SCAN_CONFIG";
  if(pauseScan){if(scanId&&!scanWaiting&&!scanDoneSeen)scanUsed=min(scanBudget,scanUsed+(int)max(pulsesL,pulsesR));}
  else{resumeCm=max(0.0f,driveCm-travel());resumeDegrees=turnDeg>0?max(0.0f,turnDeg-angle()):min(0.0f,turnDeg+angle());resumePwm=abs(leftPwm);if(action=="CAT_DRIVE")remainingCm=max(0.0f,remainingCm-travel());}
  awaitStop("WAIT_TOUCH_RESUME");
}
void firstTouchPlan(int area,uint32_t stamp){
  firstTouch=area;firstTouchUnoAt=stamp;touchTie=multiTouch=false;rounds=1;followPending=true;
  branch=draw()<setting("explorationBranchPercent")?0:1;
  touchNext=branch==0?"TOUCH_SIGNAL":"ESCAPE";
  escape=draw()<50?"SLOW_STRAIGHT_ESCAPE":"FAST_STRAIGHT_ESCAPE";
  touchCue=branch==1&&draw()<setting("touchFlashPercent");
  turnDeg=area==0?90:area==1?-90:0;scanDirection=area==1?-1:1;
  bool conflict=seen()&&(area==0&&cat()["centerX"].as<float>()>.55f||area==1&&cat()["centerX"].as<float>()<.45f);
  touchSearch=area==3||conflict;
  awaitStop(branch==0&&!touchSearch?"TOUCH_SIGNAL":"WAIT_RELEASE");
}
void onTouch(int area,uint32_t stamp=0){
  if(touch[area])return;bool wasHeld=touching();touch[area]=true;lastTouch=now();farAt=leaveFrame=0;
  if(phase=="INTERACTION"){
    if(wasHeld&&stamp&&stamp==firstTouchUnoAt&&rounds==1){touchTie=true;multiTouch=true;touchSearch=true;}
    if(action=="WAIT_CHASE")chaseDetected=true;
    pauseForTouch();logLine("TOUCH_CONTINUED","same interaction; no redraw");return;
  }
  if(phase=="PATROL"){returnToPatrol=true;phase="INTERACTION";initiator="CAT_TOUCH";sessionAt=now();newInteraction();firstTouchPlan(area,stamp);return;}
  if((phase=="IDLE"||phase=="STOPPED")&&stopReady()){
    pendingFirstTouch=area;firstTouchUnoAt=stamp;startCat=true;returnToPatrol=false;newInteraction();initiator="CAT_TOUCH";
    invalidate();clearTx();resetMotor();geometryConfirmed=false;lastFault=lastMotionFailure="";phase="STARTING";waitCommand("WAIT_PATROL_STOP","STOP");
  }
}
void motionFailure(const String &reason){if(reason.startsWith("TOUCH")&&phase=="INTERACTION"){pauseForTouch();return;}if(reason.startsWith("SIDE_")&&scanId&&scanKind=="CAT"&&!scanFlags&&closeSide(0)&&closeSide(1)){sessionAt+=now()-scanAt;scanUsed=min(scanBudget,scanUsed+(int)max(pulsesL,pulsesR));awaitStop("BOTH_SIDE_SCAN");return;}lastMotionFailure=reason;resetMotor();invalidate();logLine("MOTION_FAILED",reason);if(reason.startsWith("TOUCH"))return;if(reason.startsWith("SIDE_")&&phase=="INTERACTION")finish();else fail(reason);}
}
void policyCommandSent(uint32_t id){if(matchesPending(id,motorId)){motorDispatched=true;feedbackAt=now();}}
void policyStopUnconfirmed(){lastFault="STOP_UNCONFIRMED: motor stop has not been confirmed";invalidate();clearTx();resetMotor();stopId=sessionId=0;phase="STOPPED";state("FAULT");}
uint32_t policyTelemetryInterval(){return action=="SETTLE"?250:2000;}
bool policyPreparing(){return phase=="STARTING"||phase=="ENDING"||action=="WAIT_SCAN_CONFIG"||(scanId&&!motorConfirmed);}
int policyTargetId(){return phase=="PATROL"&&config["route"].size()?config["route"][routeIndex].as<int>():-1;}
String policyVisionMode(){
  if((action=="CAT_DRIVE"||action=="PROBE_MOVE"||action=="ESCAPE_MOVE")&&frontEnabled())return "FLOOR";
  // During any scan/slow approach/alignment, CAT is only requested while stopped.
  if(scanId||action=="WAIT_SCAN_CONFIG"||action=="SCAN_TAG"||action=="SEARCH_CAT"||action=="FACE_CAT"||action=="ALIGN_CAT"||action=="ESCAPE_TURN"||action=="APPROACH_CAT"||action=="CAT_DRIVE"||action=="CAT_CHECK"||action=="SETTLE"||action=="WAIT_STOP"||action=="PROBE_DRIVE"||action=="PROBE_MOVE")return "OFF";
  if(phase=="PATROL")return "TAGS";
  if(phase=="INTERACTION")return "CAT";
  return "USER";
}
void policySnapshot(JsonDocument &d){auto p=d["patrolStatus"].to<JsonObject>();p["action"]=action;p["nextAction"]=afterStop;p["scanPhase"]=scanId?(motorRunning?"TURN":scanWaiting?"VISION":"SETTLE"):"NONE";p["targetId"]=config["route"].size()?config["route"][routeIndex].as<int>():-1;p["visionAgeMs"]=observation["at"].is<uint32_t>()?now()-observation["at"].as<uint32_t>():UINT32_MAX;p["frameSeq"]=observation["frameSeq"]|0u;p["tagStopWidth"]=setting("tagStopWidth");p["routeCount"]=config["route"].size();float width=-1;if(fresh()&&observation["tagsValid"].as<bool>())for(JsonVariant t:observation["tags"].as<JsonArray>())if(t["id"].as<int>()==policyTargetId()){width=t["width"].as<float>();break;}p["tagWidth"]=width;p["targetIndex"]=routeIndex;p["rounds"]=rounds;p["selectedBehavior"]=afterFace;p["interactionBranch"]=branch;p["lightPattern"]=lastPattern;p["probeStep"]=probeStep;p["escape"]=escape;p["firstTouch"]=firstTouch;p["multipleTouches"]=multiTouch;p["simultaneousDetection"]=touchTie;p["touchSearchRequired"]=touchSearch;p["lastFault"]=lastFault;p["lastMotionFailure"]=lastMotionFailure;p["motorId"]=motorId;p["motorConfirmed"]=motorConfirmed;p["motorRunning"]=motorRunning;p["minimumDrivePwm"]=(int)setting("minimumDrivePwm");p["requestedLeftPwm"]=leftPwm;p["requestedRightPwm"]=rightPwm;p["leftPwm"]=drivePwmFloor(leftPwm,(int)setting("minimumDrivePwm"));p["rightPwm"]=drivePwmFloor(rightPwm,(int)setting("minimumDrivePwm"));p["scanId"]=scanId;p["scanStep"]=scanStep;p["generation"]=generation;p["scanAngle"]=scanAngle();p["scanBudgetDegrees"]=scanBudget*wheel/20/track*360;p["remainingApproachCm"]=remainingCm;p["scanCapability"]=scanCapable;p["catFailure"]["message"]=catFailure;p["catFailure"]["reason"]=catFailureReason;p["catFailure"]["at"]=catFailureAt;p["catFailure"]["angle"]=catFailureAngle;p["unsupportedSettings"][0]="farCatThresholdCm";p["unsupportedSettings"][1]="catLeaveDistanceCm";p["unsupportedSettings"][2]="finalSearchTimeoutMs";p["unsupportedSettings"][3]="searchTimeoutMs";p["escapeStage"]=(action=="ESCAPE_DIRECTION"||action=="ESCAPE_TURN"||action=="WAIT_RELEASE"||scanId&&scanNext=="ESCAPE_DIRECTION_FOUND")?"DIRECTION":(action=="ESCAPE_DRIVE"||action=="ESCAPE_MOVE")?"RUN":"NONE";p["escapeDistanceCm"]=driveCm;p["escapeTravelCm"]=travel();p["interactionId"]=interactionId;p["escapeRounds"]=escapeRounds;p["exploreRounds"]=exploreRounds;p["catDistance"]=distanceClass();p["interactionExitReason"]=interactionExitReason;
  p["catWidth"]=seen()?cat()["width"].as<float>():-1;p["settleElapsedMs"]=action=="SETTLE"?now()-actionAt:0;p["settleQuietMs"]=action=="SETTLE"?now()-quietAt:0;
  p["settleSamples"]=action=="SETTLE"?(firstSettleSampleAt?(int32_t(encoderAt[0]-firstSettleSampleAt)>0&&int32_t(encoderAt[1]-firstSettleSampleAt)>0?2:1):0):0;
  uint32_t explorationMs=exploring?now()-exploreAt:0,minimumMs=setting("explorePreyMinimumSeconds")*1000;p["exploreElapsedMs"]=explorationMs;p["exploreRemainingMs"]=explorationMs>=minimumMs?0:minimumMs-explorationMs;p["remainingWaitMs"]=(action=="PREY_WAIT"||action=="WAIT_CHASE"||action=="SIGNAL"||action=="OBSERVE"||action=="FOLLOW_WAIT"||action=="PROBE_PAUSE")&&!reached(due)?due-now():0;p["departureElapsedMs"]=farAt?now()-farAt:0;}
void policyControl(const String &control){
  if(control=="stop"){touchPaused=false;pendingFirstTouch=-1;startCat=false;fail("operator_stop");return;}
  if(control=="start"||control=="resume"){
    if(!stopReady()){logLine("INPUT_ERROR","wait for confirmed STOP before starting");return;}
    if(!config["route"].size()){logLine("INPUT_ERROR","save your tag route before starting patrol");return;}
    newInteraction();pendingFirstTouch=-1;startCat=false;returnToPatrol=true;invalidate();clearTx();resetMotor();routeIndex=0;rounds=0;branch=-1;initiator="";followPending=needRearm=false;lastDecisionFrame=arrivalFrame=0;lastFault=lastMotionFailure="";geometryConfirmed=false;phase="STARTING";waitCommand("WAIT_PATROL_STOP","STOP");return;
  }
  if(control=="alert"){
    if(phase=="PATROL"){newInteraction();pendingFirstTouch=-1;returnToPatrol=true;phase="INTERACTION";initiator="ROBOT_SEARCH";sessionAt=now();rounds=0;branch=-1;search("CHOOSE_ROBOT");}
    else if((phase=="IDLE"||phase=="STOPPED")&&stopReady()){newInteraction();pendingFirstTouch=-1;startCat=true;returnToPatrol=false;invalidate();clearTx();resetMotor();initiator="ROBOT_SEARCH";rounds=0;branch=-1;geometryConfirmed=false;lastFault=lastMotionFailure="";phase="STARTING";waitCommand("WAIT_PATROL_STOP","STOP");}
  }
}
void policyVision(){
  uint32_t frame=observation["frameSeq"]|0u;
  if(!frame||!fresh())return;
  uint32_t gen=observation["generation"]|0u,sid=observation["scanId"]|0u,step=observation["stepId"]|0u;
  if(gen!=generation)return;
  if(scanId&&sid==scanId&&step==scanStep){
    if(scanFrame==frame)return;bool valid=scanKind=="CAT"?cat()["valid"].as<bool>():observation["tagsValid"].as<bool>();bool found=false;
    if(valid){if(scanKind=="CAT")found=cat()["detected"].as<bool>();else{int target=config["route"][routeIndex];for(JsonVariant t:observation["tags"].as<JsonArray>())if(t["id"].as<int>()==target)found=true;}}
    scanFrame=frame;scanReplyFound=found;scanReplyResult=valid?(found?1:0):2;sendUno("VR "+String(scanId)+" "+String(scanStep)+" "+String(generation)+" "+String(valid?(found?1:0):2)+" "+String(frame));
  }else if(checkPending&&!sid&&step==checkStep){checkPending=false;checkReady=true;}
}
void policyInput(const String &line){
  unsigned id=0;unsigned long l=0,r=0,gen=0,step=0;int result=0;unsigned used=0;char extra=0;
  if(line.startsWith("READY")){if(phase!="IDLE"&&phase!="STOPPED")fail("Uno reboot");geometryConfirmed=scanCapable=false;invalidate();memset(touch,0,sizeof(touch));return;}
  if(line=="EVENT CAPABILITIES SCAN_V1"){scanCapable=true;return;}
  if(sscanf(line.c_str(),"VQ %u %lu %lu %u %c",&id,&step,&gen,&used,&extra)==4&&matchesPending(id,scanId)&&gen==generation&&step>0&&step<=uint32_t((scanBudget+(int)setting("scanStepPulses")-1)/(int)setting("scanStepPulses"))){
    if(step<scanStep||used>unsigned(scanBudget-scanOrigin)||step==scanStep&&used!=lastVqUsed||step>scanStep&&used<lastVqUsed)return;
    lastVqUsed=used;scanUsed=scanOrigin+used;pulsesL=pulsesR=0;
    if(step==scanStep&&scanFrame){sendUno("VR "+String(scanId)+" "+String(scanStep)+" "+String(generation)+" "+String(scanReplyResult)+" "+String(scanFrame));return;}
    if(step>scanStep){scanStep=step;scanFrame=0;}
    scanWaiting=true;motorConfirmed=true;motorRunning=false;leftPwm=rightPwm=0;requestVision(scanId,scanStep,generation,scanKind=="CAT"?"CAT":"TAGS");return;
  }
  if(sscanf(line.c_str(),"SD %u %lu %d %c",&id,&gen,&result,&extra)==3&&matchesPending(id,scanId)&&gen==generation&&result>=0&&result<=5){
    sessionAt+=now()-scanAt;String kind=scanKind,next=scanNext;scanId=0;resetMotor();cancelVision();
    if(result==1){if(kind=="TAG"){alignSteps=0;state("APPROACH_TAG");lastDecisionFrame=0;}else{afterFace=next;alignSteps=0;orient();}}
    else if(kind=="CAT")catFailed("scan result="+String(result));else fail("tag search failed result="+String(result));return;
  }
  if(sscanf(line.c_str(),"MS %u %lu %lu",&id,&l,&r)==3&&matchesPending(id,motorId)){scanWaiting=false;scanDoneSeen=false;pulsesL=pulsesR=0;motorConfirmed=motorRunning=true;if(scanId){int p=(int)setting(scanKind=="TAG"?"patrolSearchPwm":"searchPwm");leftPwm=scanDirection*p;rightPwm=-leftPwm;}feedbackAt=now();return;}
  if(sscanf(line.c_str(),"MP %u %lu %lu",&id,&l,&r)==3&&matchesPending(id,motorId)){if(scanId&&scanWaiting)return;pulsesL=l;pulsesR=r;motorConfirmed=motorRunning=true;feedbackAt=now();return;}
  if(sscanf(line.c_str(),"MD %u %lu %lu",&id,&l,&r)==3&&matchesPending(id,motorId)){
    pulsesL=l;pulsesR=r;motorRunning=false;leftPwm=rightPwm=0;feedbackAt=now();
    if(scanId){if(!scanDoneSeen)scanUsed=min(scanBudget,scanUsed+(int)max(l,r));scanDoneSeen=true;return;}
    String previous=action;if(previous=="ESCAPE_TURN")segmentCm=max(0.0f,setting("escapeDistanceCm")-setting("stopMarginCm"));if(previous=="CAT_DRIVE")remainingCm=max(0.0f,remainingCm-travel());resetMotor();
    awaitStop(previous=="CAT_DRIVE"?"CHECK_APPROACH":previous=="ALIGN_CAT"?"CHECK_ALIGN":previous=="PROBE_MOVE"?"CHECK_PROBE":previous=="ESCAPE_MOVE"?"POST_ESCAPE":previous=="ESCAPE_TURN"?"ESCAPE_DRIVE":"APPROACH_TAG");return;
  }
  if(line.startsWith("AK ")||line.startsWith("ACK ")){sscanf(line.c_str(),line.startsWith("AK ")?"AK %u":"ACK %u",&id);if(matchesPending(id,motorId))motorConfirmed=true;return;}
  if(line.startsWith("MA ")){char reason[100];if(sscanf(line.c_str(),"MA %u %99s %lu %lu",&id,reason,&l,&r)==4&&matchesPending(id,motorId)){pulsesL=l;pulsesR=r;motionFailure(reason);}return;}
  if(line.startsWith("ERROR ")){char reason[100];if(sscanf(line.c_str(),"ERROR %u %99s",&id,reason)==2){
    if(scanId&&!motorConfirmed&&(!id||matchesPending(id,scanId))&&(!strcmp(reason,"BAD_CHECKSUM")||!strcmp(reason,"BAD_ENVELOPE"))){retryScanStart(reason);return;}
    if(action=="WAIT_SCAN_CONFIG"&&sessionId&&(!id||matchesPending(id,sessionId))&&(!strcmp(reason,"BAD_CHECKSUM")||!strcmp(reason,"BAD_ENVELOPE"))){retryScanConfig(reason);return;}
    if(!id)logLine("UNO_PROTOCOL_ERROR",reason);else if(matchesPending(id,motorId))motionFailure(reason);else if(matchesPending(id,sessionId)||matchesPending(id,stopId))fail(String("required command: ")+reason);}return;}
  if(action=="WAIT_PATROL_CONFIG"&&line.indexOf(" CONFIG ")>=0&&sscanf(line.c_str(),"DATA %u",&id)==1&&matchesPending(id,sessionId)){int wi=line.indexOf("WHEEL_MM="),ti=line.indexOf("TRACK_MM="),sl=line.indexOf("SAFE_LEFT_CM="),sr=line.indexOf("SAFE_RIGHT_CM=");float w=wi>=0?line.substring(wi+9).toFloat():0,t=ti>=0?line.substring(ti+9).toFloat():0;if(w>=20&&w<=150&&t>=50&&t<=300){wheel=w;track=t;geometryConfirmed=true;if(sl>=0)safe[0]=line.substring(sl+13).toFloat();if(sr>=0)safe[1]=line.substring(sr+14).toFloat();}}
  if(line.startsWith("EVENT")&&line.indexOf(" TOUCH ")>=0){String rest=line.substring(line.indexOf(" TOUCH ")+7);int a=rest.startsWith("LEFT ")?0:rest.startsWith("RIGHT ")?1:rest.startsWith("REAR ")?2:rest.startsWith("TOP ")?3:-1;if(a>=0){if(rest.indexOf(" PRESS")>=0)onTouch(a,rest.substring(rest.indexOf(" PRESS")+7).toInt());else if(rest.indexOf(" RELEASE")>=0)touch[a]=false;}return;}
  if(line.startsWith("DATA ")&&line.indexOf(" HW ")>=0){unsigned long uptime=0;if(sscanf(line.c_str(),"DATA %u HW STATE %lu",&id,&uptime)==2)telemetryUnoAt=uptime;char kind[4],area[8],src[12];int en,raw,pressed;unsigned long count;float rpm;
    if(sscanf(line.c_str(),"DATA %u HW %3s %d %d %lu %f %11s",&id,kind,&en,&raw,&count,&rpm,src)==7&&(!strcmp(kind,"EL")||!strcmp(kind,"ER"))){int s=!strcmp(kind,"ER");encoder[s]=count;encoderAt[s]=now();if(count!=quietEncoder[s]){quietAt=now();quietEncoder[s]=count;}}
    float distance,physical;int echo;if(sscanf(line.c_str(),"DATA %u HW %3s %d %d %d %f %f %11s",&id,kind,&en,&raw,&echo,&physical,&distance,src)==9&&(!strcmp(kind,"SL")||!strcmp(kind,"SR"))){int s=!strcmp(kind,"SR");side[s]=distance;sideAt[s]=now();}
    if(sscanf(line.c_str(),"DATA %u HW T %7s %d %d %d %11s",&id,area,&en,&raw,&pressed,src)==6){int a=!strcmp(area,"LEFT")?0:!strcmp(area,"RIGHT")?1:!strcmp(area,"REAR")?2:3;if(pressed&&!touch[a])onTouch(a,telemetryUnoAt);else if(!pressed)touch[a]=false;}
  }
  if(line.startsWith("DONE ")){sscanf(line.c_str(),"DONE %u",&id);
    if(matchesPending(id,stopId)&&line=="DONE "+String(stopId)+" MOTOR_STOPPED"){stopId=0;if(touchCue){touchCue=false;sendUno("LED FLASH 120 "+String((int)setting("signalFlashMs")));sendUno("BEEP "+String((int)setting("touchFeedbackBeepMs")));}state(afterStop);return;}
    if(!matchesPending(id,sessionId))return;if(action=="WAIT_PATROL_STOP"&&!line.endsWith(" MOTOR_STOPPED"))return;
    if(action=="WAIT_SCAN_CONFIG"&&line!="DONE "+String(sessionId)+" SCAN_CONFIGURED")return;
    scanConfigRetryPending=false;sessionId=0;
    if(action=="WAIT_PATROL_STOP"&&line.endsWith(" MOTOR_STOPPED")){if(mode=="SIM_UNO")waitCommand("WAIT_PATROL_GEOMETRY","SET_GEOMETRY "+String(setting("wheelMm"))+" "+String(setting("trackMm")));else requestConfig();}
    else if(action=="WAIT_PATROL_GEOMETRY")requestConfig();
    else if(action=="WAIT_PATROL_CONFIG"){if(!geometryConfirmed)fail("geometry not confirmed");else beginSession();}
    else if(action=="WAIT_SESSION_STOP"){rounds=0;branch=-1;followPending=false;if(returnToPatrol)beginSession();else{phase="IDLE";state("IDLE");}}
    else if(action=="WAIT_SESSION_START"){if(!geometryConfirmed)fail("geometry not confirmed");else if(startCat){startCat=false;phase="INTERACTION";sessionAt=now();if(pendingFirstTouch>=0){int area=pendingFirstTouch;pendingFirstTouch=-1;firstTouchPlan(area,firstTouchUnoAt);}else search("CHOOSE_ROBOT");}else{phase="PATROL";state(needRearm?"TAG_REARM":"SCAN_TAG");}}
    else if(action=="WAIT_SCAN_CONFIG"){
      int pwm=scanKind=="TAG"?(int)setting("patrolSearchPwm"):(int)setting("searchPwm");
      scanStartCommand="SR "+String(generation)+" "+String(scanDirection*(int)setting("scanStepPulses"))+" "+String(drivePwmFloor(pwm,(int)setting("minimumDrivePwm")))+" "+String(scanBudget-scanUsed)+" "+String(scanFlags);
      scanStartRetries=0;scanWaiting=false;scanId=motorId=sendUno(scanStartCommand);motorConfirmed=motorDispatched=motorRunning=false;feedbackAt=now();state(scanKind=="TAG"?"SCAN_TAG_ACTIVE":"SEARCH_CAT_ACTIVE");
    }
  }
}
void policyTick(){
  if(phase=="IDLE"||phase=="STOPPED")return;
  if(mode=="UART"&&(!lastUnoAt||elapsed(lastUnoAt,2500))){fail("Uno link timeout");return;}
  if(action=="WAIT_STOP"){if(elapsed(actionAt,2500))fail("STOP confirmation timeout");return;}
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
  if(scanId){if(elapsed(scanAt,600000))fail("scan transaction timeout");return;}
  if(action=="WAIT_TOUCH_RESUME"){
    if(touching())return;touchPaused=false;
    if(pauseScan){if(scanUsed>=scanBudget)catFailed("two turns exhausted after contact");else beginScan(scanKind,scanNext,scanFlags,scanUsed);return;}
    if(pausedAction=="ALIGN_CAT"||pausedAction=="ESCAPE_TURN"){
      if(fabs(resumeDegrees)<1){awaitStop(pausedAction=="ALIGN_CAT"?"CHECK_ALIGN":"ESCAPE_DRIVE");return;}rotate(resumeDegrees,resumePwm,pausedAction);return;
    }
    if(pausedAction=="CAT_DRIVE"){settle("APPROACH_CAT");return;}
    if(!frontClear()){if(elapsed(actionAt,2000))exitInteraction("front unavailable after contact");return;}
    if(resumeCm<=0){awaitStop(pausedAction=="ESCAPE_MOVE"?"POST_ESCAPE":"CHECK_PROBE");return;}drive(resumeCm,resumePwm,pausedAction);return;
  }
  if(action=="BOTH_SIDE_SCAN"){if(scanUsed>=scanBudget)catFailed("two turns exhausted before side handoff");else beginScan(scanKind,scanNext,1,scanUsed);return;}
  if(action=="END_SESSION"||action=="WAIT_INTERACTION_RELEASE"){if(touching()){if(action!="WAIT_INTERACTION_RELEASE")state("WAIT_INTERACTION_RELEASE");return;}waitCommand("WAIT_SESSION_STOP","SESSION_STOP");return;}
  if(action=="TOUCH_SIGNAL"){signal(true);return;}
  if(action=="PREY_WAIT"){if(departed(true))exitInteraction("cat departed while prey");else if(reached(due))exitInteraction("prey wait timeout");return;}
  if(action=="WAIT_RELEASE"){
    if(touching())return;
    if(touchNext=="TOUCH_SIGNAL"){
      if(touchSearch)beginScan("CAT","TOUCH_SIGNAL",closeSide(0)&&closeSide(1)?1:0);else signal(true);return;
    }
    state("ESCAPE_DIRECTION");return;
  }
  if(action=="ESCAPE_DIRECTION"){
    if(touching())return;
    bool both=closeSide(0)&&closeSide(1);
    if(touchSearch||both&&!directionLocated){touchSearch=false;beginScan("CAT","ESCAPE_DIRECTION_FOUND",both?1:0);return;}
    if(fabs(turnDeg)>=1&&closeSide(turnDeg>0?1:0)){exitInteraction("escape direction blocked");return;}
    if((!sideAt[0]||elapsed(sideAt[0],2500))||(!sideAt[1]||elapsed(sideAt[1],2500))){exitInteraction("escape side observation expired");return;}
    if(fabs(turnDeg)<1)behavior("ESCAPE_DRIVE");else rotate(turnDeg,(int)setting("searchPwm"),"ESCAPE_TURN");return;
  }
  if(action=="WAIT_CHASE"){
    if(!chaseDetected&&fresh()&&cat()["valid"].as<bool>()){
      if(!cat()["detected"].as<bool>()||cat()["width"].as<float>()<setting("catNearWidth"))chaseWasOutside=true;
      else if(chaseWasOutside)chaseDetected=true;
    }
    if(chaseDetected)face("OBSERVE");else if(reached(due))exitInteraction("chase wait timeout");return;
  }
  if(phase=="INTERACTION"&&!motorId&&action!="WAIT_RELEASE"&&action!="WAIT_TOUCH_RESUME"&&departed(false)){exitInteraction("cat departed");return;}
  if(touching()&&action!="SIGNAL"&&action!="OBSERVE"&&action!="FOLLOW_WAIT"){if(motorId)pauseForTouch();return;}
  if(action=="SETTLE"){
    if(elapsed(actionAt,setting("catSettleTimeoutMs"))){catFailed("encoder settle unavailable");return;}
    bool sampled=int32_t(encoderAt[0]-actionAt)>0&&int32_t(encoderAt[1]-actionAt)>0;
    if(sampled&&!firstSettleSampleAt){firstSettleSampleAt=quietAt=now();quietEncoder[0]=encoder[0];quietEncoder[1]=encoder[1];return;}
    bool second=firstSettleSampleAt&&int32_t(encoderAt[0]-firstSettleSampleAt)>0&&int32_t(encoderAt[1]-firstSettleSampleAt)>0;
    if(sampled&&second&&elapsed(actionAt,setting("scanMinimumMs"))&&elapsed(quietAt,setting("scanQuietMs")))oneFrame(afterSettle);return;
  }
  if(action=="CHECK_APPROACH"||action=="CHECK_ALIGN"||action=="CHECK_PROBE"){settle(action=="CHECK_APPROACH"?"APPROACH_CAT":action=="CHECK_ALIGN"?"ORIENT":"PROBE_DONE");return;}
  if(action=="POST_ESCAPE"){escapeRounds++;if(escapeRounds>=(int)setting("escapePreyAfterRounds")||draw()<setting("escapePreyPercent")){prey(false);return;}search("WAIT_CHASE");return;}
  if(action=="CAT_CHECK"){
    if(checkReady){checkReady=false;if(!fresh()){catFailed("stopped frame expired");return;}if(!cat()["valid"].as<bool>()){catFailed("invalid cat frame");return;}if(!cat()["detected"].as<bool>()){beginScan("CAT",afterSettle=="APPROACH_CAT"?"RESUME_APPROACH":afterSettle=="PROBE_DONE"?"PROBE_DONE":afterFace);return;}String next=afterSettle;if(next=="ORIENT")orient();else if(next=="PROBE_DONE"){hold("PROBE_PAUSE",setting(probeStep==1?"probePause1Ms":probeStep==2?"probePause2Ms":"probePause3Ms"));}else state(next);}
    else if(elapsed(checkAt,setting("scanVisionTimeoutMs")))catFailed("new stopped cat frame timeout");return;
  }
  if(action=="FACE_CAT"){settle("ORIENT");return;}
  if(action=="SEARCH_CAT"){beginScan("CAT",afterFace);return;}
  if(action=="SIGNAL"){if(reached(due)){quietOutputs();if(branch==0&&initiator=="CAT_TOUCH"){exploreRounds++;if(elapsed(exploreAt,setting("explorePreyMinimumSeconds")*1000)&&draw()<setting("explorePreyPercent")){prey(false);return;}}face("OBSERVE");}return;}
  if(action=="OBSERVE"){if(reached(due))hold("FOLLOW_WAIT",setting("decisionIntervalMs"));return;}
  if(action=="FOLLOW_WAIT"){if(!reached(due))return;
    if(initiator=="CAT_TOUCH"){
      if(branch==0)signal(true);
      else{escape=draw()<50?"SLOW_STRAIGHT_ESCAPE":"FAST_STRAIGHT_ESCAPE";turnDeg=180;touchNext="ESCAPE";touchSearch=false;directionLocated=false;state("WAIT_RELEASE");}
    }else chooseRobot();return;
  }
  if(action=="PROBE_PAUSE"){if(!reached(due))return;probeStep++;if(probeStep>3)face("OBSERVE");else{segmentCm=setting(probeStep==2?"probeStep2Cm":"probeStep3Cm");state("PROBE_DRIVE");}return;}
  if(action=="APPROACH_CAT"||action=="PROBE_DRIVE"||action=="ESCAPE_DRIVE"){
    if(!frontClear()){if(elapsed(actionAt,2000))finish();return;}
    if(action=="APPROACH_CAT"){if(remainingCm<=0){face("OBSERVE");return;}if(!seen()){beginScan("CAT","RESUME_APPROACH");return;}drive(min(remainingCm,setting("catApproachStepCm")),(int)setting("slowApproachPwm"),"CAT_DRIVE");}
    else if(action=="PROBE_DRIVE")drive(segmentCm,(int)setting("probePwm"),"PROBE_MOVE");
    else if(segmentCm<=0)search("WAIT_CHASE");else drive(segmentCm,(int)setting(escape=="FAST_STRAIGHT_ESCAPE"?"fastEscapePwm":"slowEscapePwm"),"ESCAPE_MOVE");return;
  }
  if(action=="ALIGN_CAT"&&elapsed(actionAt,setting("alignTimeoutMs"))){catFailed("alignment motion timeout");return;}
  if(action=="CAT_DRIVE"||action=="PROBE_MOVE"||action=="ESCAPE_MOVE"){
    // No CAT inference during these local DRIVE segments. The preceding stopped
    // frame authorizes a short segment; only an explicit fresh BLOCKED aborts it.
    if(frontEnabled()&&fresh()&&String(observation["front"]|"INVALID")=="BLOCKED")finish();return;
  }
  if(phase=="PATROL"){
    auto route=config["route"].as<JsonArray>();if(!route.size()){fail("empty route");return;}routeIndex%=route.size();int target=route[routeIndex];
    if(action=="SCAN_TAG"){beginScan("TAG","APPROACH_TAG");return;}
    if(action=="TAG_ARRIVAL"){logLine("TAG_REACHED",String(target));int previous=target;routeIndex=(routeIndex+1)%route.size();needRearm=route[routeIndex].as<int>()==previous;state(needRearm?"TAG_REARM":"SCAN_TAG");return;}
    if(!fresh()||!observation["tagsValid"].as<bool>()){if(motorId)awaitStop("SCAN_TAG");else if(elapsed(actionAt,setting("scanVisionTimeoutMs")))awaitStop("SCAN_TAG");return;}
    uint32_t frame=observation["frameSeq"]|0u;if(!frame||frame==lastDecisionFrame||frame==arrivalFrame)return;lastDecisionFrame=frame;
    JsonVariant tag;for(JsonVariant t:observation["tags"].as<JsonArray>())if(t["id"].as<int>()==target){tag=t;break;}
    if(action=="TAG_REARM"){if(tag.isNull()||tag["width"].as<float>()<setting("tagStopWidth")*.8f){needRearm=false;state("SCAN_TAG");}return;}
    if(tag.isNull()){awaitStop("SCAN_TAG");return;}
    // Arrival is checked before alignment and needs a checked STOP before advance.
    if(tag["width"].as<float>()>=setting("tagStopWidth")){arrivalFrame=frame;awaitStop("TAG_ARRIVAL");return;}
    if(action=="TAG_ALIGN"){if(elapsed(actionAt,setting("alignTimeoutMs")))fail("tag alignment motion timeout");return;}
    float e=tag["centerX"].as<float>()-.5f;
    if(fabs(e)>setting("centerTolerancePercent")/100){if(motorId){awaitStop("APPROACH_TAG");return;}if(alignSteps++>=8){fail("tag alignment budget exhausted");return;}rotate(e<0?-20.25f:20.25f,(int)setting("patrolAlignPwm"),"TAG_ALIGN");return;}
    if(!frontClear()){if(motorId)awaitStop("APPROACH_TAG");else if(elapsed(actionAt,setting("scanVisionTimeoutMs")))fail("front unavailable or blocked");return;}
    move((int)setting("slowApproachPwm"),(int)setting("slowApproachPwm"));
  }
}
