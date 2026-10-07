#define main existingPolicyChecks
#include "policy-core.test.cpp"
#undef main
int main(){
 for(bool probe:{false,true}){
  fixture();phase="INTERACTION";initiator="ROBOT_SEARCH";robotFound=true;
  robotAction=probe?"PROBE_APPROACH":"FAST_APPROACH";robotFast=!probe;
  remainingCm=7;probeStep=2;robotActions=1;config["catCheckMaxFrames"]=3;
  auto interaction=interactionId;
  oneFrame(probe?"PROBE_DONE":"APPROACH_CAT");
  for(int n=1;n<=3;n++){frame(false,true,n+10);policyTick();}
  CHECK(action=="WAIT_SCAN_CONFIG");CHECK(scanNext==(probe?"PROBE_DONE":"RESUME_APPROACH"));
  CHECK(robotActions==1&&remainingCm==7&&probeStep==2&&interactionId==interaction);
  configDone();scanRequest();frame(true,true,30);
  policyInput("SD "+String(scanId)+" "+String(generation)+" 1");completeConfirmation();
  CHECK(robotActions==1&&remainingCm==7&&probeStep==2&&interactionId==interaction);
  CHECK(robotAction==(probe?"PROBE_APPROACH":"FAST_APPROACH"));
  CHECK(action==(probe?"PROBE_PAUSE":"APPROACH_CAT"));
  if(!probe){observation["cat"]["width"]=.1;policyTick();CHECK(action=="CAT_DRIVE"&&driveCm==7);}
 }
 printf("action resume: %u checks passed\n",checks);
}
