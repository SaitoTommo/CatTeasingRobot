// Exercise the actual policy and generated defaults through the native harness.
#define main existingPolicyRegressionMain
#include "policy-core.test.cpp"
#undef main
void checkFixture(int limit=3){
 fixture();config["catCheckMaxFrames"]=limit;phase="INTERACTION";
 initiator="ROBOT_SEARCH";robotFound=true;oneFrame("APPROACH_CAT");
}
int main(){
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
  CHECK(checkReady);policyTick();CHECK(phase=="ENDING");CHECK(interactionExitReason=="cat left invitation frame");CHECK(!motorId);
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
 // CAT_CONFIRM retains its single final frame and configured wait semantics.
 fixture();config["catCheckMaxFrames"]=3;phase="INTERACTION";initiator="CAT_TOUCH";
 oneFrame("CAT_CONFIRMED");state("CAT_CONFIRM");frame(false,true,70);CHECK(checkReady&&!checkPending);CHECK(catCheckSamples==0);
 printf("CAT_CHECK multi-frame policy: %u checks passed\n",checks);
}
