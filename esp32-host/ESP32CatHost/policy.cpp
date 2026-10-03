#include <Arduino.h>
#include <ArduinoJson.h>
#include <math.h>
#include "policy_ids.h"
extern String phase, mode;
extern JsonDocument config, observation;
extern uint32_t lastUnoAt;
extern uint32_t sendUno(const String &);
extern void logLine(const char *,const String &);
extern void clearTx();
namespace {
String action="IDLE", afterFace="OBSERVE", escape="SLOW_STRAIGHT_ESCAPE", initiator;
String pendingCommand, lastFault, lastMotionFailure;
uint32_t actionAt=0, sessionAt=0, lastTouch=0, due=0, farAt=0, feedbackAt=0;
uint32_t motorId=0, sessionId=0, sessionCommandAt=0, geometryId=0;
bool touch[4]={}, motorConfirmed=false, geometryConfirmed=false;
bool motorDispatched=false;
int rounds=0, routeIndex=0, leftPwm=0,rightPwm=0, branch=-1, patterns=0,lastPattern=-1, probeStep=0, sessionRetries=0;
float pulsesL=0,pulsesR=0, swept=0, wheel=67.5, track=120, segmentCm=0, turnDeg=0;
uint32_t lastRobotProbe=0,lastCatProbe=0;bool followPending=false;
uint32_t patrolReadyAt=0,blockedUntil=0;int scanDirection=1,refusals=0;
bool frontEnabled(){return config["frontAvoidanceEnabled"]|true;}
float setting(const char *name){return config[name].as<float>();}
uint32_t now(){return millis();}
bool touching(){return touch[0]||touch[1]||touch[2]||touch[3];}
int draw(){return esp_random()%100;}
bool fresh(){return observation["at"].is<uint32_t>()&&now()-observation["at"].as<uint32_t>()<setting("visionMaxAgeMs")&&((mode=="SIM_UNO")==(String(observation["source"]|"")=="SIM_CAMERA"));}
JsonVariant cat(){return observation["cat"];}
bool seen(){return fresh()&&cat()["detected"].as<bool>();}
float angle(){return (pulsesL+pulsesR)*wheel/20.0f/track*180.0f/PI;}
void state(const String &next){action=next;actionAt=now();logLine("DECISION",phase+" "+action);}
void stopMotor(){if(motorId){if(leftPwm!=rightPwm)swept+=angle();sendUno("STOP");}motorId=0;motorConfirmed=false;leftPwm=rightPwm=0;pulsesL=pulsesR=0;}
void fail(const String &reason){lastFault=reason;clearTx();sendUno("STOP");sendUno("SESSION_STOP");motorId=0;motorConfirmed=false;phase="STOPPED";state("FAULT");logLine("FAULT",reason);}
void move(int l,int r){if(now()<blockedUntil)return;if(motorId&&l==leftPwm&&r==rightPwm)return;stopMotor();leftPwm=l;rightPwm=r;pulsesL=pulsesR=0;motorId=sendUno("MOTOR "+String(l)+" "+String(r));motorDispatched=false;feedbackAt=now();motorConfirmed=false;}
void waitCommand(const String &next,const String &command){pendingCommand=command;sessionId=sendUno(command);sessionCommandAt=now();sessionRetries=0;state(next);}
void requestConfig(){geometryConfirmed=false;waitCommand("WAIT_PATROL_CONFIG","GET_CONFIG");}
void beginSession(){phase="STARTING";waitCommand("WAIT_SESSION_START","SESSION_START");}
void finish(){stopMotor();phase="ENDING";waitCommand("WAIT_SESSION_STOP","SESSION_STOP");}
void hold(const String &next,uint32_t ms){stopMotor();state(next);due=now()+ms;}
void face(const String &next){afterFace=next;stopMotor();swept=0;state("FACE_CAT");}
void search(const String &next){afterFace=next;stopMotor();swept=0;state("SEARCH_CAT");}
void prey(bool signal){stopMotor();state("PREY_WAIT");due=now()+setting("captureWaitMaxSeconds")*1000;farAt=0;if(signal){sendUno("LED FLASH 200 2200");sendUno("BEEP 360");}}
void signal(bool exploration=false){stopMotor();uint32_t duration=setting("signalFlashMs");if(exploration){if(!patterns)patterns=15;int picks[4],n=0;for(int i=0;i<4;i++)if((patterns&(1<<i))&&(i!=lastPattern||patterns==(1<<i)))picks[n++]=i;int p=picks[esp_random()%n];patterns&=~(1<<p);lastPattern=p;int interval[]={0,120,600,1400};int lengths[]={1800,2400,3600,5600};duration=lengths[p];sendUno(p==0?String("LED ON"):"LED FLASH "+String(interval[p])+" "+String(duration));sendUno("BEEP "+String(120+p*60));}else{sendUno("LED FLASH 200 "+String(duration));if(setting("signalBeepMs")>0)sendUno("BEEP "+String((int)setting("signalBeepMs")));}hold("SIGNAL",duration);}
void behavior(const String &name){if(name=="SLOW_APPROACH"){segmentCm=setting("slowApproachCm");state("APPROACH_CAT");}else if(name=="PROBE_APPROACH"){probeStep=1;segmentCm=setting("probeStep1Cm");state("PROBE_DRIVE");}else if(name=="STAY_SIGNAL")signal();else hold("OBSERVE",setting("initialLookSeconds")*1000);}
void chooseRobot(){if(!seen()){search("CHOOSE_ROBOT");return;}bool far=cat()["width"].as<float>()<setting("catNearWidth");if(far){bool probe=draw()<setting("robotFarProbePercent")&&now()-lastRobotProbe>=setting("robotProbeCooldownSeconds")*1000;if(probe)lastRobotProbe=now();face(probe?"PROBE_APPROACH":"SLOW_APPROACH");}else{int n=draw();face(n<setting("robotNearSignalPercent")?"STAY_SIGNAL":n<setting("robotNearSignalPercent")+setting("robotNearObservePercent")?"OBSERVE":"STAY");}}
void onTouch(int area){bool active=touching();touch[area]=true;stopMotor();if(phase!="PATROL"&&phase!="INTERACTION")return;if(active)return;
  bool newEpisode=phase!="INTERACTION"||now()-lastTouch>=setting("touchDebounceMs");lastTouch=now();
  if(phase!="INTERACTION"){phase="INTERACTION";sessionAt=now();rounds=0;branch=-1;initiator="CAT_TOUCH";}
  if(initiator!="CAT_TOUCH"){initiator="CAT_TOUCH";rounds=0;branch=-1;}if(!newEpisode)return;
  rounds++;followPending=true;
  if(setting("endingAfterRounds")>0&&rounds>=setting("endingAfterRounds")){int n=draw();if(n<setting("endingPatrolPercent"))finish();else prey(n>=setting("endingPatrolPercent")+setting("endingSilentPercent"));return;}
  if(branch<0)branch=draw()<setting("explorationBranchPercent")?0:1;if(branch==0){signal(true);return;}
  if(action=="WAIT_RELEASE"||action=="ESCAPE_TURN"||action=="ESCAPE_DRIVE"){state("WAIT_RELEASE");return;}
  if(draw()<setting("touchFlashPercent")){sendUno("LED FLASH 120 1400");sendUno("BEEP 180");}
  int n=draw();if(n<setting("touchPreyPercent")){prey(false);return;}escape=n<setting("touchPreyPercent")+setting("touchSlowEscapePercent")?"SLOW_STRAIGHT_ESCAPE":"FAST_STRAIGHT_ESCAPE";
  float cx=seen()?cat()["centerX"].as<float>():.5f;turnDeg=seen()?(cx>=.5f?-90:90):area==1?-90:area==0?90:area==2?0:180;state("WAIT_RELEASE");
}
void motionFailure(const String &reason){lastMotionFailure=reason;motorId=0;motorConfirmed=false;logLine("MOTION_FAILED",reason);if(reason.startsWith("TOUCH"))return;if(reason.startsWith("SIDE_")&&(action=="SCAN_TAG"||action=="SEARCH_CAT")&&refusals++<2){scanDirection=-scanDirection;blockedUntil=now()+1000;logLine("SCAN_REVERSE","fresh Uno safety check required");return;}if(phase=="INTERACTION")finish();else fail(reason);}
}
void policyCommandSent(uint32_t id){if(id&&id==motorId){motorDispatched=true;feedbackAt=now();}}
void policyStopUnconfirmed(){lastFault="STOP_UNCONFIRMED: motor stop has not been confirmed";motorId=0;motorConfirmed=motorDispatched=false;leftPwm=rightPwm=0;phase="STOPPED";state("FAULT");}
bool policyPreparing(){return phase=="STARTING"||phase=="ENDING";}
void policySnapshot(JsonDocument &d){auto p=d["patrolStatus"].to<JsonObject>();p["action"]=action;p["lastFault"]=lastFault;p["lastMotionFailure"]=lastMotionFailure;p["motorId"]=motorId;p["motorConfirmed"]=motorConfirmed;p["leftPwm"]=leftPwm;p["rightPwm"]=rightPwm;p["targetIndex"]=routeIndex;}
void policyControl(const String &control){if(control=="stop"){fail("operator_stop");return;}if(control=="start"||control=="resume"){
  if(!config["route"].size()){logLine("INPUT_ERROR","save your tag route before starting patrol");return;}
  routeIndex=0;logLine("FRONT_AVOIDANCE",frontEnabled()?"enabled":"disabled by operator; ESP front check skipped; calibration unchanged");
  clearTx();motorId=0;motorConfirmed=false;leftPwm=rightPwm=0;pulsesL=pulsesR=0;rounds=0;branch=-1;initiator="";farAt=0;followPending=false;swept=0;blockedUntil=patrolReadyAt=0;refusals=0;scanDirection=1;geometryConfirmed=false;lastFault=lastMotionFailure="";
  phase="STARTING";waitCommand("WAIT_PATROL_STOP","STOP");return;
}if(control=="alert"&&phase=="PATROL"){stopMotor();phase="INTERACTION";initiator="ROBOT_SEARCH";sessionAt=now();rounds=0;branch=-1;chooseRobot();}}
void policyInput(const String &line){unsigned id=0;unsigned long l=0,r=0;
  if(line.startsWith("READY")){if(phase!="IDLE"&&phase!="STOPPED")fail("Uno reboot");geometryConfirmed=false;memset(touch,0,sizeof(touch));return;}
  if(sscanf(line.c_str(),"MS %u %lu %lu",&id,&l,&r)==3&&matchesPending(id,motorId)){motorConfirmed=true;feedbackAt=now();return;}
  if(sscanf(line.c_str(),"MP %u %lu %lu",&id,&l,&r)==3&&matchesPending(id,motorId)){pulsesL=l;pulsesR=r;motorConfirmed=true;feedbackAt=now();return;}
  if(line.startsWith("AK ")||line.startsWith("ACK ")){sscanf(line.c_str(),line.startsWith("AK ")?"AK %u":"ACK %u",&id);if(matchesPending(id,motorId))motorConfirmed=true;return;}
  if(line.startsWith("MA ")){char reason[100];if(sscanf(line.c_str(),"MA %u %99s %lu %lu",&id,reason,&l,&r)==4&&matchesPending(id,motorId))motionFailure(reason);return;}
  if(line.startsWith("ERROR ")){char reason[100];if(sscanf(line.c_str(),"ERROR %u %99s",&id,reason)==2){if(!id)logLine("UNO_PROTOCOL_ERROR",String("unidentified command: ")+reason);else if(matchesPending(id,motorId))motionFailure(reason);else if(matchesPending(id,sessionId)||matchesPending(id,geometryId))fail(String("required command: ")+reason);}return;}
  if(action=="WAIT_PATROL_CONFIG"&&line.indexOf(" CONFIG ")>=0&&sscanf(line.c_str(),"DATA %u",&id)==1&&matchesPending(id,sessionId)){int wi=line.indexOf("WHEEL_MM="),ti=line.indexOf("TRACK_MM=");float w=wi>=0?line.substring(wi+9).toFloat():0,t=ti>=0?line.substring(ti+9).toFloat():0;if(w>=20&&w<=150&&t>=50&&t<=300){wheel=w;track=t;geometryConfirmed=true;}}
  if(line.startsWith("EVENT")&&line.indexOf(" TOUCH ")>=0){int p=line.indexOf(" TOUCH ")+7;String rest=line.substring(p);int area=rest.startsWith("LEFT ")?0:rest.startsWith("RIGHT ")?1:rest.startsWith("REAR ")?2:rest.startsWith("TOP ")?3:-1;if(area>=0){if(rest.indexOf(" PRESS")>=0)onTouch(area);else if(rest.indexOf(" RELEASE")>=0)touch[area]=false;}return;}
  if(line.startsWith("DATA ")&&line.indexOf(" HW T ")>=0){int p=line.indexOf(" HW T ")+6;char area[8],src[12];int enabled,raw,pressed;if(sscanf(line.substring(p).c_str(),"%7s %d %d %d %11s",area,&enabled,&raw,&pressed,src)==5){int a=!strcmp(area,"LEFT")?0:!strcmp(area,"RIGHT")?1:!strcmp(area,"REAR")?2:3;if(pressed&&!touch[a])onTouch(a);else if(!pressed)touch[a]=false;}}
  if(line.startsWith("EVENT MOTION_ABORTED ")){char reason[80];if(sscanf(line.c_str(),"EVENT MOTION_ABORTED %u %79s",&id,reason)==2&&matchesPending(id,motorId))motionFailure(reason);return;}
  if(line.startsWith("DONE ")){sscanf(line.c_str(),"DONE %u",&id);if(matchesPending(id,sessionId)){
    if(action=="WAIT_PATROL_STOP"){if(mode=="SIM_UNO")waitCommand("WAIT_PATROL_GEOMETRY","SET_GEOMETRY "+String(setting("wheelMm"))+" "+String(setting("trackMm")));else requestConfig();}
    else if(action=="WAIT_PATROL_GEOMETRY")requestConfig();
    else if(action=="WAIT_PATROL_CONFIG"){if(!geometryConfirmed){fail("geometry not confirmed in current GET_CONFIG reply");return;}beginSession();}
    else if(action=="WAIT_SESSION_STOP"){rounds=0;branch=-1;followPending=false;beginSession();}
    else if(action=="WAIT_SESSION_START"){if(!geometryConfirmed){fail("geometry not confirmed; set real wheel/track then GET_CONFIG");return;}phase="PATROL";swept=0;state("SCAN_TAG");}
  }}
}
void policyTick(){
  observation["policy"]["action"]=action;observation["policy"]["targetIndex"]=routeIndex;observation["policy"]["rounds"]=rounds;
  if(phase=="IDLE"||phase=="STOPPED")return;
  if(mode=="UART"&&lastUnoAt&&now()-lastUnoAt>2500){fail("Uno link timeout");return;}
  if(policyPreparing()){if(now()-sessionCommandAt>2500){if(sessionRetries++<2){if(action=="WAIT_PATROL_CONFIG")geometryConfirmed=false;sessionId=sendUno(pendingCommand);sessionCommandAt=now();logLine("STARTUP_RETRY",action+" "+pendingCommand);}else fail("confirmation timeout: "+action+" "+pendingCommand);}return;}
  if(motorId&&motorDispatched){if(now()-feedbackAt>750){fail("no checked encoder feedback for 750ms");return;}}
  if(phase=="INTERACTION"&&action!="PREY_WAIT"&&now()-sessionAt>setting("sessionLimitSeconds")*1000){finish();return;}
  if(action=="PREY_WAIT"){bool far=seen()&&!touching()&&cat()["width"].as<float>()<setting("catFarWidth");if(far){if(!farAt)farAt=now();if(now()-farAt>=setting("catLeaveSeconds")*1000){finish();return;}}else farAt=0;if(now()>=due)fail("MAX_WAIT_UNCONFIRMED: manual resume required");return;}
  if(action=="WAIT_RELEASE"){if(touching()){if(now()-actionAt>setting("touchReleaseTimeoutMs"))finish();return;}if(fabs(turnDeg)<1){segmentCm=setting("escapeDistanceCm");state("ESCAPE_DRIVE");}else{state("ESCAPE_TURN");int p=setting("searchPwm");move(turnDeg<0?-p:p,turnDeg<0?p:-p);}return;}
  if(touching()&&action!="SIGNAL"){stopMotor();return;}
  if(action=="ESCAPE_TURN"){if(angle()>=fabs(turnDeg)){stopMotor();segmentCm=setting("escapeDistanceCm");state("ESCAPE_DRIVE");}return;}
  if(action=="SIGNAL"){if(now()>=due){sendUno("LED OFF");face("OBSERVE");}return;}
  if(action=="OBSERVE"){if(now()>=due)hold("FOLLOW_WAIT",setting("decisionIntervalMs"));return;}
  if(action=="FOLLOW_WAIT"){if(now()<due)return;if(initiator=="CAT_TOUCH"){
    if(setting("endingInactivitySeconds")>0&&now()-lastTouch>setting("endingInactivitySeconds")*1000){finish();return;}
    if(followPending){followPending=false;int n=draw();bool probe=seen()&&now()-lastCatProbe>=setting("catProbeCooldownSeconds")*1000;if(probe&&n<setting("catFollowProbePercent")){lastCatProbe=now();face("PROBE_APPROACH");}else if(n<setting("catFollowProbePercent")+setting("catFollowObservePercent"))face("OBSERVE");else finish();}
  }else chooseRobot();return;}
  if(action=="WAIT_CHASE"){if(now()>=due)face("OBSERVE");return;}
  if(action=="PROBE_PAUSE"){if(now()<due)return;probeStep++;if(probeStep>3){face("OBSERVE");return;}segmentCm=setting(probeStep==2?"probeStep2Cm":"probeStep3Cm");state("PROBE_DRIVE");return;}
  if(action=="APPROACH_CAT"||action=="ESCAPE_DRIVE"||action=="PROBE_DRIVE"){
    if(action!="ESCAPE_DRIVE"&&!seen()){stopMotor();finish();return;}
    if(!fresh()||(frontEnabled()&&String(observation["front"]|"INVALID")!="CLEAR")){stopMotor();if(now()-actionAt>2000)finish();return;}
    if(motorId){float cm=(pulsesL+pulsesR)*PI*wheel/400.0f;if(cm>=max(0.0f,segmentCm-setting("stopMarginCm"))){String current=action;stopMotor();if(current=="PROBE_DRIVE")hold("PROBE_PAUSE",setting(probeStep==1?"probePause1Ms":probeStep==2?"probePause2Ms":"probePause3Ms"));else if(current=="ESCAPE_DRIVE")face("WAIT_CHASE");else face("OBSERVE");}return;}
    {int p=action=="ESCAPE_DRIVE"?(int)setting(escape=="FAST_STRAIGHT_ESCAPE"?"fastEscapePwm":"slowEscapePwm"):(int)setting(action=="PROBE_DRIVE"?"probePwm":"slowApproachPwm");move(p,p);}return;
  }
  if(action=="SEARCH_CAT"||action=="FACE_CAT"){
    if(!fresh()||(cat()["valid"].is<bool>()&&!cat()["valid"].as<bool>())){stopMotor();if(now()-actionAt>setting("searchTimeoutMs"))finish();return;}
    if(seen()){float e=cat()["centerX"].as<float>()-.5f;if(fabs(e)<=setting("centerTolerancePercent")/100){stopMotor();String next=afterFace;if(next=="CHOOSE_ROBOT")chooseRobot();else if(next=="WAIT_CHASE")hold("WAIT_CHASE",setting("postEscapeWaitSeconds")*1000);else behavior(next);return;}int p=setting("alignPwm");move(e<0?-p:p,e<0?p:-p);}
    else{if(swept+angle()>=360||now()-actionAt>setting("searchTimeoutMs")){finish();return;}int p=setting("searchPwm");move(scanDirection*p,-scanDirection*p);}return;
  }
  if(phase=="PATROL"){
    if(now()<patrolReadyAt)return;
    auto route=config["route"].as<JsonArray>();if(!route.size()){fail("empty route");return;}routeIndex%=route.size();int target=route[routeIndex];JsonVariant tag;for(JsonVariant v:observation["tags"].as<JsonArray>())if(v["id"].as<int>()==target){tag=v;break;}
    if(!fresh()||(observation["tagsValid"].is<bool>()&&!observation["tagsValid"].as<bool>())){stopMotor();if(now()-actionAt>setting("searchTimeoutMs"))fail("tag vision unavailable");return;}
    if(tag.isNull()){if(swept+angle()>=360||now()-actionAt>setting("searchTimeoutMs")){fail("tag search exhausted id="+String(target));return;}int p=setting("patrolSearchPwm");move(scanDirection*p,-scanDirection*p);return;}
    float e=tag["centerX"].as<float>()-.5f;if(fabs(e)>setting("centerTolerancePercent")/100){int p=setting("patrolAlignPwm");move(e<0?-p:p,e<0?p:-p);return;}
    if(tag["width"].as<float>()>=setting("tagStopWidth")){stopMotor();routeIndex=(routeIndex+1)%route.size();swept=0;refusals=0;patrolReadyAt=now()+500;state("SCAN_TAG");logLine("TAG_REACHED",String(target));return;}
    if(frontEnabled()&&String(observation["front"]|"INVALID")!="CLEAR"){stopMotor();if(now()-actionAt>setting("searchTimeoutMs"))fail("front unavailable or blocked");return;}
    move(setting("slowApproachPwm"),setting("slowApproachPwm"));
  }
}
