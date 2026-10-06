// Exercise the actual policy and generated defaults through the native harness.
#define main existingPolicyRegressionMain
#include "policy-core.test.cpp"
#undef main
void checkFixture(int limit=3){
 fixture();config["catCheckMaxFrames"]=limit;phase="INTERACTION";
 initiator="ROBOT_SEARCH";robotFound=true;oneFrame("APPROACH_CAT");
}
int main(){
 // Both entry points use the same stopped confirmation with no centering turns,
 // even at the image edges or with a saved narrow legacy centering tolerance.
 for(const char *entry:{"CAT_TOUCH","ROBOT_SEARCH"})for(float center:{0.f,.2f,.8f,1.f})for(float width:{.1f,.2f,.5f}){
  fixture();phase="INTERACTION";initiator=entry;config["centerTolerancePercent"]=1;
  config["catConfirmPreyPercent"]=100;config["catConfirmExplorePercent"]=0;
  config["robotNearSignalPercent"]=0;config["robotNearObservePercent"]=100;config["robotNearStayPercent"]=0;
  observation["cat"]["centerX"]=center;observation["cat"]["width"]=width;
  beginScan("CAT",initiator=="CAT_TOUCH"?"CHOOSE_TOUCH":"CHOOSE_ROBOT");configDone();scanRequest();frame(true);
  policyInput("SD "+String(scanId)+" "+String(generation)+" 1");
  CHECK(action=="WAIT_STOP");CHECK(afterStop=="CAT_CONFIRM");
  completeConfirmation();CHECK(action==(initiator=="CAT_TOUCH"?"PREY_WAIT":width>=.35f?"OBSERVE":"APPROACH_CAT"));
  CHECK(!motorId&&!scanId&&!stopId);CHECK(!checkPending);CHECK(robotActions==0);
 }
 // A lost confirmation resumes the remaining search budget for either entry.
 for(const char *entry:{"CAT_TOUCH","ROBOT_SEARCH"}){
  fixture();phase="INTERACTION";initiator=entry;
  beginScan("CAT",initiator=="CAT_TOUCH"?"CHOOSE_TOUCH":"CHOOSE_ROBOT");configDone();scanRequest();frame(true);
  auto used=scanUsed;policyInput("SD "+String(scanId)+" "+String(generation)+" 1");
  completeConfirmation(false);CHECK(action=="WAIT_SCAN_CONFIG");CHECK(scanOrigin==used);CHECK(!robotFound);
 }
 JsonDocument defaults;policyDefaults(defaults);CHECK(defaults["catCheckMaxFrames"].as<int>()==3);
 // A detection at any position succeeds early; misses never command motion.
 for(int limit:{1,2,3})for(int hit=1;hit<=limit;++hit){
  checkFixture(limit);auto before=commands.size();auto requested=requests.size();
  for(int sample=1;sample<=hit;++sample){
   frame(sample==hit,true,10+sample);
   CHECK(catCheckSamples==sample);CHECK(commands.size()==before);CHECK(!motorId);
   if(sample<hit){CHECK(checkPending&&!checkReady);policyTick();CHECK(action=="CAT_CHECK");CHECK(phase=="INTERACTION");CHECK(requests.size()==requested+sample);}
  }
  CHECK(checkReady&&!checkPending);CHECK(requests.size()==requested+hit-1);
  policyTick();CHECK(action=="APPROACH_CAT");CHECK(phase=="INTERACTION");CHECK(!motorId);
 }
 // All negatives resolve only after the configured number of distinct frames.
 for(int limit:{1,2,3}){
  checkFixture(limit);
  for(int sample=1;sample<=limit;++sample){
   frame(false,true,20+sample);
   if(sample<limit){policyTick();CHECK(action=="CAT_CHECK");CHECK(phase=="INTERACTION");CHECK(!motorId);}
  }
  CHECK(checkReady);policyTick();CHECK(phase=="INTERACTION");CHECK(action=="OBSERVE");CHECK(robotAction=="OBSERVE");CHECK(robotActions==0);CHECK(!motorId);
 }
 // Duplicate, superseded, and unrelated frames cannot satisfy the window.
 checkFixture();frame(false,true,30);auto requested=requests.size();frame(false,true,30);
 CHECK(catCheckSamples==1);CHECK(requests.size()==requested);CHECK(checkPending);
 observation["frameSeq"]=31;observation["generation"]=generation+1;policyVision();CHECK(catCheckSamples==1);
 observation["generation"]=generation;observation["stepId"]=checkStep+1;policyVision();CHECK(catCheckSamples==1);
 observation["stepId"]=checkStep;observation["scanId"]=123;policyVision();CHECK(catCheckSamples==1);
 frame(true,true,32);CHECK(checkReady);policyTick();CHECK(action=="APPROACH_CAT");
 // Invalid and missing frames fault while stopped, without resuming patrol.
 checkFixture();frame(false,true,40);frame(false,false,41);policyTick();CHECK(phase=="STOPPED");CHECK(action=="FAULT");CHECK(!motorId);
 checkFixture();frame(false,true,50);tick+=(uint32_t)setting("scanVisionTimeoutMs");policyTick();CHECK(phase=="STOPPED");CHECK(!checkPending);CHECK(!motorId);
 // Touch preemption invalidates the old ticket and its sample budget.
 checkFixture();frame(false,true,60);onTouch(3);CHECK(!checkPending);CHECK(initiator=="CAT_TOUCH");
 // Touch and robot confirmation both retry distinct negative frames.
 fixture();config["catCheckMaxFrames"]=3;phase="INTERACTION";initiator="CAT_TOUCH";
 oneFrame("CAT_CONFIRMED");state("CAT_CONFIRM");frame(false,true,70);CHECK(!checkReady&&checkPending);CHECK(catCheckSamples==1);
 // Complete approach distance is dispatched once, followed by one stopped check.
 for(bool fast:{false,true}){
  fixture();phase="INTERACTION";initiator="ROBOT_SEARCH";
  observation["cat"]["width"]=.1;behavior(fast?"FAST_APPROACH":"SLOW_APPROACH");float goal=remainingCm;
  policyTick();CHECK(action=="CAT_DRIVE");CHECK(driveCm==goal);
  CHECK(driveCm>setting("catApproachStepCm"));
  unsigned pulses=(unsigned)ceilf(goal*400/(2*PI*wheel));
  md(pulses,pulses);CHECK(remainingCm==0);stopped();settled();
  frame(true,true,80);policyTick();CHECK(action=="APPROACH_CAT");
  policyTick();CHECK(robotActions==1);CHECK(action=="WAIT_STOP");
 }
 // Confirmation shares early hit / maximum-three semantics with both initiators.
 for(const char *entry:{"CAT_TOUCH","ROBOT_SEARCH"})for(int hit=1;hit<=3;hit++){
  fixture();phase="INTERACTION";initiator=entry;config["catCheckMaxFrames"]=3;
  oneFrame("CAT_CONFIRMED","CAT_CONFIRM");
  for(int i=1;i<=hit;i++){frame(i==hit,true,90+i);CHECK(catCheckSamples==i);CHECK(checkPending==(i<hit));}
  CHECK(checkReady);CHECK(!motorId);
 }
 printf("CAT_CHECK multi-frame policy: %u checks passed\n",checks);
}
