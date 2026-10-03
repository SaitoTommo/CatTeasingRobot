#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <deque>
#include <cassert>
#include "../firmware/UnoCatController/scan_machine.h"
#include "../esp32-host/ESP32CatHost/vision_gate.h"
#include "../esp32-host/ESP32CatHost/pwm_floor.h"
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
void logLine(const char *,const String &){}
bool stopGate=true;bool stopReady(){return stopGate;}
void cancelVision(){}
void requestVision(uint32_t id,uint16_t step,uint32_t gen,const char *m){requests.push_back({id,step,gen,m});}
#include "../esp32-host/ESP32CatHost/policy_defaults.h"
#include "../esp32-host/ESP32CatHost/policy.cpp"
unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL line %d: %s action=%s phase=%s\n",__LINE__,#x,action.c_str(),phase.c_str());exit(1);}}while(0)
void fixture(){
 stopGate=true;tick=100000;draws.clear();phase="STOPPED";policyControl("stop");commands.clear();requests.clear();
 config.clear();policyDefaults(config);config["route"][0]=9;config["route"][1]=10;
 config["frontAvoidanceEnabled"]=false;config["visionMaxAgeMs"]=1500;config["tagStopWidth"]=.25;
 config["catNearWidth"]=.35;config["catFarWidth"]=.12;config["patrolSearchPwm"]=45;config["patrolAlignPwm"]=30;config["stopMarginCm"]=3;
 mode="SIM_UNO";phase="PATROL";action="APPROACH_TAG";actionAt=tick;sessionAt=tick;motorId=scanId=stopId=sessionId=0;
 scanCapable=geometryConfirmed=true;routeIndex=0;rounds=0;branch=-1;patterns=0;lastPattern=-1;probeStep=0;
 firstTouch=-1;multiTouch=needRearm=followPending=false;initiator="";lastTouch=0;lastRobotProbe=lastCatProbe=0;
 memset(touch,0,sizeof(touch));scanDirection=1;scanUsed=0;scanBudget=71;lastDecisionFrame=arrivalFrame=0;remainingCm=0;alignSteps=0;
 sideAt[0]=sideAt[1]=tick;side[0]=side[1]=20;safe[0]=safe[1]=1;encoderAt[0]=encoderAt[1]=tick;encoder[0]=encoder[1]=0;
 observation.clear();observation["source"]="SIM_CAMERA";observation["at"]=tick;observation["frameSeq"]=1;observation["cat"]["valid"]=true;observation["cat"]["detected"]=true;observation["cat"]["centerX"]=.5;observation["cat"]["width"]=.2;observation["tagsValid"]=true;
}
void stopped(){CHECK(stopId!=0);auto id=stopId;policyInput("DONE "+String(id)+" MOTOR_STOPPED");CHECK(stopId==0);}
void md(unsigned l=1,unsigned r=1){auto id=motorId;CHECK(id);policyInput("MD "+String(id)+" "+String(l)+" "+String(r));}
void configDone(){CHECK(sessionId);policyInput("DONE "+String(sessionId)+" SCAN_CONFIGURED");CHECK(scanId);}
void frame(bool detected=true,bool valid=true,unsigned n=2){
 observation["at"]=tick;observation["frameSeq"]=n;observation["generation"]=generation;observation["scanId"]=scanId;observation["stepId"]=scanId?scanStep:checkStep;observation["cat"]["detected"]=detected;observation["cat"]["valid"]=valid;policyVision();
}
void settled(){policyTick();CHECK(action=="SETTLE");tick+=250;encoderAt[0]=encoderAt[1]=tick;policyTick();CHECK(action=="SETTLE");tick+=150;encoderAt[0]=encoderAt[1]=tick;policyTick();CHECK(action=="CAT_CHECK");}
void scanRequest(){policyInput("VQ "+String(scanId)+" 1 "+String(generation)+" 2");CHECK(requests.back().mode=="CAT");CHECK(!motorRunning);}
int main(){
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
 fixture();beginScan("TAG","APPROACH_TAG");mode="UART";lastUnoAt=tick;tick+=2500;policyTick();CHECK(lastFault=="Uno link timeout");CHECK(!scanConfigRetryPending);
 fixture();move(30,30);policyInput("ERROR "+String(motorId)+" BAD_CHECKSUM");CHECK(!scanConfigRetryPending);CHECK(phase=="STOPPED");
 fixture();pulsesL=pulsesR=1;CHECK(fabs(angle()-10.125)<.001);pulsesL=2;pulsesR=0;CHECK(fabs(angle()-10.125)<.001);
 fixture();observation["tags"][0]["id"]=9;observation["tags"][0]["width"]=.3;observation["tags"][0]["centerX"]=.9;policyTick();CHECK(action=="WAIT_STOP");CHECK(routeIndex==0);auto id=stopId;policyInput("DONE 0 MOTOR_STOPPED");CHECK(stopId==id);policyInput("DONE "+String(id)+" ACCEPTED");CHECK(routeIndex==0);stopped();policyTick();CHECK(routeIndex==1);CHECK(action=="SCAN_TAG");
 fixture();config["route"][1]=9;observation["tags"][0]["id"]=9;observation["tags"][0]["width"]=.3;policyTick();stopped();policyTick();CHECK(action=="TAG_REARM");observation["frameSeq"]=2;policyTick();CHECK(action=="TAG_REARM");observation["frameSeq"]=3;observation["tags"][0]["width"]=.1;policyTick();CHECK(action=="SCAN_TAG");
 fixture();phase="INTERACTION";search("OBSERVE");stopped();policyTick();CHECK(action=="WAIT_SCAN_CONFIG");configDone();CHECK(scanBudget==71);CHECK(commands.back().text.startsWith("SR "));scanRequest();tick+=1000;policyTick();CHECK(phase=="INTERACTION");frame(false);CHECK(commands.back().text.startsWith("VR "));auto size=commands.size();policyVision();CHECK(commands.size()==size);auto sid=scanId;auto gen=generation;policyInput("SD 0 "+String(gen)+" 1");CHECK(scanId==sid);policyInput("SD "+String(sid)+" "+String(gen-1)+" 1");CHECK(scanId==sid);frame(true,true,3);policyInput("SD "+String(sid)+" "+String(gen)+" 1");CHECK(action=="OBSERVE");
 fixture();phase="INTERACTION";beginScan("CAT","OBSERVE");configDone();scanUsed=71;sid=scanId;gen=generation;policyInput("SD "+String(sid)+" "+String(gen)+" 0");CHECK(catFailure=="找猫失败");CHECK(phase=="ENDING");CHECK(action=="WAIT_STOP");stopped();policyTick();CHECK(action=="WAIT_SESSION_STOP");policyInput("DONE "+String(sessionId)+" SESSION_STOPPED");CHECK(action=="WAIT_SESSION_START");policyInput("DONE "+String(sessionId)+" SESSION_STARTED");CHECK(phase=="PATROL");CHECK(routeIndex==0);CHECK(catFailure=="找猫失败");JsonDocument d;policySnapshot(d);CHECK(d["patrolStatus"]["catFailure"]["angle"].as<float>()>700);
 // CAT-07A..F: stopped recognition, loss/reacquisition, remaining distance and STOP.
 fixture();phase="INTERACTION";behavior("SLOW_APPROACH");CHECK(remainingCm==6);policyTick();CHECK(action=="CAT_DRIVE");CHECK(policyVisionMode()=="OFF");auto original=motorId;md();CHECK(remainingCm<6&&remainingCm>4);CHECK(action=="WAIT_STOP");stopped();settled();frame(false);policyTick();CHECK(action=="WAIT_SCAN_CONFIG");auto remaining=remainingCm;configDone();scanRequest();frame(true,true,4);policyInput("SD "+String(scanId)+" "+String(generation)+" 1");CHECK(action=="APPROACH_CAT");CHECK(remainingCm==remaining);policyTick();CHECK(action=="CAT_DRIVE");policyInput("MD "+String(original)+" 20 20");CHECK(action=="CAT_DRIVE");CHECK(remainingCm==remaining);policyControl("stop");frame(true,true,5);policyTick();CHECK(phase=="STOPPED");CHECK(!motorId);
 fixture();phase="INTERACTION";behavior("SLOW_APPROACH");policyTick();md();stopped();settled();frame(false,false);policyTick();CHECK(phase=="ENDING");CHECK(catFailureReason=="invalid cat frame");
 fixture();phase="INTERACTION";behavior("SLOW_APPROACH");policyTick();CHECK(!checkPending);config["frontAvoidanceEnabled"]=true;CHECK(policyVisionMode()=="FLOOR");policyControl("stop");auto count=commands.size();policyStopUnconfirmed();CHECK(commands.empty());policyTick();CHECK(commands.empty());CHECK(!motorId);
 // Touch branch pools, first touch, held touches, top/multiple and side boundaries.
 fixture();draws={99,99,50};onTouch(3);CHECK(firstTouch==3);CHECK(rounds==1);onTouch(0);CHECK(firstTouch==3);CHECK(rounds==1);stopped();touch[3]=touch[0]=false;policyTick();CHECK(action=="WAIT_SCAN_CONFIG");CHECK(scanFlags==0);
 fixture();draws={99,99,50};onTouch(0);stopped();touch[0]=false;side[0]=side[1]=.5;policyTick();CHECK(scanFlags==1);configDone();CHECK(commands.back().text.endsWith(" 1"));CHECK(commands.back().text.indexOf("DRIVE")<0);
 fixture();draws={99,99,50};onTouch(0);stopped();touch[0]=false;side[1]=.5;policyTick();CHECK(phase=="ENDING");
 fixture();draws={99,99,50};onTouch(0);stopped();touch[0]=false;side[1]=1;policyTick();CHECK(action=="ESCAPE_TURN");CHECK(turnDeg==90);md(9,9);stopped();policyTick();CHECK(action=="ESCAPE_MOVE");
 fixture();draws={99,99,50};onTouch(0);stopped();touch[0]=false;sideAt[0]=sideAt[1]=tick-3000;policyTick();CHECK(phase=="ENDING");
 fixture();draws={0,0};onTouch(0);stopped();policyTick();CHECK(action=="SIGNAL");CHECK(lastPattern==0);CHECK(patterns==14);touch[0]=false;tick=due;policyTick();stopped();settled();frame();policyTick();CHECK(action=="OBSERVE");
 // CAT-01..18 retained behavior choices and local completion, no camera-only trigger.
 fixture();policyTick();CHECK(phase=="PATROL");CHECK(initiator=="");policyControl("alert");CHECK(initiator=="ROBOT_SEARCH");CHECK(action=="WAIT_STOP");
 fixture();phase="INTERACTION";draws={0};chooseRobot();CHECK(afterFace=="PROBE_APPROACH");CHECK(lastRobotProbe==tick);stopped();settled();frame();policyTick();CHECK(action=="PROBE_DRIVE");policyTick();CHECK(action=="PROBE_MOVE");md(3,3);stopped();settled();frame();policyTick();CHECK(action=="PROBE_PAUSE");CHECK(probeStep==1);tick=due;observation["at"]=tick;policyTick();CHECK(probeStep==2);CHECK(segmentCm==4);policyTick();md(4,4);stopped();settled();frame(true,true,5);policyTick();tick=due;observation["at"]=tick;policyTick();CHECK(probeStep==3);CHECK(segmentCm==3);policyTick();md(3,3);stopped();settled();frame(true,true,6);policyTick();tick=due;policyTick();CHECK(action=="WAIT_STOP");
 for(int pick:{0,25,80}){fixture();phase="INTERACTION";observation["cat"]["width"]=.5;draws={(unsigned)pick};chooseRobot();CHECK(afterFace==(pick==0?"STAY_SIGNAL":pick==25?"OBSERVE":"STAY"));}
 fixture();phase="INTERACTION";lastRobotProbe=tick;draws={0};chooseRobot();CHECK(afterFace=="SLOW_APPROACH");
 for(int pick:{0,50,80}){fixture();rounds=2;initiator="CAT_TOUCH";phase="INTERACTION";draws={(unsigned)pick};onTouch(0);CHECK(rounds==3);stopped();policyTick();CHECK(pick==0?phase=="ENDING":action=="PREY_WAIT");}
 fixture();phase="INTERACTION";prey(true);CHECK(commands.back().text=="BEEP 360");CHECK(action=="PREY_WAIT");observation["cat"]["detected"]=false;tick+=1000;observation["at"]=tick;policyTick();CHECK(action=="PREY_WAIT");tick=due;policyTick();CHECK(phase=="STOPPED");
 fixture();phase="INTERACTION";prey(false);observation["cat"]["width"]=.05;policyTick();tick+=5000;observation["at"]=tick;policyTick();CHECK(phase=="ENDING");
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";followPending=true;lastTouch=tick;draws={0};hold("FOLLOW_WAIT",0);policyTick();CHECK(afterFace=="PROBE_APPROACH");
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";followPending=true;lastTouch=tick;draws={20};hold("FOLLOW_WAIT",0);policyTick();CHECK(afterFace=="OBSERVE");
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";followPending=true;lastTouch=tick;draws={90};hold("FOLLOW_WAIT",0);policyTick();CHECK(phase=="ENDING");
 fixture();phase="INTERACTION";hold("WAIT_CHASE",100);tick+=100;policyTick();CHECK(action=="WAIT_STOP");
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
 CHECK(action=="APPROACH_CAT");CHECK(remainingCm==5);
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
 fixture();phase="INTERACTION";for(int i=0;i<5;i++){draws={0};signal(true);CHECK(lastPattern==i%4);CHECK(due-tick==uint32_t(i%4==0?1800:i%4==1?2400:i%4==2?3600:5600));}
 fixture();phase="INTERACTION";config["captureCueFlashMs"]=2900;config["captureCueBeepMs"]=600;prey(true);CHECK(commands.back().text=="BEEP 600");CHECK(commands[commands.size()-2].text=="LED FLASH 200 2900");
 fixture();config["touchFeedbackBeepMs"]=700;draws={99,0,50};onTouch(0);stopped();CHECK(commands.back().text=="BEEP 700");
 fixture();mode="UART";lastUnoAt=tick-2501;policyTick();CHECK(phase=="STOPPED");CHECK(lastFault=="Uno link timeout");
 fixture();phase="INTERACTION";motorId=42;policyInput("MA 0 ENCODER_STALL 1 1");CHECK(motorId==42);policyInput("MA 42 ENCODER_STALL 1 1");CHECK(phase=="STOPPED");CHECK(!motorId);CHECK(!leftPwm&&!rightPwm);
 fixture();draws={99,99,90};onTouch(2);stopped();touch[2]=false;policyTick();policyTick();CHECK(action=="ESCAPE_MOVE");CHECK(leftPwm==210);md(22,22);stopped();policyTick();CHECK(action=="WAIT_STOP");CHECK(afterFace=="WAIT_CHASE");
 fixture();draws={99,99,0};onTouch(2);stopped();policyTick();CHECK(action=="PREY_WAIT");CHECK(touch[2]);
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";followPending=true;lastTouch=lastCatProbe=tick;draws={0};hold("FOLLOW_WAIT",0);policyTick();CHECK(afterFace=="OBSERVE");CHECK(draws.empty());
 fixture();phase="INTERACTION";routeIndex=1;beginScan("CAT","OBSERVE");configDone();policyInput("SD "+String(scanId)+" "+String(generation)+" 0");stopped();policyTick();policyInput("DONE "+String(sessionId)+" SESSION_STOPPED");policyInput("DONE "+String(sessionId)+" SESSION_STARTED");CHECK(routeIndex==1);CHECK(phase=="PATROL");
 fixture();phase="INTERACTION";motorId=42;scanId=42;touch[1]=true;policyInput("READY UNO_CAT_V6.6");CHECK(phase=="STOPPED");CHECK(!scanId&&!motorId&&!touch[1]);CHECK(!scanCapable);
 fixture();phase="INTERACTION";state("WAIT_RELEASE");touch[0]=true;tick+=4100;policyTick();CHECK(phase=="ENDING");
 for(float center:{.2f,.8f}){fixture();phase="INTERACTION";afterFace="OBSERVE";observation["cat"]["centerX"]=center;orient();CHECK(action=="ALIGN_CAT");CHECK(turnDeg==(center<.5?-20.25f:20.25f));md(2,2);stopped();settled();observation["cat"]["centerX"]=.5;frame();policyTick();CHECK(action=="OBSERVE");}
 fixture();phase="INTERACTION";afterFace="OBSERVE";alignSteps=8;observation["cat"]["centerX"]=.8;orient();CHECK(phase=="ENDING");CHECK(catFailureReason=="alignment budget exhausted");
 fixture();phase="INTERACTION";state("ALIGN_CAT");tick+=5001;policyTick();CHECK(phase=="ENDING");CHECK(catFailureReason=="alignment motion timeout");
 fixture();observation["tags"][0]["id"]=9;observation["tags"][0]["width"]=.1;observation["tags"][0]["centerX"]=.8;policyTick();CHECK(action=="TAG_ALIGN");CHECK(leftPwm==30);md(2,2);stopped();observation["frameSeq"]=2;observation["tags"][0]["centerX"]=.5;policyTick();CHECK(leftPwm==95&&rightPwm==95);
 fixture();phase="INTERACTION";onTouch(3,1234);onTouch(0,1234);CHECK(firstTouch==3);CHECK(touchTie);CHECK(multiTouch);CHECK(rounds==1);
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";rounds=2;draws={80};onTouch(3,1234);stopped();CHECK(action=="WAIT_RELEASE");touch[3]=false;policyTick();CHECK(action=="WAIT_SCAN_CONFIG");CHECK(scanNext=="PREY_SIGNAL");
 fixture();phase="INTERACTION";beginScan("CAT","OBSERVE");auto pendingConfig=sessionId;onTouch(0);CHECK(sessionId==0);stopped();policyInput("DONE "+String(pendingConfig)+" SCAN_CONFIGURED");CHECK(action=="TOUCH_PREY"||action=="WAIT_RELEASE"||action=="TOUCH_SIGNAL");CHECK(!scanId);
 fixture();phase="INTERACTION";beginScan("CAT","OBSERVE");configDone();policyInput("VQ "+String(scanId)+" 3 "+String(generation)+" 7");CHECK(scanUsed==7);policyInput("MP "+String(scanId)+" 2 2");CHECK(!motorRunning);policyInput("VQ "+String(scanId)+" 2 "+String(generation)+" 5");CHECK(scanUsed==7);
 fixture();config["minimumDrivePwm"]=140;move(30,-30);CHECK(commands.back().text=="MOTOR 140 -140");JsonDocument pwmState;policySnapshot(pwmState);CHECK(pwmState["patrolStatus"]["leftPwm"].as<int>()==140);CHECK(pwmState["patrolStatus"]["rightPwm"].as<int>()==-140);CHECK(pwmState["patrolStatus"]["requestedLeftPwm"].as<int>()==30);CHECK(pwmState["patrolStatus"]["requestedRightPwm"].as<int>()==-30);
 fixture();config["minimumDrivePwm"]=100;drive(1,95,"CAT_DRIVE");CHECK(commands.back().text=="DRIVE 1.00 100");policySnapshot(pwmState);CHECK(pwmState["patrolStatus"]["leftPwm"].as<int>()==100);
 fixture();config["minimumDrivePwm"]=140;beginScan("CAT","OBSERVE");configDone();CHECK(commands.back().text=="SR "+String(generation)+" 2 140 71 0");policyInput("MS "+String(scanId)+" 0 0");policySnapshot(pwmState);CHECK(pwmState["patrolStatus"]["leftPwm"].as<int>()==140);CHECK(pwmState["patrolStatus"]["requestedLeftPwm"].as<int>()==75);
 fixture();config["minimumDrivePwm"]=1;move(30,-30);CHECK(commands.back().text=="MOTOR 30 -30");
 printf("actual ESP policy core: %u checks passed\n",checks);
}
