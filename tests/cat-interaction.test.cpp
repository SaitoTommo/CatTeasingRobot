// Reuse the real-policy harness, not a copied behavioral implementation.
#define main existingPolicyRegressionMain
#include "policy-core.test.cpp"
#undef main
void releaseAll(){memset(touch,0,sizeof(touch));}
void freshCat(float width,bool detected=true,bool valid=true){observation["at"]=tick;observation["frameSeq"]=observation["frameSeq"].as<unsigned>()+1;observation["cat"]["width"]=width;observation["cat"]["detected"]=detected;observation["cat"]["valid"]=valid;}
int main(){
 // Repeated touch never redraws branch, first direction, or interaction identity.
 fixture();draws={99,0,99};onTouch(0,100);stopped();releaseAll();auto interaction=interactionId;auto originalBranch=branch;auto originalEscape=escape;
 for(int a:{1,2,3,0}){draws={42};onTouch(a,200);CHECK(interactionId==interaction);CHECK(firstTouch==0);CHECK(rounds==1);CHECK(branch==originalBranch);CHECK(escape==originalEscape);CHECK(draws.size()==1);releaseAll();}
 // Existing robot-initiated interaction is not converted by new cat contact.
 fixture();policyControl("alert");auto robot=interactionId;onTouch(1);CHECK(initiator=="ROBOT_SEARCH");CHECK(interactionId==robot);CHECK(branch==-1);
 // Explicit direction node -> turn -> checked stop -> actual DRIVE, no camera turn loop.
 fixture();config["escapePreyPercent"]=0;draws={99,0,99};onTouch(0);stopped();releaseAll();policyTick();CHECK(action=="ESCAPE_DIRECTION");policyTick();CHECK(action=="ESCAPE_TURN");CHECK(commands.back().text.startsWith("ROTATE "));
 md(9,9);stopped();policyTick();CHECK(action=="ESCAPE_MOVE");CHECK(commands.back().text=="DRIVE 23.00 140");CHECK(escapeRounds==0);
 JsonDocument snapshot;policySnapshot(snapshot);CHECK(snapshot["patrolStatus"]["escapeStage"].as<String>()=="RUN");
 // TOP search found -> face cat -> select away direction -> 180-degree turn -> DRIVE.
 fixture();config["escapePreyPercent"]=0;draws={99,0,99};onTouch(3);stopped();releaseAll();policyTick();policyTick();CHECK(scanNext=="ESCAPE_DIRECTION_FOUND");configDone();scanRequest();frame(true);policyInput("SD "+String(scanId)+" "+String(generation)+" 1");CHECK(action=="ESCAPE_DIRECTION");policyTick();CHECK(action=="ESCAPE_TURN");CHECK(turnDeg==180);md(18,18);stopped();policyTick();CHECK(action=="ESCAPE_MOVE");
 // Partial DRIVE contact abort uses checked MA progress, resumes remaining cm once.
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";branch=1;config["escapePreyPercent"]=0;drive(23,95,"ESCAPE_MOVE");auto oldMotor=motorId;
 policyInput("MA "+String(oldMotor)+" TOUCH 5 5");CHECK(touchPaused);CHECK(action=="WAIT_STOP");float left=resumeCm;CHECK(left>17&&left<18);onTouch(0);stopped();CHECK(action=="WAIT_TOUCH_RESUME");policyTick();CHECK(!motorId);releaseAll();policyTick();CHECK(action=="ESCAPE_MOVE");CHECK(driveCm==left);CHECK(escapeRounds==0);
 md(17,17);stopped();policyTick();CHECK(escapeRounds==1);CHECK(action=="WAIT_STOP");policyInput("MD "+String(oldMotor)+" 23 23");CHECK(escapeRounds==1);
 // Scan aborted while turning retains used angle; old replies are ignored.
 fixture();phase="INTERACTION";beginScan("CAT","OBSERVE");configDone();auto oldScan=scanId,oldGeneration=generation;scanUsed=10;
 policyInput("MS "+String(oldScan)+" 0 0");policyInput("MA "+String(oldScan)+" TOUCH 2 2");CHECK(scanUsed==12);onTouch(1);stopped();releaseAll();policyTick();CHECK(action=="WAIT_SCAN_CONFIG");CHECK(scanOrigin==12);configDone();CHECK(commands.back().text.endsWith(" 59 0"));policyInput("SD "+String(oldScan)+" "+String(oldGeneration)+" 4");CHECK(scanId!=0);CHECK(phase=="INTERACTION");
 // A vision-wait abort must not double count the already reported VQ progress.
 fixture();phase="INTERACTION";beginScan("CAT","OBSERVE");configDone();scanRequest();CHECK(scanUsed==2);onTouch(1);stopped();releaseAll();policyTick();CHECK(scanOrigin==2);configDone();CHECK(commands.back().text.endsWith(" 69 0"));
 // Five completed escape rounds force prey; aborts/contacts never count as rounds.
 fixture();phase="INTERACTION";config["escapePreyPercent"]=0;
 for(int n=1;n<=5;n++){state("POST_ESCAPE");policyTick();CHECK(escapeRounds==n);CHECK(n==5?action=="PREY_WAIT":action=="WAIT_STOP");if(n<5){stopId=0;resetMotor();}}
 fixture();phase="INTERACTION";config["escapePreyPercent"]=100;state("POST_ESCAPE");policyTick();CHECK(action=="PREY_WAIT");CHECK(escapeRounds==1);
 // Exploration probability is sampled once per completed cycle, after minimum time.
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";branch=0;config["explorePreyPercent"]=100;draws={0};signal(true);auto began=exploreAt;tick=due;freshCat(.3);draws={0};policyTick();CHECK(exploreRounds==1);CHECK(action=="WAIT_STOP");CHECK(draws.size()==1);
 stopId=0;tick=began+30000;signal(true);auto explorationId=interactionId;onTouch(2);CHECK(exploreAt==began);CHECK(interactionId==explorationId);releaseAll();tick=due;freshCat(.3);draws={0};policyTick();CHECK(action=="PREY_WAIT");CHECK(exploreRounds==2);CHECK(draws.empty());
 auto preyDue=due;onTouch(1);CHECK(due==preyDue);CHECK(action=="PREY_WAIT");releaseAll();tick=preyDue;freshCat(.4);policyTick();CHECK(phase=="ENDING");CHECK(interactionExitReason=="prey wait timeout");CHECK(lastFault!="MAX_WAIT_UNCONFIRMED: manual resume required");
 // Stationary prey absence requires new valid frames, and contact/invalid reset it.
 fixture();phase="INTERACTION";prey(false);freshCat(.1,false);policyTick();tick+=1000;freshCat(.1,false,false);policyTick();CHECK(!farAt);
 for(int n=0;n<=5;n++){tick+=1000;freshCat(.1,false);policyTick();}CHECK(phase=="ENDING");CHECK(interactionExitReason=="cat departed while prey");CHECK(!scanId);
 fixture();phase="INTERACTION";prey(false);freshCat(.1,false);policyTick();tick+=1000;onTouch(2);CHECK(!farAt);CHECK(action=="PREY_WAIT");
 // Chase is not merely seeing a stationary cat: a near transition or new contact wins.
 fixture();phase="INTERACTION";freshCat(.4);behavior("WAIT_CHASE");auto chaseDue=due;tick=chaseDue;freshCat(.4);policyTick();CHECK(phase=="ENDING");CHECK(interactionExitReason=="chase wait timeout");
 fixture();phase="INTERACTION";freshCat(.2);behavior("WAIT_CHASE");freshCat(.4);policyTick();CHECK(action=="WAIT_STOP");CHECK(afterFace=="OBSERVE");
 fixture();phase="INTERACTION";freshCat(.4);behavior("WAIT_CHASE");onTouch(0);CHECK(chaseDetected);policyTick();CHECK(action=="WAIT_STOP");CHECK(afterFace=="OBSERVE");
 // Neither legacy inactivity nor session duration ends a near ongoing interaction.
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";branch=0;sessionAt=lastTouch=tick-120000;hold("FOLLOW_WAIT",0);freshCat(.4);policyTick();CHECK(action=="SIGNAL");CHECK(phase=="INTERACTION");
 // Far/middle/near boundaries and first middle observation.
 fixture();freshCat(.2);CHECK(distanceClass()==-1);freshCat(.12);CHECK(distanceClass()==0);freshCat(.2);CHECK(distanceClass()==0);freshCat(.35);CHECK(distanceClass()==1);freshCat(.2);CHECK(distanceClass()==1);freshCat(.2,false);CHECK(distanceClass()==-1);
 // SETTLE gets prompt samples but never skips the user-specified minimum wait.
 fixture();phase="INTERACTION";config["scanMinimumMs"]=1000;settle("ORIENT");CHECK(policyTelemetryInterval()==250);
 tick+=250;encoderAt[0]=encoderAt[1]=tick;policyTick();CHECK(action=="SETTLE");tick+=250;encoderAt[0]=encoderAt[1]=tick;policyTick();CHECK(action=="SETTLE");tick+=500;encoderAt[0]=encoderAt[1]=tick;policyTick();CHECK(action=="CAT_CHECK");CHECK(policyTelemetryInterval()==2000);
 // Operator stop cancels a contact continuation and never resumes old movement.
 fixture();phase="INTERACTION";drive(23,95,"ESCAPE_MOVE");onTouch(0);CHECK(touchPaused);policyControl("stop");releaseAll();policyTick();CHECK(phase=="STOPPED");CHECK(!touchPaused);CHECK(!motorId);
 printf("continuous cat interaction: %u checks passed\n",checks);
}
