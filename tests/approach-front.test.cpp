#define main existingPolicyRegressionMain
#include "policy-core.test.cpp"
#undef main

void targetFrame(unsigned n,bool found,float x=.5f,float w=.1f){
  observation["tags"].to<JsonArray>();observation["tagsValid"]=true;
  if(found){auto t=observation["tags"][0];t["id"]=9;t["centerX"]=x;t["width"]=w;}
  observation["at"]=tick;observation["frameSeq"]=n;
}
void forwardFixture(){fixture();targetFrame(2,true);policyTick();CHECK(motorId);policyCommandSent(motorId);policyInput("MS "+String(motorId)+" 0 0");}

void blockedFrame(unsigned frameNumber=20){
  config["frontAvoidanceEnabled"]=true;
  observation["at"]=tick;observation["frameSeq"]=frameNumber;
  observation["front"]="BLOCKED";observation["frontCalibrated"]=true;
}
void catDriveFixture(bool fast=true){
  fixture();config["frontAvoidanceEnabled"]=true;phase="INTERACTION";initiator="ROBOT_SEARCH";observation["cat"]["width"]=.05;
  config["robotNearSignalPercent"]=100;config["robotNearObservePercent"]=0;config["robotNearStayPercent"]=0;
  behavior(fast?"FAST_APPROACH":"SLOW_APPROACH");policyTick();
  CHECK(action=="CAT_DRIVE"&&motorId);CHECK(policyVisionMode()=="FLOOR");
}
int main(){
  // Front arrival wins even when the target disappeared or is off center.
  for(bool enabled:{true})for(bool lost:{false,true}){
    forwardFixture();auto oldMotor=motorId;config["frontAvoidanceEnabled"]=enabled;
    targetFrame(3,!lost,.9,.01);blockedFrame(3);policyTick();
    CHECK(action=="WAIT_STOP"&&afterStop=="TAG_ARRIVAL"&&routeIndex==0);
    CHECK(approachEndReason=="FRONT_BLOCKED");auto stopping=stopId;
    policyInput("MD "+String(oldMotor)+" 20 20");policyInput("DONE "+String(stopping+1)+" MOTOR_STOPPED");policyInput("DONE "+String(stopping)+" WRONG_RESULT");
    CHECK(stopId==stopping&&routeIndex==0);policyTick();CHECK(routeIndex==0);
    stopped();policyTick();CHECK(routeIndex==1&&action=="SCAN_TAG");
    policyInput("DONE "+String(stopping)+" MOTOR_STOPPED");CHECK(routeIndex==1);
  }
  forwardFixture();config["route"][1]=9;blockedFrame();policyTick();stopped();policyTick();CHECK(action=="TAG_REARM"&&routeIndex==1);
  targetFrame(21,true,.5,.3);policyTick();CHECK(action=="TAG_REARM");
  targetFrame(22,false);policyTick();CHECK(action=="SCAN_TAG");
  // Interaction and STOP start in the same tick, no motion before confirmation.
  for(bool fast:{false,true})for(bool enabled:{true}){
    catDriveFixture(fast);config["frontAvoidanceEnabled"]=enabled;auto oldMotor=motorId;
    blockedFrame();observation["cat"]["valid"]=false;policyTick();
    CHECK(action=="SIGNAL"&&interactionStopPending&&stopId);
    CHECK(robotActions==1&&robotAction=="STAY_SIGNAL"&&!robotFast&&!robotSlowFlash&&remainingCm==0);
    CHECK(commands.front().text=="STOP");bool light=false,sound=false;
    for(auto c:commands){if(c.text.startsWith("LED FLASH"))light=true;if(c.text.startsWith("BEEP ")&&c.text!="BEEP 0")sound=true;}
    CHECK(light&&sound);auto stopping=stopId;auto n=commands.size();auto deadline=due;
    policyInput("MD "+String(oldMotor)+" 20 20");policyInput("DONE "+String(stopping)+" WRONG_RESULT");policyTick();
    CHECK(action=="SIGNAL"&&interactionStopPending&&commands.size()==n&&robotActions==1);
    stopped();CHECK(action=="SIGNAL"&&due==deadline&&!interactionStopPending);
    policyTick();CHECK(action=="SIGNAL"&&commands.size()==n);
    observation["cat"]["valid"]=true;chooseRobot(true);CHECK(action=="SIGNAL"&&!motorId&&robotActions==1);
  }
  catDriveFixture();blockedFrame();policyTick();tick+=2500;policyTick();CHECK(action=="FAULT"&&phase=="STOPPED");
  catDriveFixture();robotActions=4;blockedFrame();policyTick();CHECK(robotActions==5&&phase=="ENDING"&&action=="WAIT_STOP");
  fixture();config["frontAvoidanceEnabled"]=true;phase="INTERACTION";initiator="ROBOT_SEARCH";config["robotNearSignalPercent"]=100;config["robotNearObservePercent"]=config["robotNearStayPercent"]=0;behavior("PROBE_APPROACH");policyTick();CHECK(action=="PROBE_MOVE"&&policyVisionMode()=="FLOOR");blockedFrame();policyTick();CHECK(action=="SIGNAL"&&interactionStopPending&&robotActions==1&&probeStep==0);
  fixture();phase="PATROL";action="TAG_RECHECK";checkReady=true;blockedFrame();observation["tagsValid"]=false;tagRecheckEncoder[0]=encoder[0];tagRecheckEncoder[1]=encoder[1];policyTick();CHECK(action=="WAIT_STOP"&&afterStop=="TAG_ARRIVAL"&&!scanId);
  fixture();phase="INTERACTION";initiator="ROBOT_SEARCH";robotAction="SLOW_APPROACH";action="CAT_CHECK";afterSettle="APPROACH_CAT";checkReady=true;blockedFrame();observation["cat"]["valid"]=false;policyTick();CHECK(interactionStopPending&&robotActions==1&&!scanId);
  fixture();action="TAG_RECHECK";checkReady=true;config["frontAvoidanceEnabled"]=true;observation["front"]="INVALID";observation["tags"].to<JsonArray>();policyTick();CHECK(action=="WAIT_STOP"&&afterStop=="APPROACH_TAG"&&!scanId&&routeIndex==0);
  fixture();phase="INTERACTION";action="CAT_CHECK";afterSettle="APPROACH_CAT";checkReady=true;config["frontAvoidanceEnabled"]=true;observation["front"]="INVALID";observation["cat"]["detected"]=false;policyTick();CHECK(action=="WAIT_STOP"&&afterStop=="APPROACH_CAT"&&!scanId&&!interactionStopPending);
  fixture();phase="INTERACTION";initiator="CAT_TOUCH";behavior("SLOW_APPROACH");blockedFrame();policyTick();CHECK(action=="OBSERVE"&&interactionStopPending&&!motorId);
  // Invalid, uncalibrated and stale observations stop; they never mean arrival.
  for(int reason=0;reason<4;reason++){
    forwardFixture();blockedFrame();if(reason==0)observation["front"]="INVALID";if(reason==1)observation["frontCalibrated"]=false;if(reason==2){tick+=1500;feedbackAt=tick;}
    if(reason==3)config["frontCalibrated"]=false;
    policyTick();CHECK(action=="WAIT_STOP"&&afterStop=="APPROACH_TAG"&&routeIndex==0);stopped();tick+=3000;policyTick();CHECK(action=="FAULT"&&routeIndex==0);
    catDriveFixture();blockedFrame();if(reason==0)observation["front"]="INVALID";if(reason==1)observation["frontCalibrated"]=false;if(reason==2)tick+=1500;
    if(reason==3)config["frontCalibrated"]=false;
    policyTick();CHECK(action=="WAIT_STOP"&&afterStop=="APPROACH_CAT"&&robotActions==0);stopped();tick+=3000;policyTick();CHECK(action=="FAULT"&&robotActions==0);
  }
  // Disabled front checks never advance a small target or demand calibration.
  for(const char *front:{"BLOCKED","INVALID","CLEAR"}){
    fixture();config["frontAvoidanceEnabled"]=false;config["frontCalibrated"]=false;
    targetFrame(2,true,.5,.01);observation["front"]=front;observation["frontCalibrated"]=false;policyTick();
    CHECK(routeIndex==0&&action=="APPROACH_TAG"&&motorId&&!stopId);
    CHECK(leftPwm>0&&rightPwm==leftPwm&&approachEndReason!="FRONT_BLOCKED");
    fixture();config["frontAvoidanceEnabled"]=false;targetFrame(2,true,.5,.3);observation["front"]=front;policyTick();
    CHECK(action=="WAIT_STOP"&&afterStop=="TAG_ARRIVAL"&&approachEndReason=="WIDTH_REACHED");
    fixture();config["frontAvoidanceEnabled"]=false;action="TAG_RECHECK";checkReady=true;targetFrame(2,true);observation["front"]=front;policyTick();
    CHECK(routeIndex==0&&action=="APPROACH_TAG"&&!stopId);
    for(bool fast:{false,true}){
      catDriveFixture(fast);config["frontAvoidanceEnabled"]=false;blockedFrame();config["frontAvoidanceEnabled"]=false;policyTick();
      CHECK(action=="CAT_DRIVE"&&motorId&&!interactionStopPending&&!stopId&&robotActions==0);
      CHECK(policyVisionMode()=="OFF");
    }
  }
  fixture();phase="INTERACTION";initiator="ROBOT_SEARCH";behavior("PROBE_APPROACH");policyTick();blockedFrame();config["frontAvoidanceEnabled"]=false;policyTick();
  CHECK(action=="PROBE_MOVE"&&motorId&&!interactionStopPending&&!stopId&&policyVisionMode()=="OFF");
  fixture();phase="INTERACTION";action="CAT_CHECK";afterSettle="APPROACH_CAT";checkReady=true;blockedFrame();config["frontAvoidanceEnabled"]=false;policyTick();
  CHECK(!interactionStopPending&&robotActions==0);
  fixture();phase="INTERACTION";initiator="ROBOT_SEARCH";blockedFrame();config["frontAvoidanceEnabled"]=false;observation["cat"]["width"]=.05;chooseRobot(true);
  CHECK(robotAction=="FAST_APPROACH"&&robotActions==0&&!interactionStopPending);
  printf("front approach priority: %u checks passed\n",checks);
}
