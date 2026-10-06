#define main existingPolicyRegressionMain
#include "policy-core.test.cpp"
#undef main

void lostCat(float center=.8f){
 fixture();phase="INTERACTION";initiator="ROBOT_SEARCH";robotFound=true;
 robotAction="FAST_APPROACH";robotFast=true;remainingCm=7;
 config["catCheckMaxFrames"]=3;
 catMemory.observe(0,100,tick,center,.1);
 oneFrame("APPROACH_CAT");
 for(int n=1;n<=3;n++){frame(false,true,100+n);policyTick();if(n<3)CHECK(action=="CAT_CHECK"&&!motorId&&!scanId);}
 CHECK(action=="WAIT_SCAN_CONFIG");CHECK(scanNext=="RESUME_APPROACH");
 CHECK(robotAction=="FAST_APPROACH"&&robotActions==0&&remainingCm==7);
}
void catScanEnd(float total,int result=0){
 configDone();auto half=unsigned((total-scanOrigin)*2);
 policyInput("VQ "+String(scanId)+" 1 "+String(generation)+" "+String(half/2)+" "+String(half));
 CHECK(scanUsed==total);
 policyInput("SD "+String(scanId)+" "+String(generation)+" "+String(result));
}
int main(){
 // Both target kinds use the same memory and local/reverse/full search engine.
 for(float x:{.2f,.8f}){
  lostCat(x);int direction=x<.5f?-1:1;
  CHECK(catRecoveryStage==1&&scanDirection==direction&&scanBudget==6);
  catScanEnd(5.5);CHECK(catRecoveryStage==2&&scanDirection==-direction&&scanOrigin==5.5f&&scanBudget==17);
  catScanEnd(16.5);CHECK(catRecoveryStage==3&&scanOrigin==16.5f&&scanBudget==71);
  catScanEnd(71);CHECK(phase=="ENDING"&&!motorId&&!catMemory.frame);CHECK(catRecoveryStage==0);
 }
 // A recovered cat resumes the selected action and remaining distance, without a redraw.
 lostCat();configDone();scanRequest();observation["cat"]["centerX"]=.7f;observation["cat"]["width"]=.1f;
 frame(true,true,120);CHECK(catMemory.frame==120&&catMemory.center==.7f);
 policyInput("SD "+String(scanId)+" "+String(generation)+" 1");
 auto id=interactionId;completeConfirmation();
 CHECK(action=="APPROACH_CAT"&&remainingCm==7&&robotActions==0);
 CHECK(robotAction=="FAST_APPROACH"&&interactionId==id);policyTick();
 CHECK(action=="CAT_DRIVE"&&driveCm==7);
 // No memory, expired memory or a wrong target never provides false presence.
 for(int reason=0;reason<3;reason++){
  fixture();phase="INTERACTION";initiator="ROBOT_SEARCH";
  if(reason)catMemory.observe(reason==2?9:0,10,reason==2?tick:tick-5001,.2,.1);
  scanDirection=1;searchCatTarget("RESUME_APPROACH");
  CHECK(catRecoveryStage==0&&scanBudget==71&&scanDirection==1);
 }
 // Every stopped scan position tries up to three distinct new negative frames.
 lostCat();configDone();scanRequest();auto before=commands.size();
 for(int n=1;n<=3;n++){
  frame(false,true,200+n);CHECK(scanTagSamples==n);
  if(n<3)CHECK(commands.size()==before);
 }
 CHECK(commands.back().text.startsWith("VR "));CHECK(!scanReplyFound);
 // Wrong generation/step and duplicates cannot update memory or consume attempts.
 lostCat();configDone();scanRequest();frame(false,true,300);
 auto count=scanTagSamples;frame(false,true,300);CHECK(scanTagSamples==count);
 observation["frameSeq"]=301;observation["generation"]=generation+1;observation["cat"]["detected"]=true;
 policyVision();CHECK(catMemory.frame==100&&scanTagSamples==count);
 observation["generation"]=generation;observation["stepId"]=scanStep+1;
 policyVision();CHECK(catMemory.frame==100&&scanTagSamples==count);
 // First attack interrupts recovery and clears target memory; later attacks retain the branch.
 lostCat();onTouch(0);CHECK(initiator=="CAT_TOUCH"&&action=="WAIT_STOP");CHECK(!catMemory.frame&&!catRecoveryStage);
 lostCat();policyControl("stop");CHECK(phase=="STOPPED"&&!catMemory.frame&&!catRecoveryStage);
 lostCat();policyResetTagRecovery();CHECK(!catMemory.frame&&!catRecoveryStage);
 printf("shared cat recovery: %u checks passed\n",checks);
}
