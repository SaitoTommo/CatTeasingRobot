#define main existingPolicyRegressionMain
#include "policy-core.test.cpp"
#undef main
void tagFixture(int limit=3){fixture();config["tagScanMaxFrames"]=limit;beginScan("TAG","APPROACH_TAG");configDone();policyInput("VQ "+String(scanId)+" 1 "+String(generation)+" 1 3");CHECK(scanUsed==1.5f);CHECK(requests.back().mode=="TAGS");}
void tagFrame(unsigned seq,bool found=false,bool valid=true){observation["tags"].to<JsonArray>();if(found)observation["tags"][0]["id"]=9;observation["tagsValid"]=valid;frame(false,true,seq);}
int main(){
 JsonDocument defaults;policyDefaults(defaults);CHECK(defaults["tagScanMaxFrames"].as<int>()==3);
 for(int limit:{1,3,10})for(int hit=1;hit<=limit;hit++){
  tagFixture(limit);auto n=commands.size();auto q=requests.size();
  for(int sample=1;sample<=hit;sample++){tagFrame(10+sample,sample==hit);CHECK(scanTagSamples==sample);CHECK(!motorRunning);CHECK(commands.size()==n+(sample==hit?1:0));}
  CHECK(scanReplyResult==1);CHECK(scanFrame==unsigned(10+hit));CHECK(requests.size()==q+hit-1);
  tagFrame(100,true);CHECK(scanTagSamples==hit);CHECK(commands.size()==n+1);
 }
 for(int limit:{1,3,10}){
  tagFixture(limit);auto n=commands.size();
  for(int sample=1;sample<=limit;sample++){tagFrame(200+sample);CHECK(scanTagSamples==sample);CHECK(commands.size()==n+(sample==limit?1:0));CHECK(!motorRunning);}
  CHECK(scanReplyResult==0);CHECK(scanFrame==unsigned(200+limit));
  policyInput("VQ "+String(scanId)+" 1 "+String(generation)+" 1 3");CHECK(scanTagSamples==limit);CHECK(commands.size()==n+2);
  policyInput("MS "+String(scanId));policyInput("VQ "+String(scanId)+" 2 "+String(generation)+" 3 6");CHECK(scanTagSamples==0);CHECK(scanUsed==3);tagFrame(400);CHECK(scanTagSamples==1);
 }
 tagFixture();tagFrame(10);auto q=requests.size();tagFrame(10);tagFrame(9);CHECK(scanTagSamples==1&&requests.size()==q);
 observation["generation"]=generation+1;observation["frameSeq"]=11;policyVision();CHECK(scanTagSamples==1);
 observation["generation"]=generation;observation["stepId"]=scanStep+1;policyVision();CHECK(scanTagSamples==1);
 policyInput("VQ "+String(scanId)+" 1 "+String(generation)+" 1 3");CHECK(scanTagSamples==1);
 tagFrame(12,true);CHECK(scanReplyResult==1);
 tagFixture();tagFrame(10,false,false);CHECK(scanReplyResult==2);CHECK(scanTagSamples==1);CHECK(!motorRunning);
 tagFixture();tagFrame(10);tick+=scanVisionWindow();tagFrame(11,true);CHECK(scanTagSamples==1);policyTick();CHECK(action=="FAULT");CHECK(!scanId);
 tagFixture();tagFrame(10);onTouch(0);CHECK(!scanId);CHECK(initiator=="CAT_TOUCH");
 // Extended half-pulse consistency and monotonicity; legacy integer packets still accepted.
 tagFixture();policyInput("MD "+String(scanId)+" 2 1");CHECK(scanUsed==1.5f);auto q2=requests.size();policyInput("VQ "+String(scanId)+" 2 "+String(generation)+" 2 3");CHECK(requests.size()==q2);CHECK(scanUsed==1.5f);
 policyInput("VQ "+String(scanId)+" 2 "+String(generation)+" 1 2");CHECK(scanStep==1);
 policyInput("VQ "+String(scanId)+" 2 "+String(generation)+" 3");CHECK(scanStep==2&&scanUsed==3);
 fixture();phase="INTERACTION";beginScan("CAT","CHOOSE_ROBOT");configDone();pulsesL=3;pulsesR=1;pauseForTouch();CHECK(scanUsed==2);
 printf("tag stopped checks and mean scan policy: %u checks passed\n",checks);
}
