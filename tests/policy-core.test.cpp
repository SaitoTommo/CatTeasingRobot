#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <deque>
#include <cassert>
#include "../firmware/UnoCatController/scan_machine.h"
#include "../esp32-host/ESP32CatHost/vision_gate.h"
#include "../esp32-host/ESP32CatHost/pwm_floor.h"
#include "../esp32-host/ESP32CatHost/compact_telemetry.h"
String phase="IDLE",mode="SIM_UNO";
JsonDocument config,observation;
uint32_t lastUnoAt=0,tick=100000,seq=0;
uint32_t millis(){return tick;}
std::deque<uint32_t> draws;
uint32_t esp_random(){if(draws.empty())return 99;auto v=draws.front();draws.pop_front();return v;}
struct Command{uint32_t id;String text;};
std::vector<Command> commands;
struct Request{uint32_t id,step,gen;String mode;};
std::vector<Request> requests;
uint32_t sendUno(const String &s){seq=seq%65535+1;commands.push_back({seq,applyDrivePwmFloor(s,config["minimumDrivePwm"]|140)});return seq;}
void resendScanStart(uint32_t id,const String &s){commands.push_back({id,s});}
void clearTx(){commands.clear();}
void resetLinkState(){lastUnoAt=0;}
void logLine(const char *,const String &){}
bool stopGate=true;bool stopReady(){return stopGate;}
void cancelVision(){}
void requestVision(uint32_t id,uint16_t step,uint32_t gen,const char *m){requests.push_back({id,step,gen,m});}
#include "../esp32-host/ESP32CatHost/policy_defaults.h"
#define LED_BUILTIN 21
#define HIGH 1
#define LOW 0
#define OUTPUT 1
int ledLevel=HIGH;
void digitalWrite(uint8_t,int value){ledLevel=value;}
void pinMode(uint8_t,int){}
#include "../esp32-host/ESP32CatHost/host_led.h"
#include "../esp32-host/ESP32CatHost/policy.cpp"
unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL line %d: %s action=%s phase=%s\n",__LINE__,#x,action.c_str(),phase.c_str());exit(1);}}while(0)
void fixture(){
 stopGate=true;tick=100000;draws.clear();phase="STOPPED";policyControl("stop");commands.clear();requests.clear();
 config.clear();policyDefaults(config);config["catCheckMaxFrames"]=1;config["route"][0]=9;config["route"][1]=10;
 config["frontAvoidanceEnabled"]=false;config["visionMaxAgeMs"]=1500;config["tagStopWidth"]=.25;
 config["catNearWidth"]=.35;config["catFarWidth"]=.12;config["patrolSearchPwm"]=45;config["patrolAlignPwm"]=30;config["stopMarginCm"]=3;
 mode="SIM_UNO";phase="PATROL";action="APPROACH_TAG";actionAt=tick;sessionAt=tick;motorId=scanId=stopId=sessionId=0;
 scanCapable=geometryConfirmed=true;routeIndex=0;rounds=0;branch=-1;patterns=0;lastPattern=-1;probeStep=0;
 firstTouch=-1;multiTouch=needRearm=followPending=false;initiator="";lastTouch=tick;lastRobotProbe=lastCatProbe=0;
 newInteraction();returnToPatrol=true;startCat=false;pendingFirstTouch=-1;memset(touch,0,sizeof(touch));scanDirection=1;scanUsed=0;scanBudget=71;lastDecisionFrame=arrivalFrame=0;remainingCm=0;alignSteps=0;
 sideAt[0]=sideAt[1]=tick;side[0]=side[1]=20;safe[0]=safe[1]=1;encoderAt[0]=encoderAt[1]=tick;encoder[0]=encoder[1]=0;encoderRpm[0]=encoderRpm[1]=0;
 observation.clear();observation["source"]="SIM_CAMERA";observation["at"]=tick;observation["frameSeq"]=1;observation["cat"]["valid"]=true;observation["cat"]["detected"]=true;observation["cat"]["centerX"]=.5;observation["cat"]["width"]=.2;observation["tagsValid"]=true;
}
void stopped(){CHECK(stopId!=0);auto id=stopId;policyInput("DONE "+String(id)+" MOTOR_STOPPED");CHECK(stopId==0);}
void md(unsigned l=1,unsigned r=1){auto id=motorId;CHECK(id);policyInput("MD "+String(id)+" "+String(l)+" "+String(r));}
void configDone(){CHECK(sessionId);policyInput("DONE "+String(sessionId)+" SCAN_CONFIGURED");CHECK(scanId);}
void frame(bool detected=true,bool valid=true,unsigned n=2){
 observation["at"]=tick;observation["frameSeq"]=n;observation["generation"]=generation;observation["scanId"]=scanId;observation["stepId"]=scanId?scanStep:checkStep;observation["cat"]["detected"]=detected;observation["cat"]["valid"]=valid;policyVision();
}
void settled(){if(action=="CAT_CONFIRM"){tick+=250;encoderAt[0]=encoderAt[1]=tick;policyTick();tick+=250;encoderAt[0]=encoderAt[1]=tick;policyTick();CHECK(due);tick=due;policyTick();CHECK(action=="CAT_CONFIRM"&&checkPending);return;}policyTick();CHECK(action=="SETTLE");tick+=250;encoderAt[0]=encoderAt[1]=tick;policyTick();CHECK(action=="SETTLE");tick+=150;encoderAt[0]=encoderAt[1]=tick;policyTick();CHECK(action=="CAT_CHECK");}
void scanRequest(){policyInput("VQ "+String(scanId)+" 1 "+String(generation)+" 2");CHECK(requests.back().mode=="CAT");CHECK(!motorRunning);}
void completeConfirmation(bool detected=true){
 stopped();CHECK(action=="CAT_CONFIRM");auto from=commands.size();auto requestCount=requests.size();
 tick+=250;encoderAt[0]=encoderAt[1]=tick;policyTick();CHECK(!due);
 tick+=250;encoderAt[0]=encoderAt[1]=tick;policyTick();CHECK(due);CHECK(requests.size()==requestCount);
 CHECK(policyVisionMode()=="CAT");auto deadline=due;tick=deadline-1;policyTick();CHECK(requests.size()==requestCount);
 tick=deadline;sideAt[0]=sideAt[1]=tick;policyTick();CHECK(action=="CAT_CONFIRM");CHECK(checkPending);CHECK(requests.size()==requestCount+1);
 for(size_t i=from;i<commands.size();i++)CHECK(!commands[i].text.startsWith("ROTATE ")&&!commands[i].text.startsWith("DRIVE ")&&!commands[i].text.startsWith("SR ")&&!commands[i].text.startsWith("MOTOR "));
 frame(detected,true,observation["frameSeq"].as<unsigned>()+1);policyTick();
}
void touchFound(unsigned pick){
 policyTick();CHECK(action=="WAIT_SCAN_CONFIG");CHECK(scanNext=="CHOOSE_TOUCH");configDone();scanRequest();frame(true);observation["cat"]["centerX"]=.85;
 policyInput("SD "+String(scanId)+" "+String(generation)+" 1");CHECK(action=="WAIT_STOP");draws={pick,0};completeConfirmation();
}
int main(){
 // Observation always detects cats: a valid negative frame ends immediately.
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";behavior("WAIT_CHASE");observation["cat"]["detected"]=false;policyTick();CHECK(phase=="ENDING");CHECK(interactionExitReason=="cat left observation frame");
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";behavior("WAIT_CHASE");observation["cat"]["valid"]=false;policyTick();CHECK(action=="WAIT_CHASE");tick+=3000;policyTick();CHECK(interactionExitReason=="cat observation unavailable");
 // Touch still starts escape if visual input is temporarily unavailable.
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";behavior("WAIT_CHASE");tick+=1600;onTouch(0);policyTick();CHECK(action=="WAIT_RELEASE");
 // Exploration inactivity searches/centers; new interaction resumes exploration.
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";branch=0;hold("OBSERVE",60000);tick+=12000;observation["at"]=tick;policyTick();CHECK(action=="WAIT_STOP");CHECK(afterFace=="CHASE_ALIGN");
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";branch=0;behavior("WAIT_CHASE");onTouch(0);policyTick();CHECK(action=="WAIT_CHASE");CHECK(responseActive);CHECK(policyVisionMode()=="CAT");
 // Prey finishes by searching again rather than returning directly to patrol.
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";prey(false);tick=due;observation["at"]=tick;policyTick();CHECK(action=="WAIT_STOP");CHECK(afterFace=="CHASE_ALIGN");CHECK(phase=="INTERACTION");
 // After escape, an off-center visible cat stops scanning; no centering turns.
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";beginScan("CAT","CHASE_ALIGN");configDone();scanRequest();frame(true);observation["cat"]["centerX"]=.8;
 policyInput("SD "+String(scanId)+" "+String(generation)+" 1");CHECK(action=="WAIT_STOP");CHECK(afterStop=="CAT_CONFIRM");CHECK(afterFace=="WAIT_CHASE");
 auto beforeConfirmation=commands.size();completeConfirmation();CHECK(action=="WAIT_CHASE");CHECK(policyVisionMode()=="CAT");
 for(size_t i=beforeConfirmation;i<commands.size();++i){CHECK(!commands[i].text.startsWith("ROTATE "));CHECK(!commands[i].text.startsWith("DRIVE "));CHECK(!commands[i].text.startsWith("SR "));}
 // A touch starts the next escape immediately after release, without OBSERVE/FOLLOW_WAIT.
 onTouch(0);policyTick();CHECK(action=="WAIT_RELEASE");CHECK(touchNext=="ESCAPE");touch[0]=false;sideAt[0]=sideAt[1]=tick;policyTick();CHECK(action=="ESCAPE_DIRECTION");policyTick();CHECK(action=="ESCAPE_TURN");
 // Visual far-to-near chase starts the same next-round path; timeout exits.
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";observation["cat"]["width"]=.2;behavior("WAIT_CHASE");observation["cat"]["width"]=.4;policyTick();CHECK(action=="WAIT_RELEASE");
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";behavior("WAIT_CHASE");tick=due;observation["at"]=tick;policyTick();CHECK(phase=="ENDING");CHECK(interactionExitReason=="chase wait timeout");
 // Each round chooses a target within the configured interval, without margin subtraction.
 fixture();config["escapeMinDistanceCm"]=30;config["escapeMaxDistanceCm"]=80;
 draws={0};chooseEscapeDistance();CHECK(escapeTargetCm==30);CHECK(segmentCm==30);
 draws={UINT32_MAX};chooseEscapeDistance();CHECK(escapeTargetCm==80);
 draws={UINT32_MAX/2};chooseEscapeDistance();CHECK(fabs(escapeTargetCm-55)<.01);
 config["escapeMaxDistanceCm"]=30;draws={99};chooseEscapeDistance();CHECK(escapeTargetCm==30);CHECK(draws.size()==1);
 fixture();phase="INTERACTION";state("ESCAPE_DRIVE");config["escapeMinDistanceCm"]=50;config["escapeMaxDistanceCm"]=50;policyTick();CHECK(commands.back().text.startsWith("DRIVE 50.00 "));CHECK(action=="ESCAPE_MOVE");
 // Contact resumes the remaining distance without another random draw.
 initiator="CAT_TOUCH";pulsesL=pulsesR=10;draws={123};pauseForTouch();stopped();CHECK(action=="WAIT_TOUCH_RESUME");policyTick();CHECK(action=="ESCAPE_MOVE");CHECK(escapeTargetCm==50);CHECK(driveCm<50&&driveCm>0);CHECK(draws.size()==1);
 // Replay log 23 through the actual compact decoder and policy parser.
 fixture();sideAt[0]=sideAt[1]=0;
 CompactTelemetry batch;
 CHECK(batch.add("TB 458 0 01005C030000FF3F000000000000000000",tick));
 CHECK(batch.add("TB 458 1 0053010000300100000000000000000000",tick));
 CHECK(batch.add("TB 458 2 74007400F6FFF6FF00000F000000F6FF",tick));
 CHECK(batch.finish("DONE 458 TELEMETRY",tick,[](const String &row){policyInput(row);}));
 CHECK(sideAt[0]==tick&&sideAt[1]==tick);CHECK(fabs(side[0]-11.6f)<.01f);CHECK(side[1]==-1);
 phase="INTERACTION";initiator="CAT_TOUCH";config["catConfirmPreyPercent"]=0;config["catConfirmExplorePercent"]=0;
 for(unsigned pick:{0u,50u,99u}){draws={pick,0};chooseConfirmedTouch();CHECK(branch==1);CHECK(action=="ESCAPE_DIRECTION");}
 policyTick();CHECK(action=="ESCAPE_TURN");CHECK(commands.back().text.startsWith("ROTATE "));CHECK(interactionExitReason=="");
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";config["catConfirmPreyPercent"]=0;config["catConfirmExplorePercent"]=0;
 sideAt[0]=sideAt[1]=0;policyInput("DATA 458 HW SL 1 1 0 11.6 11.6 P");policyInput("DATA 458 HW SR 1 1 0 20.0 20.0 P");
 chooseConfirmedTouch();policyTick();CHECK(action=="ESCAPE_TURN");CHECK(commands.back().text.startsWith("ROTATE "));
 md();stopped();policyTick();CHECK(action=="ESCAPE_MOVE");CHECK(commands.back().text.startsWith("DRIVE "));
 // Malformed rows cannot refresh data or create a top-button press.
 fixture();sideAt[0]=sideAt[1]=0;encoderAt[0]=0;
 for(const char *row:{"DATA 1 HW SL 1 1 0 20 20 P extra","DATA 1 HW SL 1 1 0 20 nan P","DATA 1 HW SL 1 1 0 20 500 P","DATA 1 HW EL 1 1 23 nan P","DATA 1 HW EL 1 1 23 0 P extra","DATA 1 HW T UNKNOWN 1 1 1 P","DATA 1 HW T TOP 1 1 2 P","DATA 1 HW T TOP 1 1 1 P extra"})policyInput(row);
 CHECK(sideAt[0]==0);CHECK(encoderAt[0]==0);CHECK(!touching());CHECK(phase=="PATROL");
 fixture();rotate(20,52,"ALIGN_CAT");auto protocolMotor=motorId;
 for(const char *verb:{"AK ","ACK ","MS ","MP ","MD "}){String row=String(verb)+String(protocolMotor);if(strcmp(verb,"AK ")&&strcmp(verb,"ACK "))row+=" 1 1";policyInput(row+" extra");}
 CHECK(!motorConfirmed);CHECK(motorId==protocolMotor);CHECK(action=="ALIGN_CAT");
 for(const char *waiting:{"WAIT_SESSION_START","WAIT_SESSION_STOP","WAIT_PATROL_GEOMETRY","WAIT_PATROL_CONFIG"}){
   fixture();state(waiting);sessionId=123;policyInput("DONE 123 WRONG_RESULT");CHECK(sessionId==123);CHECK(action==waiting);
 }
 // Independent search opens a session directly; a tag route is not needed.
 for(const char *initial:{"IDLE","STOPPED"}){
  fixture();phase=initial;config["route"].to<JsonArray>().clear();geometryConfirmed=false;
  policyControl("alert");CHECK(phase=="STARTING");CHECK(startCat);CHECK(returnToPatrol);
  policyInput("DONE "+String(sessionId)+" MOTOR_STOPPED");
  policyInput("DONE "+String(sessionId)+" GEOMETRY_SET");
  policyInput("DATA "+String(sessionId)+" CONFIG WHEEL_MM=67.5 TRACK_MM=120.0 SLOTS_PER_REV=20 SAFE_LEFT_CM=1.0 SAFE_RIGHT_CM=1.0");
  policyInput("DONE "+String(sessionId)+" CONFIG");policyInput("DONE "+String(sessionId)+" SESSION_STARTED");
  CHECK(phase=="INTERACTION");CHECK(initiator=="ROBOT_SEARCH");CHECK(afterFace=="CHOOSE_ROBOT");
  finish();stopped();policyTick();policyInput("DONE "+String(sessionId)+" SESSION_STOPPED");CHECK(phase=="STOPPED");CHECK(action=="FAULT");CHECK(lastFault.startsWith("empty route:"));
 }
 returnToPatrol=true;
 // No PING/READY/capability event is needed to start the current firmware.
 for(const char *control:{"start","resume"}){
   fixture();mode="UART";phase="STOPPED";scanCapable=false;lastUnoAt=tick;
   policyControl(control);CHECK(phase=="STARTING");CHECK(action=="WAIT_PATROL_STOP");CHECK(!scanCapable);
   policyInput("DONE "+String(sessionId)+" MOTOR_STOPPED");CHECK(action=="WAIT_PATROL_CONFIG");
   policyInput("DATA "+String(sessionId)+" CONFIG WHEEL_MM=67.5 TRACK_MM=120.0 SLOTS_PER_REV=20 SAFE_LEFT_CM=1.0 SAFE_RIGHT_CM=1.0");
   policyInput("DONE "+String(sessionId)+" CONFIG");CHECK(action=="WAIT_SESSION_START");
   policyInput("DONE "+String(sessionId)+" SESSION_STARTED");CHECK(phase=="PATROL");
   policyTick();CHECK(action=="WAIT_SCAN_CONFIG");configDone();CHECK(commands.back().text.startsWith("SR "));CHECK(!scanCapable);
 }
 // Start retries retain id/generation/frozen PWM and only time actual dispatch.
 fixture();beginScan("TAG","APPROACH_TAG");configDone();auto startId=scanId;auto startText=commands.back().text;
 CHECK(policyPreparing());auto beforeStart=commands.size();tick+=1000;policyTick();CHECK(commands.size()==beforeStart); // not dispatched
 policyCommandSent(startId);tick+=99;policyTick();CHECK(commands.size()==beforeStart);
 tick++;policyTick();CHECK(scanStartRetries==1);CHECK(commands.back().id==startId);CHECK(commands.back().text==startText);CHECK(!motorDispatched);
 beforeStart=commands.size();policyInput("ERROR 0 BAD_ENVELOPE");CHECK(commands.size()==beforeStart); // already queued
 config["minimumDrivePwm"]=255;policyCommandSent(startId);policyInput("ERROR 0 BAD_CHECKSUM");CHECK(scanStartRetries==2);CHECK(commands.back().text==startText);CHECK(commands.back().id==startId);
 policyCommandSent(startId);policyInput("ERROR "+String(startId)+" BAD_ENVELOPE");CHECK(scanStartRetries==3);
 policyCommandSent(startId);tick+=100;policyTick();CHECK(phase=="STOPPED");CHECK(lastFault.indexOf("scan start failed after 3 retries")>=0);CHECK(commands.front().text=="STOP");
 fixture();beginScan("TAG","APPROACH_TAG");configDone();startId=scanId;policyCommandSent(startId);policyInput("AK "+String(startId));CHECK(motorConfirmed);CHECK(!policyPreparing());beforeStart=commands.size();tick+=101;policyTick();CHECK(commands.size()==beforeStart);
 fixture();beginScan("TAG","APPROACH_TAG");configDone();startId=scanId;policyCommandSent(startId);policyInput("MS "+String(startId)+" 0 0");CHECK(motorConfirmed);beforeStart=commands.size();tick+=100;policyTick();CHECK(commands.size()==beforeStart);
 fixture();beginScan("TAG","APPROACH_TAG");configDone();policyCommandSent(scanId);policyInput("ERROR 0 BAD_CHECKSUM");policyControl("stop");beforeStart=commands.size();tick+=101;policyTick();CHECK(commands.size()==beforeStart);CHECK(!scanId);
 fixture();beginScan("TAG","APPROACH_TAG");configDone();policyCommandSent(scanId);policyInput("ERROR "+String(scanId)+" SESSION_OR_TOUCH_LOCK");CHECK(phase=="STOPPED");CHECK(scanStartRetries==0);
 fixture();beginScan("TAG","APPROACH_TAG");configDone();policyCommandSent(scanId);beforeStart=commands.size();policyInput("ERROR "+String(scanId+1)+" BAD_CHECKSUM");CHECK(commands.size()==beforeStart);
 fixture();beginScan("TAG","APPROACH_TAG");configDone();policyCommandSent(scanId);policyInput("ERROR 0 BAD_CHECKSUM"); // next scan resets the retry counter
 beginScan("TAG","APPROACH_TAG");configDone();CHECK(scanStartRetries==0);
 // Reproduce a corrupted SC after route wrap: only SC is retried, never motion.
 fixture();routeIndex=1;state("TAG_ARRIVAL");policyTick();CHECK(routeIndex==0);policyTick();
 CHECK(action=="WAIT_SCAN_CONFIG");CHECK(policyPreparing());auto originalConfig=sessionId;auto originalCommand=pendingCommand;
 policyInput("ERROR 0 BAD_CHECKSUM");CHECK(scanConfigRetryPending);CHECK(sessionRetries==0);CHECK(!motorId);
 auto queued=commands.size();tick+=99;policyTick();CHECK(commands.size()==queued);
 policyInput("ERROR 0 BAD_CHECKSUM");tick++;policyTick();CHECK(sessionRetries==1);CHECK(commands.size()==queued+1);CHECK(commands.back().text==originalCommand);CHECK(sessionId!=originalConfig);
 policyInput("DONE "+String(originalConfig)+" SCAN_CONFIGURED");CHECK(!scanId);
 policyInput("DONE "+String(sessionId)+" ACCEPTED");CHECK(!scanId);CHECK(sessionId!=0);
 policyInput("ERROR "+String(originalConfig)+" BAD_CHECKSUM");CHECK(!scanConfigRetryPending);
 configDone();CHECK(action=="SCAN_TAG_ACTIVE");CHECK(policyPreparing());CHECK(!scanConfigRetryPending);
 policyInput("AK "+String(scanId));CHECK(!policyPreparing());
 // Three retransmissions in addition to the initial send; failure sends STOP.
 fixture();beginScan("TAG","APPROACH_TAG");
 for(int attempt=1;attempt<=3;attempt++){auto previous=sessionId;policyInput("ERROR 0 BAD_ENVELOPE");tick+=100;policyTick();CHECK(sessionRetries==attempt);CHECK(sessionId!=previous);CHECK(!motorId);CHECK(commands.back().text.startsWith("SC "));}
 policyInput("ERROR 0 BAD_CHECKSUM");CHECK(phase=="STOPPED");CHECK(lastFault.indexOf("3 retries")>=0);CHECK(lastFault.indexOf("BAD_CHECKSUM")>=0);CHECK(!scanId);CHECK(commands.front().text=="STOP");
 queued=commands.size();tick+=10000;policyTick();CHECK(commands.size()==queued);
 // No response waits for 2500ms before scheduling the 100ms retry gap.
 fixture();beginScan("TAG","APPROACH_TAG");queued=commands.size();tick+=2499;policyTick();CHECK(!scanConfigRetryPending);CHECK(commands.size()==queued);tick++;policyTick();CHECK(scanConfigRetryPending);CHECK(commands.size()==queued);tick+=100;policyTick();CHECK(sessionRetries==1);
 // A valid success arriving in the gap cancels retry; operator stop does too.
 fixture();beginScan("TAG","APPROACH_TAG");policyInput("ERROR 0 BAD_CHECKSUM");configDone();queued=commands.size();tick+=100;policyTick();CHECK(commands.size()==queued);
 fixture();beginScan("TAG","APPROACH_TAG");policyInput("ERROR 0 BAD_CHECKSUM");policyControl("stop");queued=commands.size();tick+=100;policyTick();CHECK(commands.size()==queued);CHECK(!scanConfigRetryPending);
 fixture();beginScan("TAG","APPROACH_TAG");policyInput("ERROR "+String(sessionId)+" INVALID_ARGUMENT");CHECK(phase=="STOPPED");CHECK(!scanConfigRetryPending);
 fixture();policyInput("ERROR 0 BAD_CHECKSUM");CHECK(!scanConfigRetryPending);CHECK(phase=="PATROL");
 // Retry accounting starts afresh for each scan and works over millis wrap.
 fixture();tick=UINT32_MAX-50;beginScan("TAG","APPROACH_TAG");policyInput("ERROR 0 BAD_CHECKSUM");tick=48;policyTick();CHECK(sessionRetries==0);tick=49;policyTick();CHECK(sessionRetries==1);configDone();beginScan("TAG","APPROACH_TAG");CHECK(sessionRetries==0);
 fixture();beginScan("TAG","APPROACH_TAG");mode="UART";
 for(int attempt=1;attempt<=3;attempt++){tick+=2500;lastUnoAt=tick;policyTick();CHECK(scanConfigRetryPending);tick+=100;lastUnoAt=tick;policyTick();CHECK(sessionRetries==attempt);CHECK(phase=="PATROL");}
 tick+=2500;lastUnoAt=tick;policyTick();CHECK(phase=="STOPPED");CHECK(lastFault.indexOf("confirmation timeout")>=0);
 fixture();beginScan("TAG","APPROACH_TAG");mode="UART";lastUnoAt=tick;tick+=2500;policyTick();CHECK(linkRecovering);CHECK(action=="LINK_RECOVERY_STOP");CHECK(!scanConfigRetryPending);
 fixture();move(30,30);policyInput("ERROR "+String(motorId)+" BAD_CHECKSUM");CHECK(!scanConfigRetryPending);CHECK(phase=="STOPPED");
 fixture();pulsesL=pulsesR=1;CHECK(fabs(angle()-10.125)<.001);pulsesL=2;pulsesR=0;CHECK(fabs(angle()-10.125)<.001);
 fixture();observation["tags"][0]["id"]=9;observation["tags"][0]["width"]=.3;observation["tags"][0]["centerX"]=.9;policyTick();CHECK(action=="WAIT_STOP");CHECK(routeIndex==0);auto id=stopId;policyInput("DONE 0 MOTOR_STOPPED");CHECK(stopId==id);policyInput("DONE "+String(id)+" ACCEPTED");CHECK(routeIndex==0);stopped();policyTick();CHECK(routeIndex==1);CHECK(action=="SCAN_TAG");
 fixture();config["route"][1]=9;observation["tags"][0]["id"]=9;observation["tags"][0]["width"]=.3;policyTick();stopped();policyTick();CHECK(action=="TAG_REARM");observation["frameSeq"]=2;policyTick();CHECK(action=="TAG_REARM");observation["frameSeq"]=3;observation["tags"][0]["width"]=.1;policyTick();CHECK(action=="SCAN_TAG");
 fixture();phase="INTERACTION";search("OBSERVE");stopped();policyTick();CHECK(action=="WAIT_SCAN_CONFIG");configDone();CHECK(scanBudget==71);CHECK(commands.back().text.startsWith("SR "));scanRequest();tick+=1000;policyTick();CHECK(phase=="INTERACTION");frame(false);CHECK(commands.back().text.startsWith("VR "));auto size=commands.size();policyVision();CHECK(commands.size()==size);auto sid=scanId;auto gen=generation;policyInput("SD 0 "+String(gen)+" 1");CHECK(scanId==sid);policyInput("SD "+String(sid)+" "+String(gen-1)+" 1");CHECK(scanId==sid);frame(true,true,3);policyInput("SD "+String(sid)+" "+String(gen)+" 1");completeConfirmation();CHECK(action=="OBSERVE");
 fixture();phase="INTERACTION";beginScan("CAT","OBSERVE");configDone();scanUsed=71;sid=scanId;gen=generation;policyInput("SD "+String(sid)+" "+String(gen)+" 0");CHECK(catFailure=="找猫失败");CHECK(phase=="ENDING");CHECK(action=="WAIT_STOP");stopped();policyTick();CHECK(action=="WAIT_SESSION_STOP");policyInput("DONE "+String(sessionId)+" SESSION_STOPPED");CHECK(action=="WAIT_SESSION_START");policyInput("DONE "+String(sessionId)+" SESSION_STARTED");CHECK(phase=="PATROL");CHECK(routeIndex==0);CHECK(catFailure=="找猫失败");JsonDocument d;policySnapshot(d);CHECK(d["patrolStatus"]["catFailure"]["angle"].as<float>()>700);
 // CAT-07A..F: stopped recognition, loss/reacquisition, remaining distance and STOP.
 fixture();phase="INTERACTION";behavior("SLOW_APPROACH");CHECK(remainingCm==6);policyTick();CHECK(action=="CAT_DRIVE");CHECK(policyVisionMode()=="OFF");auto original=motorId;md();CHECK(remainingCm<6&&remainingCm>4);CHECK(action=="WAIT_STOP");stopped();settled();frame(false);policyTick();CHECK(action=="WAIT_SCAN_CONFIG");auto remaining=remainingCm;configDone();scanRequest();frame(true,true,4);policyInput("SD "+String(scanId)+" "+String(generation)+" 1");CHECK(action=="WAIT_STOP");completeConfirmation();CHECK(action=="APPROACH_CAT");CHECK(remainingCm==remaining);policyTick();CHECK(action=="CAT_DRIVE");policyInput("MD "+String(original)+" 20 20");CHECK(action=="CAT_DRIVE");CHECK(remainingCm==remaining);policyControl("stop");frame(true,true,5);policyTick();CHECK(phase=="STOPPED");CHECK(!motorId);
 fixture();phase="INTERACTION";behavior("SLOW_APPROACH");policyTick();md();stopped();settled();frame(false,false);policyTick();CHECK(phase=="STOPPED");CHECK(lastFault=="cat check invalid cat frame");
 fixture();phase="INTERACTION";behavior("SLOW_APPROACH");policyTick();CHECK(!checkPending);config["frontAvoidanceEnabled"]=true;CHECK(policyVisionMode()=="FLOOR");policyControl("stop");auto count=commands.size();policyStopUnconfirmed();CHECK(commands.empty());policyTick();CHECK(commands.empty());CHECK(!motorId);
 // Touch branch pools, first touch, held touches, top/multiple and side boundaries.
 fixture();draws={99,99,50};onTouch(3);CHECK(firstTouch==3);CHECK(rounds==1);onTouch(0);CHECK(firstTouch==3);CHECK(rounds==1);stopped();touch[3]=touch[0]=false;policyTick();policyTick();CHECK(action=="WAIT_SCAN_CONFIG");CHECK(scanFlags==0);
 fixture();draws={99,99,50};onTouch(0);stopped();touch[0]=false;side[0]=side[1]=.5;policyTick();policyTick();CHECK(scanFlags==1);configDone();CHECK(commands.back().text.endsWith(" 1"));CHECK(commands.back().text.indexOf("DRIVE")<0);
 fixture();draws={99};onTouch(0);stopped();touch[0]=false;side[1]=.5;touchFound(99);policyTick();CHECK(phase=="ENDING");
 fixture();draws={99};onTouch(0);stopped();touch[0]=false;side[1]=1;touchFound(99);CHECK(action=="ESCAPE_DIRECTION");policyTick();CHECK(action=="ESCAPE_TURN");CHECK(turnDeg==180);md(18,18);stopped();policyTick();CHECK(action=="ESCAPE_MOVE");
 fixture();draws={99};onTouch(0);stopped();touch[0]=false;touchFound(99);sideAt[0]=sideAt[1]=tick-3000;policyTick();CHECK(phase=="ENDING");
 fixture();draws={0};onTouch(0);stopped();CHECK(action=="WAIT_RELEASE");CHECK(touchSignalUntil>tick);touch[0]=false;touchFound(25);CHECK(action=="SIGNAL");CHECK(branch==0);CHECK(lastPattern==0);tick=due;observation["at"]=tick;policyTick();CHECK(action=="OBSERVE");
 // CAT-01..18 retained behavior choices and local completion, no camera-only trigger.
 fixture();policyTick();CHECK(phase=="PATROL");CHECK(initiator=="");policyControl("alert");CHECK(initiator=="ROBOT_SEARCH");CHECK(action=="WAIT_STOP");
 fixture();phase="INTERACTION";observation["cat"]["width"]=.1;draws={0};chooseRobot();CHECK(afterFace=="PROBE_APPROACH");CHECK(lastRobotProbe==tick);stopped();settled();frame();policyTick();CHECK(action=="PROBE_DRIVE");policyTick();CHECK(action=="PROBE_MOVE");md(3,3);stopped();settled();frame();policyTick();CHECK(action=="PROBE_PAUSE");CHECK(probeStep==1);tick=due;observation["at"]=tick;policyTick();CHECK(probeStep==2);CHECK(segmentCm==4);policyTick();md(4,4);stopped();settled();frame(true,true,5);policyTick();tick=due;observation["at"]=tick;policyTick();CHECK(probeStep==3);CHECK(segmentCm==3);policyTick();md(3,3);stopped();settled();frame(true,true,6);policyTick();tick=due;policyTick();CHECK(action=="WAIT_STOP");
 for(int pick:{0,25,80}){fixture();phase="INTERACTION";observation["cat"]["width"]=.5;draws={(unsigned)pick};chooseRobot();CHECK(afterFace==(pick==0?"STAY_SIGNAL":pick==25?"OBSERVE":"STAY"));}
 fixture();phase="INTERACTION";lastRobotProbe=tick;observation["cat"]["width"]=.1;draws={0};chooseRobot();CHECK(afterFace=="FAST_APPROACH");
 fixture();phase="INTERACTION";hold("OBSERVE",100);tick+=100;policyTick();CHECK(action=="FOLLOW_WAIT");
 fixture();phase="INTERACTION";motorId=42;motorRunning=motorDispatched=true;feedbackAt=tick-751;policyTick();CHECK(phase=="STOPPED");
 fixture();phase="INTERACTION";motorId=42;motorDispatched=motorConfirmed=true;motorRunning=false;feedbackAt=tick-1000;scanId=42;scanAt=tick;policyTick();CHECK(phase=="INTERACTION");
 fixture();onTouch(0);auto blockedGen=generation;policyControl("stop");policyInput("VQ 42 1 "+String(blockedGen));CHECK(phase=="STOPPED");CHECK(!scanId);
 // Full paired two-turn search: actual Uno core -> ESP -> actual frame gate -> Uno.
 fixture();phase="INTERACTION";remainingCm=5;beginScan("CAT","RESUME_APPROACH");configDone();
 ScanMachine robot;CHECK(robot.begin(scanId,generation,2,75,scanBudget,0,tick));
 uint32_t raw=0,frames=10;VisionGate gate;bool secondTurn=false;
 while(robot.active()){
   auto e=robot.tick(tick,raw,raw);
   if(e==ScanMachine::START_STEP){
     policyInput("MS "+String(scanId)+" "+String(raw)+" "+String(raw));
     robot.motionStarted(raw,raw);raw+=robot.stepPulses();tick+=200;
     policyInput("MD "+String(scanId)+" "+String(robot.stepPulses())+" "+String(robot.stepPulses()));
     robot.motionStopped(tick,raw,raw);tick+=250;
   }else if(e==ScanMachine::REQUEST){
     policyInput("VQ "+String(scanId)+" "+String(robot.step)+" "+String(generation)+" "+String(robot.used));
     CHECK(gate.request(scanId,robot.step,generation,"CAT",frames,uint64_t(tick)*1000));auto t=gate.ticket;
     CHECK(!gate.finish(t,frames+1,uint64_t(tick)*1000-1));tick+=50;frames++;
     CHECK(gate.finish(t,frames,uint64_t(tick)*1000));
     bool found=raw>=40;secondTurn|=found;frame(found,true,frames);
     unsigned rid,st,g,rval,fr;CHECK(sscanf(commands.back().text.c_str(),"VR %u %u %u %u %u",&rid,&st,&g,&rval,&fr)==5);
     CHECK(robot.reply(rid,st,g,rval,fr,tick));
     if(raw<36)CHECK(phase=="INTERACTION"&&scanId);
   }else tick+=10;
 }
 CHECK(secondTurn);CHECK(robot.result==ScanMachine::FOUND);CHECK(raw>35&&raw<71);
 policyInput("SD "+String(scanId)+" "+String(generation)+" 1");
 completeConfirmation();CHECK(action=="APPROACH_CAT");CHECK(remainingCm==5);
 fixture();phase="INTERACTION";beginScan("CAT","OBSERVE");configDone();scanUsed=20;side[0]=side[1]=.5;
 policyInput("MA "+String(scanId)+" SIDE_LEFT_OBSTACLE 1 1");CHECK(action=="WAIT_STOP");CHECK(scanUsed==21);
 stopped();policyTick();CHECK(scanFlags==1);CHECK(scanUsed==21);configDone();CHECK(commands.back().text.endsWith(" 50 1"));CHECK(scanBudget==71);
 fixture();phase="INTERACTION";beginScan("CAT","OBSERVE");configDone();
 auto active=scanId;policyInput("VQ "+String(active)+" 0 "+String(generation));CHECK(requests.empty());
 policyInput("VQ "+String(active)+" 1 "+String(generation)+" EXTRA");CHECK(requests.empty());
 policyInput("SD "+String(active)+" "+String(generation)+" 1 EXTRA");CHECK(scanId==active);
 policyInput("SD "+String(active)+" "+String(generation)+" 99");CHECK(scanId==active);
 scanRequest();frame(false,false);CHECK(commands.back().text.indexOf(" 2 ")>=0);auto cached=commands.back().text;
 policyInput("VQ "+String(active)+" 1 "+String(generation)+" 2");CHECK(commands.back().text==cached);
 fixture();stopGate=false;policyControl("start");CHECK(phase=="PATROL");CHECK(!sessionId);
 fixture();phase="INTERACTION";for(int i=0;i<30;i++){draws={0};signal(true);CHECK(lastPattern==i%15);CHECK(responseLight()||responseSound());CHECK(due-tick==uint32_t(max(lightLengths[responseLight()],soundLengths[responseSound()])));}
 // Each independent light/sound form reaches actual GPIO/commands; no 0/0.
 for(int combo=1;combo<=15;combo++){
   fixture();phase="INTERACTION";initiator="CAT_TOUCH";branch=0;mode="UART";lastUnoAt=tick;
   patterns=1<<(combo-1);draws={0};signal(true);CHECK(lastPattern==combo-1);
   int light=combo/4,sound=combo%4;CHECK(hostLedMode==3);CHECK(hostLedOn==(light!=0));
   std::vector<String> beeps;for(auto &c:commands)if(c.text.startsWith("BEEP ")&&c.text!="BEEP 0")beeps.push_back(c.text);
   tick+=400;responseTick();CHECK(hostLedOn==(light==1||light==3));hostLedAccept("EVENT HOST_LED OFF");CHECK(hostLedMode==3);
   tick+=200;responseTick();tick+=200;responseTick();tick+=400;responseTick();
   beeps.clear();for(auto &c:commands)if(c.text.startsWith("BEEP ")&&c.text!="BEEP 0")beeps.push_back(c.text);
   CHECK(beeps.size()==responseCounts[sound]);
   policyControl("stop");auto count=commands.size();tick+=5000;policyTick();CHECK(!responseActive);CHECK(!hostLedOn);CHECK(commands.size()==count);
 }
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";branch=0;signal(true);auto first=lastPattern;auto interaction=interactionId;
 onTouch(0);CHECK(lastPattern!=first);CHECK(interactionId==interaction);CHECK(branch==0);auto responseCommandCount=commands.size();onTouch(0);CHECK(commands.size()==responseCommandCount);
 touch[0]=false;onTouch(0);CHECK(commands.size()>responseCommandCount);touch[0]=false;tick=due;observation["at"]=tick;policyTick();CHECK(action=="OBSERVE");
 tick=due;observation["at"]=tick;policyTick();CHECK(action=="FOLLOW_WAIT");tick=due;observation["at"]=tick;responseCommandCount=commands.size();policyTick();CHECK(action=="FOLLOW_WAIT");CHECK(commands.size()==responseCommandCount);
 fixture();mode="SIM_UNO";lastUnoAt=0;hostLedManual(true);hostLedTick();CHECK(hostLedOn);CHECK(ledLevel==LOW);hostLedAccept("EVENT HOST_LED OFF");CHECK(hostLedOn);hostLedManual(false);CHECK(!hostLedOn);CHECK(ledLevel==HIGH);hostLedManual(true);hostLedStop();CHECK(!hostLedOn);
 // Responses run during scan rotation, vision wait, config wait and confirmation.
 for(const char *stage:{"WAIT_SCAN_CONFIG","SEARCH_CAT_ACTIVE","CAT_CONFIRM","CAT_CHECK","WAIT_STOP"}){
   fixture();phase="INTERACTION";initiator="CAT_TOUCH";branch=0;state(stage);due=tick+5000;auto deadline=due;auto gen=generation;auto identity=interactionId;
   if(String(stage)=="SEARCH_CAT_ACTIVE"){scanId=motorId=101;motorConfirmed=true;scanWaiting=true;scanBudget=71;scanUsed=7;}
   if(String(stage)=="WAIT_SCAN_CONFIG"||String(stage)=="SEARCH_CAT_ACTIVE")stopGate=false;
   onTouch(0);CHECK(responseActive);CHECK(interactionId==identity);CHECK(branch==0);
   if(String(stage)=="WAIT_SCAN_CONFIG"||String(stage)=="SEARCH_CAT_ACTIVE"){
     CHECK(action=="WAIT_STOP");CHECK(touchPaused);CHECK(pausedAction==stage);CHECK(afterStop=="WAIT_TOUCH_RESUME");CHECK(scanUsed==7||String(stage)=="WAIT_SCAN_CONFIG");
     stopGate=false;tick+=100;responseTick();CHECK(responseActive);CHECK(responsePulse==0);stopGate=true;stopped();CHECK(action=="WAIT_TOUCH_RESUME");
     tick+=100;policyTick();CHECK(responseActive);CHECK(action=="WAIT_TOUCH_RESUME");
     touch[0]=false;policyTick();CHECK(action=="WAIT_SCAN_CONFIG");CHECK(responseActive);CHECK(scanUsed==7||String(stage)=="WAIT_SCAN_CONFIG");
   }else{CHECK(action==stage);CHECK(generation==gen);CHECK(due==deadline);tick+=100;responseTick();CHECK(responseActive);}
   auto selected=lastPattern;touch[0]=false;onTouch(0);CHECK(lastPattern!=selected);CHECK(responseActive);
   policyControl("stop");CHECK(!responseActive);CHECK(!responseCompleted);
 }
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";branch=0;scanId=motorId=101;motorConfirmed=true;scanWaiting=false;scanUsed=7;scanBudget=71;pulsesL=pulsesR=2;state("SEARCH_CAT_ACTIVE");stopGate=false;
 onTouch(0);CHECK(touchPaused);CHECK(scanUsed==9);CHECK(responseActive);stopGate=true;stopped();touch[0]=false;policyTick();CHECK(action=="WAIT_SCAN_CONFIG");CHECK(scanUsed==9);CHECK(responseActive);
 // Completing the response does not interrupt scanning; prey is deferred.
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";branch=0;config["explorePreyPercent"]=100;config["explorePreyMinimumSeconds"]=0;
 scanId=motorId=101;motorConfirmed=true;scanAt=scanProgressAt=tick;state("SEARCH_CAT_ACTIVE");playExplorationResponse();tick+=3500;policyTick();CHECK(action=="SEARCH_CAT_ACTIVE");CHECK(responseCompleted);CHECK(exploreRounds==1);
 scanId=motorId=0;observation["at"]=tick;behavior("WAIT_CHASE");policyTick();CHECK(action=="PREY_WAIT");CHECK(!responseCompleted);
 // A stalled loop skips missed beeps instead of flooding UART.
 fixture();phase="INTERACTION";patterns=4;signal(true);commands.clear();tick+=2000;responseTick();CHECK(commands.empty());
 fixture();mode="UART";lastUnoAt=tick-2501;policyTick();CHECK(phase=="RECOVERING");CHECK(linkRecovering);
 fixture();phase="INTERACTION";motorId=42;policyInput("MA 0 ENCODER_STALL 1 1");CHECK(motorId==42);policyInput("MA 42 ENCODER_STALL 1 1");CHECK(phase=="STOPPED");CHECK(!motorId);CHECK(!leftPwm&&!rightPwm);
 fixture();config["escapePreyPercent"]=0;draws={99};onTouch(2);stopped();touch[2]=false;touchFound(99);policyTick();CHECK(action=="ESCAPE_TURN");md(18,18);stopped();policyTick();CHECK(action=="ESCAPE_MOVE");md(22,22);stopped();policyTick();CHECK(action=="WAIT_STOP");CHECK(afterFace=="CHASE_ALIGN");
 fixture();phase="INTERACTION";routeIndex=1;beginScan("CAT","OBSERVE");configDone();policyInput("SD "+String(scanId)+" "+String(generation)+" 0");stopped();policyTick();policyInput("DONE "+String(sessionId)+" SESSION_STOPPED");policyInput("DONE "+String(sessionId)+" SESSION_STARTED");CHECK(routeIndex==1);CHECK(phase=="PATROL");
 fixture();phase="INTERACTION";motorId=42;scanId=42;touch[1]=true;policyInput("READY UNO_CAT_V6.6");CHECK(phase=="STOPPED");CHECK(!scanId&&!motorId&&!touch[1]);CHECK(!scanCapable);
 for(float center:{.0f,.2f,.8f,1.f}){fixture();phase="INTERACTION";afterFace="OBSERVE";observation["cat"]["centerX"]=center;catLocated();CHECK(action=="OBSERVE");CHECK(!motorId);}
 fixture();observation["tags"][0]["id"]=9;observation["tags"][0]["width"]=.1;observation["tags"][0]["centerX"]=.8;policyTick();CHECK(action=="TAG_ALIGN");CHECK(leftPwm==30);md(2,2);stopped();observation["frameSeq"]=2;observation["tags"][0]["centerX"]=.5;policyTick();CHECK(leftPwm==95&&rightPwm==95);
 fixture();onTouch(3,1234);onTouch(0,1234);CHECK(firstTouch==3);CHECK(touchTie);CHECK(multiTouch);CHECK(rounds==1);
 fixture();phase="INTERACTION";beginScan("CAT","OBSERVE");configDone();policyInput("VQ "+String(scanId)+" 3 "+String(generation)+" 7");CHECK(scanUsed==7);policyInput("MP "+String(scanId)+" 2 2");CHECK(!motorRunning);policyInput("VQ "+String(scanId)+" 2 "+String(generation)+" 5");CHECK(scanUsed==7);
 fixture();config["minimumDrivePwm"]=140;move(30,-30);CHECK(commands.back().text=="MOTOR 140 -140");JsonDocument pwmState;policySnapshot(pwmState);CHECK(pwmState["patrolStatus"]["leftPwm"].as<int>()==140);CHECK(pwmState["patrolStatus"]["rightPwm"].as<int>()==-140);CHECK(pwmState["patrolStatus"]["requestedLeftPwm"].as<int>()==30);CHECK(pwmState["patrolStatus"]["requestedRightPwm"].as<int>()==-30);
 fixture();config["minimumDrivePwm"]=100;drive(1,95,"CAT_DRIVE");CHECK(commands.back().text=="DRIVE 1.00 100");policySnapshot(pwmState);CHECK(pwmState["patrolStatus"]["leftPwm"].as<int>()==100);
 fixture();config["minimumDrivePwm"]=140;beginScan("CAT","OBSERVE");configDone();CHECK(commands.back().text=="SR "+String(generation)+" 2 140 71 0");policyInput("MS "+String(scanId)+" 0 0");policySnapshot(pwmState);CHECK(pwmState["patrolStatus"]["leftPwm"].as<int>()==140);CHECK(pwmState["patrolStatus"]["requestedLeftPwm"].as<int>()==75);
 fixture();config["minimumDrivePwm"]=1;move(30,-30);CHECK(commands.back().text=="MOTOR 30 -30");
 // Log 14: accepted false, next rotation completed, all following VQ/SD lost.
 fixture();beginScan("CAT","OBSERVE");configDone();auto recoveryId=scanId,recoveryGen=generation;auto recoveryCommand=scanStartCommand;
 policyCommandSent(scanId);policyInput("MS "+String(scanId)+" 61 70");md(2,2);
 auto progress=scanProgressAt;tick+=7499;policyTick();CHECK(scanRecoveryRetries==0);
 tick++;policyTick();CHECK(scanRecoveryRetries==1);CHECK(commands.back().id==recoveryId);CHECK(commands.back().text==recoveryCommand);
 CHECK(generation==recoveryGen);CHECK(!motorRunning);CHECK(scanProgressAt==progress);
 policyInput("AK "+String(scanId));CHECK(scanProgressAt==progress);
 tick+=1000;policyTick();CHECK(scanRecoveryRetries==2);tick+=1000;policyTick();CHECK(scanRecoveryRetries==3);
 tick+=1000;policyTick();CHECK(phase=="STOPPED");CHECK(!scanId);CHECK(lastFault.indexOf("scan handoff timeout")>=0);
 // Recovery yields a new VQ; repeats of the same step cannot renew progress.
 fixture();beginScan("CAT","OBSERVE");configDone();policyInput("MS "+String(scanId)+" 0 0");md();tick+=7500;policyTick();CHECK(scanRecoveryRetries==1);
 scanRequest();CHECK(scanRecoveryRetries==0);frame(false);progress=scanProgressAt;
 tick+=7500;policyInput("VQ "+String(scanId)+" 1 "+String(generation)+" 2");CHECK(scanProgressAt==progress);policyTick();CHECK(scanRecoveryRetries==1);
 // A recovered terminal failure exits the scan; a recovered success advances.
 policyInput("SD "+String(scanId)+" "+String(generation)+" 3");CHECK(!scanId);CHECK(phase=="ENDING");
 fixture();phase="INTERACTION";beginScan("CAT","OBSERVE");configDone();scanRequest();frame(true);tick+=7500;policyTick();CHECK(scanRecoveryRetries==1);
 policyInput("SD "+String(scanId)+" "+String(generation)+" 1");CHECK(!scanId);CHECK(action!="SEARCH_CAT_ACTIVE");
 // Encoder RPM remains nonzero even between pulses: no camera during rotation.
 fixture();settle("CAT_LOCATED");tick+=250;encoderAt[0]=encoderAt[1]=tick;encoderRpm[0]=24;policyTick();
 tick+=250;encoderAt[0]=encoderAt[1]=tick;policyTick();CHECK(action=="SETTLE");CHECK(requests.empty());
 encoderRpm[0]=0;policyTick();CHECK(action=="CAT_CHECK");
 fixture();phase="INTERACTION";action="CAT_CONFIRM";afterFace="CHOOSE_TOUCH";due=tick+5000;encoderRpm[1]=30;policyTick();CHECK(action=="WAIT_STOP");CHECK(afterStop=="CAT_CONFIRM");CHECK(!due);CHECK(commands.back().text=="BEEP 0"||commands.back().text=="STOP");
 // A link gap cancels old motion, then requires checked stop/config/session.
 fixture();mode="UART";phase="INTERACTION";initiator="CAT_TOUCH";branch=1;
 motorId=123;motorRunning=true;lastUnoAt=tick-2500;policyTick();
 CHECK(linkRecovering);CHECK(action=="LINK_RECOVERY_STOP");CHECK(!motorId);CHECK(!scanId);
 CHECK(commands.back().text=="STOP");CHECK(linkRecoveryAttempts==1);
 auto recoveryStop=stopId;policyInput("DONE "+String(recoveryStop)+" TELEMETRY");CHECK(stopId==recoveryStop);
 stopped();policyTick();CHECK(action=="LINK_RECOVERY_SESSION_STOP");
 policyInput("DONE "+String(sessionId)+" SESSION_STOPPED");CHECK(action=="LINK_RECOVERY_CONFIG");
 policyInput("DATA "+String(sessionId)+" CONFIG WHEEL_MM=67.5 TRACK_MM=120.0 SLOTS_PER_REV=20 SAFE_LEFT_CM=1.0 SAFE_RIGHT_CM=1.0");CHECK(geometryConfirmed);policyInput("DONE "+String(sessionId)+" CONFIG");CHECK(action=="LINK_RECOVERY_SESSION_START");
 lastUnoAt=tick;policyInput("DONE "+String(sessionId)+" SESSION_STARTED");CHECK(!linkRecovering);
 CHECK(phase=="INTERACTION");CHECK(action=="WAIT_STOP");CHECK(afterFace=="CHASE_ALIGN");CHECK(branch==1);
 // Diagnostics alone never release recovery or authorize motion. Bounded failure.
 fixture();mode="UART";lastUnoAt=tick-2500;policyTick();
 policyInput("EVENT DIAG M 507 507 0 0 0 0");CHECK(action=="LINK_RECOVERY_STOP");
 policyStopUnconfirmed();CHECK(linkRecoveryAttempts==2);CHECK(linkRecovering);
 policyStopUnconfirmed();CHECK(linkRecoveryAttempts==3);CHECK(linkRecovering);
 policyStopUnconfirmed();CHECK(linkRecoveryAttempts==4);CHECK(linkRecovering);
 policyStopUnconfirmed();CHECK(!linkRecovering);CHECK(phase=="STOPPED");CHECK(action=="FAULT");
 fixture();mode="UART";lastUnoAt=tick-2500;policyTick();policyControl("stop");CHECK(!linkRecovering);
 // A failed STOP after a scan fault recovers diagnostics, without restarting motion.
 fixture();mode="UART";fail("tag search failed result=3");policyStopUnconfirmed();CHECK(linkRecovering);CHECK(linkResumePhase=="STOPPED");
 stopped();policyTick();policyInput("DONE "+String(sessionId)+" SESSION_STOPPED");policyInput("DATA "+String(sessionId)+" CONFIG WHEEL_MM=67.5 TRACK_MM=120.0 SLOTS_PER_REV=20 SAFE_LEFT_CM=1.0 SAFE_RIGHT_CM=1.0");CHECK(geometryConfirmed);
 policyInput("DONE "+String(sessionId)+" CONFIG");CHECK(commands.back().text=="GET_CONFIG");
 CHECK(!linkRecovering);CHECK(phase=="STOPPED");CHECK(action=="FAULT");CHECK(lastFault=="tag search failed result=3");CHECK(!motorId);CHECK(!scanId);
 fixture();mode="UART";fail("Uno link recovery failed after 4 attempts");policyStopUnconfirmed();CHECK(!linkRecovering);CHECK(phase=="STOPPED");
 // Correct CONFIG is required, including on the final (fourth) recovery.
 fixture();mode="UART";phase="INTERACTION";initiator="CAT_TOUCH";branch=1;recoverLink();retryLink();retryLink();retryLink();CHECK(linkRecoveryAttempts==4);stopped();lastUnoAt=tick;policyTick();policyInput("DONE "+String(sessionId)+" SESSION_STOPPED");
 auto recoveryConfig=sessionId;
 for(const char *bad:{"CONFIG WHEEL_MM=67.5 TRACK_MM=120.0","CONFIG WHEEL_MM=nan TRACK_MM=120.0 SLOTS_PER_REV=20 SAFE_LEFT_CM=1.0 SAFE_RIGHT_CM=1.0","CONFIG WHEEL_MM=67.5 TRACK_MM=120.0 SLOTS_PER_REV=19 SAFE_LEFT_CM=1.0 SAFE_RIGHT_CM=1.0","CONFIG WHEEL_MM=67.5 TRACK_MM=120.0 SLOTS_PER_REV=20 SAFE_LEFT_CM=0.0 SAFE_RIGHT_CM=1.0","CONFIG WHEEL_MM=67.5 TRACK_MM=120.0 SLOTS_PER_REV=20 SAFE_LEFT_CM=1.0 SAFE_RIGHT_CM=1.0 EXTRA"}){policyInput("DATA "+String(recoveryConfig)+" "+bad);CHECK(!geometryConfirmed);}
 policyInput("DATA "+String(recoveryConfig+1)+" CONFIG WHEEL_MM=67.5 TRACK_MM=120.0 SLOTS_PER_REV=20 SAFE_LEFT_CM=1.0 SAFE_RIGHT_CM=1.0");CHECK(!geometryConfirmed);
 policyInput("DATA "+String(recoveryConfig)+" CONFIG WHEEL_MM=67.5 TRACK_MM=120.0 SLOTS_PER_REV=20 SAFE_LEFT_CM=1.0 SAFE_RIGHT_CM=1.0");CHECK(geometryConfirmed);policyInput("DONE "+String(recoveryConfig));CHECK(action=="LINK_RECOVERY_SESSION_START");policyInput("DONE "+String(sessionId)+" SESSION_STARTED");CHECK(!linkRecovering);CHECK(phase=="INTERACTION");CHECK(branch==1);
 // Legacy cat tolerance is ignored; tag tolerance still controls tag alignment.
 fixture();config["centerTolerancePercent"]=2;config["tagCenterTolerancePercent"]=15;observation["tags"][0]["id"]=9;observation["tags"][0]["width"]=.1;observation["tags"][0]["centerX"]=.6;policyTick();CHECK(action=="APPROACH_TAG");CHECK(leftPwm>0&&leftPwm==rightPwm);
 fixture();config["centerTolerancePercent"]=15;config["tagCenterTolerancePercent"]=2;observation["tags"][0]["id"]=9;observation["tags"][0]["width"]=.1;observation["tags"][0]["centerX"]=.6;policyTick();CHECK(action=="TAG_ALIGN");
 fixture();phase="INTERACTION";config["centerTolerancePercent"]=2;config["tagCenterTolerancePercent"]=15;afterFace="OBSERVE";observation["cat"]["centerX"]=.6;catLocated();CHECK(action=="OBSERVE");CHECK(!motorId);
 fixture();phase="INTERACTION";config["centerTolerancePercent"]=15;config["tagCenterTolerancePercent"]=2;afterFace="OBSERVE";observation["cat"]["centerX"]=.6;catLocated();CHECK(action=="OBSERVE");
 // Exposed 0/100 probabilities select existing branches, respecting cooldown.
 for(bool probe:{false,true}){fixture();phase="INTERACTION";observation["cat"]["width"]=.1;config["robotFarProbePercent"]=probe?100:0;config["robotFarApproachPercent"]=probe?0:100;draws={99};chooseRobot();CHECK(afterFace==(probe?"PROBE_APPROACH":"FAST_APPROACH"));}
 for(int choice:{0,1,2}){fixture();phase="INTERACTION";observation["cat"]["width"]=.5;config["robotNearSignalPercent"]=choice==0?100:0;config["robotNearObservePercent"]=choice==1?100:0;config["robotNearStayPercent"]=choice==2?100:0;draws={99};chooseRobot();CHECK(afterFace==(choice==0?"STAY_SIGNAL":choice==1?"OBSERVE":"STAY"));}
 printf("actual ESP policy core: %u checks passed\n",checks);
 return 0;
}
