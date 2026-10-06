#define main existingPolicyRegressionMain
#include "policy-core.test.cpp"
#undef main

void targetFrame(unsigned n,bool found,float x=.5f,float w=.1f,bool valid=true){
 observation["tags"].to<JsonArray>();observation["tagsValid"]=valid;
 if(found){auto t=observation["tags"][0];t["id"]=9;t["centerX"]=x;t["width"]=w;}
 if(motorId&&motorConfirmed&&motorRunning)policyInput("MP "+String(motorId)+" 1 1");
 frame(false,true,n);
}
void forwardFixture(){
 fixture();targetFrame(2,true);policyTick();CHECK(motorId);policyCommandSent(motorId);policyInput("MS "+String(motorId)+" 0 0");CHECK(motorConfirmed&&motorRunning);
}
void tagStopped(){
 stopped();CHECK(action=="TAG_SETTLE");CHECK(policyVisionMode()=="OFF");
 tick+=250;encoderAt[0]=encoderAt[1]=tick;policyTick();CHECK(action=="TAG_SETTLE");
 tick+=250;encoderAt[0]=encoderAt[1]=tick;policyTick();CHECK(action=="TAG_RECHECK");CHECK(checkPending);CHECK(requests.back().mode=="TAGS");
}
void recheckFixture(){forwardFixture();targetFrame(3,false);policyTick();tick+=1000;targetFrame(4,false);policyTick();CHECK(action=="WAIT_STOP"&&afterStop=="TAG_SETTLE");tagStopped();}
void scanEndAt(float total,int result=0){
 configDone();auto half=unsigned((total-scanOrigin)*2);policyInput("VQ "+String(scanId)+" 1 "+String(generation)+" "+String(half/2)+" "+String(half));
 CHECK(scanUsed==total);policyInput("SD "+String(scanId)+" "+String(generation)+" "+String(result));
}
int main(){
 // Target-local memory: side, trend in dead band, duplicate and expiry guards.
 TagRecoveryMemory m;CHECK(m.direction(9,10,1)==1);m.observe(9,1,100,.3,.1);CHECK(m.direction(9,200,1)==-1);
 m.observe(9,2,200,.51,.1);CHECK(m.direction(9,201,-1)==1);
 m.observe(9,2,900,.9,.1);CHECK(m.at==200&&m.center==.51f);CHECK(m.direction(10,201,-1)==-1);CHECK(m.direction(9,5201,-1)==-1);
 m.observe(10,3,300,.5,.1);CHECK(!m.hasPrevious);CHECK(m.direction(10,301,-1)==-1);
 m.clear();m.observe(10,1,100,.2,.1);m.observe(10,2,6000,.5,.1);CHECK(m.direction(10,6001,-1)==-1);m.clear();
 m.miss(400);m.miss(999);CHECK(m.lostAt==400);CHECK(!m.expired(1399,1000));CHECK(m.expired(1400,1000));
 m.observe(10,4,500,.5,.1);CHECK(!m.losing);m.clear();m.observe(9,1,UINT32_MAX-100,.2,.1);CHECK(m.direction(9,100,1)==-1);
 // A single negative retains exactly the confirmed forward command; repeated/old frames never extend its deadline.
 forwardFixture();auto id=motorId;auto n=commands.size();targetFrame(3,false);policyTick();CHECK(tagMemory.losing&&motorId==id&&commands.size()==n);
 tick+=999;targetFrame(3,false);policyTick();CHECK(action=="APPROACH_TAG"&&motorId==id);CHECK(tagMemory.lostAt==100000);
 tick++;targetFrame(2,false);policyTick();CHECK(action=="WAIT_STOP");CHECK(afterStop=="TAG_SETTLE");
 // Positive frame on the expiry boundary cancels loss, without STOP or an extra MOTOR command.
 forwardFixture();id=motorId;n=commands.size();targetFrame(3,false);policyTick();tick+=1000;targetFrame(4,true);policyTick();CHECK(!tagMemory.losing&&motorId==id&&commands.size()==n);
 targetFrame(5,true,.5,.3);policyTick();CHECK(action=="WAIT_STOP"&&afterStop=="TAG_ARRIVAL");stopped();policyTick();CHECK(routeIndex==1&&!tagMemory.frame);
 for(int grace:{0,3000}){forwardFixture();config["tagLossGraceMs"]=grace;targetFrame(3,false);policyTick();if(grace){CHECK(action=="APPROACH_TAG");tick+=grace;targetFrame(4,false);policyTick();}CHECK(action=="WAIT_STOP");}
 forwardFixture();tick+=750;policyTick();CHECK(action=="FAULT"&&!tagMemory.frame);
 // Unconfirmed movement, alignment, stale/invalid capture and obstacle cannot use grace.
 for(int reason=0;reason<5;reason++){
  forwardFixture();switch(reason){case 0:motorConfirmed=false;break;case 1:action="TAG_ALIGN";break;case 2:observation["tagsValid"]=false;break;case 3:tick+=1500;policyInput("MP "+String(motorId)+" 1 1");break;case 4:config["frontAvoidanceEnabled"]=true;observation["front"]="BLOCKED";break;}
  if(reason<2)targetFrame(3,false);policyTick();CHECK(action=="WAIT_STOP"&&afterStop=="TAG_SETTLE");CHECK(!tagMemory.losing);
 }
 fixture();config["frontAvoidanceEnabled"]=true;observation["front"]="BLOCKED";policyTick();tick+=3000;targetFrame(3,true);policyTick();CHECK(action=="FAULT"&&!motorId);
 // Stationary recheck counts new, matching-generation frames, exits early on success and never advances route from memory.
 for(int limit:{1,3,10})for(int hit=1;hit<=limit;hit++){
  recheckFixture();config["tagScanMaxFrames"]=limit;auto q=requests.size();
  for(int i=1;i<=hit;i++){targetFrame(10+i,i==hit);CHECK(tagRecheckSamples==i);CHECK(!motorId);if(i<hit){policyTick();CHECK(action=="TAG_RECHECK");}}
  CHECK(requests.size()==q+hit-1);policyTick();CHECK(action=="APPROACH_TAG"&&routeIndex==0);CHECK(!scanId);policyTick();CHECK(motorId);
 }
 recheckFixture();targetFrame(11,false);auto q=requests.size();targetFrame(11,false);targetFrame(10,false);CHECK(tagRecheckSamples==1&&requests.size()==q);
 observation["generation"]=generation+1;observation["frameSeq"]=12;policyVision();CHECK(tagRecheckSamples==1);
 observation["generation"]=generation;observation["stepId"]=checkStep+1;policyVision();CHECK(tagRecheckSamples==1);
 recheckFixture();targetFrame(11,false,.5,.1,false);policyTick();CHECK(action=="FAULT");
 recheckFixture();tick+=9000;policyTick();CHECK(action=="FAULT");
 recheckFixture();policyInput("DATA 1 HW EL 1 1 1 0 P");CHECK(action=="FAULT");
 recheckFixture();encoderRpm[1]=1;policyTick();CHECK(action=="FAULT");
 // Settle requires two later sample epochs; no vision ticket before checked STOP or encoder quiet.
 forwardFixture();stopForTagRecheck("test");n=requests.size();policyTick();CHECK(requests.size()==n);stopped();tick+=3001;policyTick();CHECK(action=="FAULT");
 // Remember last seen side, then local + reverse + full remainder share one budget, with mean half-pulse progress.
 recheckFixture();tagMemory.observe(9,100,tick,.3,.1);
 for(int i=0;i<3;i++)targetFrame(110+i,false);policyTick();CHECK(tagRecoveryStage==1&&scanDirection==-1&&scanBudget==6);scanEndAt(5.5f);
 CHECK(tagRecoveryStage==2&&scanDirection==1&&scanBudget==17&&scanOrigin==5.5f);scanEndAt(16.5f);
 CHECK(tagRecoveryStage==3&&scanDirection==1&&scanBudget==71&&scanOrigin==16.5f);scanEndAt(70.5f);CHECK(action=="FAULT");
 // Early reacquisition leaves recovery; memory cannot leak across stop, camera/config reset or cat touch.
 recheckFixture();for(int i=0;i<3;i++)targetFrame(11+i,false);policyTick();scanEndAt(1.5f,1);CHECK(action=="APPROACH_TAG"&&!tagRecoveryStage);
 forwardFixture();policyResetTagRecovery();CHECK(!tagMemory.frame&&!tagRecoveryStage);
 forwardFixture();policyControl("stop");CHECK(!tagMemory.frame&&!tagMemory.losing);
 forwardFixture();onTouch(0);CHECK(!tagMemory.frame&&initiator=="CAT_TOUCH");
 forwardFixture();mode="UART";lastUnoAt=0;policyTick();CHECK(linkRecovering&&!tagMemory.frame);
 printf("tag approach loss/recheck/recovery: %u checks passed\n",checks);
}
