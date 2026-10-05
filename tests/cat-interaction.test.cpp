// Reuse the real-policy harness, not a copied behavioral implementation.
#define main existingPolicyRegressionMain
#include "policy-core.test.cpp"
#undef main
void releaseAll(){memset(touch,0,sizeof(touch));}
void freshCat(float width,bool detected=true,bool valid=true){observation["at"]=tick;observation["frameSeq"]=observation["frameSeq"].as<unsigned>()+1;observation["cat"]["width"]=width;observation["cat"]["detected"]=detected;observation["cat"]["valid"]=valid;}
void endAndResumePatrol(){
 CHECK(phase=="ENDING");CHECK(action=="WAIT_STOP");stopped();policyTick();CHECK(action=="WAIT_SESSION_STOP");
 auto closing=sessionId;policyInput("DONE "+String(closing)+" WRONG_RESULT");CHECK(action=="WAIT_SESSION_STOP");
 policyInput("DONE "+String(closing)+" SESSION_STOPPED");CHECK(action=="WAIT_SESSION_START");CHECK(phase=="STARTING");
 policyInput("DONE "+String(sessionId)+" SESSION_STARTED");CHECK(phase=="PATROL");CHECK(!motorId);
}
int main(){
 // Independently configurable light/beep lengths finish at the longer output.
 fixture();policyControl("alert");stopped();config["signalFlashMs"]=100;config["signalBeepMs"]=800;behavior("STAY_SIGNAL");
 CHECK(due==tick+800);tick+=100;freshCat(.4);policyTick();CHECK(action=="SIGNAL");CHECK(robotActions==0);
 tick=due;freshCat(.4);policyTick();CHECK(robotActions==1);CHECK(action=="WAIT_STOP");
 // Only completed high-level actions consume the default five-action budget.
 fixture();policyControl("alert");stopped();freshCat(.4);
 for(int n=1;n<=5;n++){
  behavior("OBSERVE");CHECK(robotActions==n-1);tick=due-1;freshCat(.4);policyTick();CHECK(robotActions==n-1);
  tick=due;freshCat(.4);policyTick();CHECK(robotActions==n);
  if(n<5){CHECK(action=="FOLLOW_WAIT");CHECK(phase=="INTERACTION");tick=due;freshCat(.4);draws={20};policyTick();CHECK(action=="WAIT_STOP");CHECK(afterFace=="OBSERVE");stopped();settled();frame(true,true,n+10);policyTick();CHECK(action=="OBSERVE");}
 }
 CHECK(interactionExitReason=="robot action limit reached");endAndResumePatrol();
 // Fast/slow segments share checked motion; segment completion is not a round.
 fixture();policyControl("alert");stopped();freshCat(.1);config["robotFastApproachCm"]=3;config["robotSlowFlashPercent"]=100;
 behavior("FAST_APPROACH");CHECK(robotFast);policyTick();CHECK(action=="CAT_DRIVE");CHECK(commands.back().text=="DRIVE 1.00 180");
 md(1,1);stopped();settled();frame(true,true,20);policyTick();CHECK(!robotActions);CHECK(action=="APPROACH_CAT");
 freshCat(.2);policyTick();CHECK(robotActions==1);CHECK(afterFace=="SLOW_APPROACH");CHECK(!robotFast);stopped();settled();frame(true,true,21);policyTick();CHECK(robotSlowFlash);
 auto flashDraws=draws.size();policyTick();CHECK(action=="CAT_DRIVE");bool flashed=false;for(auto &c:commands)if(c.text.startsWith("LED FLASH 200 "))flashed=true;CHECK(flashed);CHECK(robotActions==1);CHECK(draws.size()==flashDraws);
 md(6,6);stopped();settled();frame(true,true,22);policyTick();CHECK(action=="APPROACH_CAT");policyTick();CHECK(robotActions==2);CHECK(action=="WAIT_STOP");CHECK(!robotSlowFlash);
 // WAIT_CHASE uses the same absence threshold and holds past its observation
 // deadline while confirming absence; a brief miss and reappearance reset it.
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";config["catLeaveSeconds"]=2;config["catObserveTimeoutSeconds"]=1;
 freshCat(.4);behavior("WAIT_CHASE");freshCat(.4,false);policyTick();auto chaseMissAt=farAt;
 tick+=500;freshCat(.4);policyTick();CHECK(!farAt);CHECK(action=="WAIT_CHASE");
 freshCat(.4,false);policyTick();chaseMissAt=farAt;
 for(int n=1;n<=3;n++){tick=chaseMissAt+n*500;freshCat(.4,false);policyTick();CHECK(action=="WAIT_CHASE");CHECK(!motorId);}
 tick=chaseMissAt+1999;freshCat(.4,false);policyTick();CHECK(phase=="INTERACTION");
 tick=chaseMissAt+2000;freshCat(.4,false);policyTick();CHECK(phase=="ENDING");CHECK(interactionExitReason=="cat absent for configured leave interval");
 // Late invalid frames get their own full unavailable timeout; they do not
 // inherit time spent successfully observing. Recovery clears that timer.
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";config["catObserveTimeoutSeconds"]=30;freshCat(.4);behavior("WAIT_CHASE");
 tick+=5000;freshCat(.4,false,false);policyTick();CHECK(action=="WAIT_CHASE");auto invalidAt=chaseUnavailableAt;
 tick+=2999;freshCat(.4,false,false);policyTick();CHECK(action=="WAIT_CHASE");CHECK(chaseUnavailableAt==invalidAt);
 freshCat(.4);policyTick();CHECK(!chaseUnavailableAt);freshCat(.4,false,false);policyTick();invalidAt=chaseUnavailableAt;
 tick+=3000;freshCat(.4,false,false);policyTick();CHECK(phase=="ENDING");CHECK(interactionExitReason=="cat observation unavailable");
 // A new attack during absence still wins immediately over departure waiting.
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";branch=1;freshCat(.4);behavior("WAIT_CHASE");freshCat(.4,false);policyTick();onTouch(0);policyTick();CHECK(action=="WAIT_RELEASE");CHECK(!farAt);
 // After finding a cat, absence never overrides the robot action rounds.
 for(const char *first:{"OBSERVE","STAY_SIGNAL"}){
  fixture();policyControl("alert");stopped();config["robotActionLimit"]=5;config["catLeaveSeconds"]=1;behavior(first);
  for(int n=1;n<=5;n++){
   freshCat(.4,false);tick=due;freshCat(.4,false);policyTick();CHECK(robotActions==n);
   if(n==5){CHECK(phase=="ENDING");CHECK(interactionExitReason=="robot action limit reached");break;}
   CHECK(phase=="INTERACTION");
   if(action=="WAIT_STOP"){stopped();settled();frame(false);policyTick();CHECK(action=="OBSERVE");}
   else{CHECK(action=="FOLLOW_WAIT");tick=due;freshCat(.4,false);policyTick();CHECK(action=="OBSERVE");}
   CHECK(!motorId&&!scanId);CHECK(robotActions==n);
  }
 }
 // Losing the target after a movement does not count an unfinished movement,
 // scan indefinitely, or start another blind DRIVE. Remaining rounds observe.
 fixture();policyControl("alert");stopped();behavior("SLOW_APPROACH");policyTick();md();stopped();settled();frame(false,true,30);policyTick();CHECK(action=="OBSERVE");CHECK(phase=="INTERACTION");CHECK(robotActions==0);CHECK(!motorId);
 fixture();policyControl("alert");stopped();behavior("FAST_APPROACH");freshCat(.1,false);policyTick();CHECK(action=="OBSERVE");CHECK(!motorId&&!scanId);CHECK(!robotFast);
 fixture();policyControl("alert");stopped();behavior("PROBE_APPROACH");freshCat(.1,false);policyTick();CHECK(action=="OBSERVE");CHECK(!motorId);
 // New settings reject fractional budgets; existing saved fields retain values.
 fixture();CHECK(config["robotActionLimit"].as<int>()==5);
 // Repeated touch never redraws branch, first direction, or interaction identity.
 fixture();draws={99,0,99};onTouch(0,100);stopped();releaseAll();auto interaction=interactionId;auto originalBranch=branch;auto originalEscape=escape;
 for(int a:{1,2,3,0}){draws={42};onTouch(a,200);CHECK(interactionId==interaction);CHECK(firstTouch==0);CHECK(rounds==1);CHECK(branch==originalBranch);CHECK(escape==originalEscape);CHECK(draws.size()==1);releaseAll();}
 // First attack during an invitation uses the same plan as patrol contact.
 for(const char *previous:{"OBSERVE","CAT_DRIVE","SEARCH_CAT_ACTIVE","WAIT_TOUCH_RESUME"}){
  fixture();policyControl("alert");stopId=0;state(previous);auto robot=interactionId;
  if(String(previous)=="CAT_DRIVE")drive(10,95,"CAT_DRIVE");
  if(String(previous)=="SEARCH_CAT_ACTIVE"){scanId=motorId=123;scanUsed=10;}
  if(String(previous)=="WAIT_TOUCH_RESUME")touchPaused=true;
  draws={99};onTouch(1,200);CHECK(initiator=="CAT_TOUCH");CHECK(interactionId==robot+1);CHECK(branch==-1);
  CHECK(firstTouch==1);CHECK(firstTouchUnoAt==200);CHECK(rounds==1);CHECK(!touchPaused);CHECK(!motorId&&!scanId);
  CHECK(action=="WAIT_STOP");CHECK(afterStop=="WAIT_RELEASE");stopped();releaseAll();touchFound(0);CHECK(action=="PREY_WAIT");CHECK(branch==2);
 auto attacked=interactionId;onTouch(0,300);CHECK(interactionId==attacked);CHECK(firstTouch==1);CHECK(branch==2);
 }
 // A complete invitation ends after observation + decision wait, without a
 // second probability draw, even if the cat remains in frame indefinitely.
 for(const char *invitation:{"OBSERVE","STAY","STAY_SIGNAL"}){
  fixture();config["robotActionLimit"]=1;routeIndex=1;needRearm=true;policyControl("alert");stopped();resetMotor();scanId=0;
  behavior(invitation);if(action=="SIGNAL"){tick=due;freshCat(.4);policyTick();CHECK(phase=="ENDING");endAndResumePatrol();continue;}
  tick=due;freshCat(.4);policyTick();CHECK(phase=="ENDING");
  CHECK(interactionExitReason=="robot action limit reached");
  scanDirection=-1;alignSteps=8;endAndResumePatrol();CHECK(routeIndex==1);CHECK(needRearm);CHECK(action=="TAG_REARM");CHECK(scanDirection==1);CHECK(alignSteps==0);
 }
 // Independent alert resets stale patrol progress and starts the saved route.
 for(const char *initial:{"IDLE","STOPPED"}){
  fixture();config["robotActionLimit"]=1;phase=initial;routeIndex=1;needRearm=true;arrivalFrame=99;policyControl("alert");CHECK(returnToPatrol);CHECK(routeIndex==0);CHECK(!needRearm);CHECK(!arrivalFrame);
  policyInput("DONE "+String(sessionId)+" MOTOR_STOPPED");policyInput("DONE "+String(sessionId)+" GEOMETRY_SET");
  policyInput("DATA "+String(sessionId)+" CONFIG WHEEL_MM=67.5 TRACK_MM=120.0 SLOTS_PER_REV=20 SAFE_LEFT_CM=1.0 SAFE_RIGHT_CM=1.0");
  policyInput("DONE "+String(sessionId)+" CONFIG");policyInput("DONE "+String(sessionId)+" SESSION_STARTED");CHECK(phase=="INTERACTION");
  stopped();behavior("OBSERVE");tick=due;freshCat(.4);policyTick();endAndResumePatrol();CHECK(action=="SCAN_TAG");CHECK(policyTargetId()==9);
 }
 // Checked no-cat scan completion also returns to the interrupted target.
 fixture();routeIndex=1;policyControl("alert");stopped();policyTick();configDone();auto failedScan=scanId,failedGen=generation;
 policyInput("SD "+String(failedScan)+" "+String(failedGen)+" 0");CHECK(catFailure.length()>0);endAndResumePatrol();CHECK(routeIndex==1);CHECK(policyTargetId()==10);
 // Explicit direction node -> turn -> checked stop -> actual DRIVE, no camera turn loop.
 fixture();config["escapePreyPercent"]=0;draws={99};onTouch(0);stopped();releaseAll();touchFound(99);CHECK(action=="ESCAPE_DIRECTION");policyTick();CHECK(action=="ESCAPE_TURN");CHECK(commands.back().text.startsWith("ROTATE "));
 md(18,18);stopped();policyTick();CHECK(action=="ESCAPE_MOVE");CHECK(commands.back().text=="DRIVE 23.00 140");CHECK(escapeRounds==0);
 JsonDocument snapshot;policySnapshot(snapshot);CHECK(snapshot["patrolStatus"]["escapeStage"].as<String>()=="RUN");
 // TOP search found -> face cat -> select away direction -> 180-degree turn -> DRIVE.
 fixture();config["escapePreyPercent"]=0;draws={99};onTouch(3);stopped();releaseAll();touchFound(99);CHECK(action=="ESCAPE_DIRECTION");policyTick();CHECK(action=="ESCAPE_TURN");CHECK(turnDeg==180);md(18,18);stopped();policyTick();CHECK(action=="ESCAPE_MOVE");
 // Partial DRIVE contact abort uses checked MA progress, resumes remaining cm once.
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";branch=1;config["escapePreyPercent"]=0;drive(23,95,"ESCAPE_MOVE");auto oldMotor=motorId;
 policyInput("MA "+String(oldMotor)+" TOUCH 5 5");CHECK(touchPaused);CHECK(action=="WAIT_STOP");float left=resumeCm;CHECK(left>17&&left<18);onTouch(0);stopped();CHECK(action=="WAIT_TOUCH_RESUME");policyTick();CHECK(!motorId);releaseAll();policyTick();CHECK(action=="ESCAPE_MOVE");CHECK(driveCm==left);CHECK(escapeRounds==0);
 md(17,17);stopped();policyTick();CHECK(escapeRounds==1);CHECK(action=="WAIT_STOP");policyInput("MD "+String(oldMotor)+" 23 23");CHECK(escapeRounds==1);
 // Scan aborted while turning retains used angle; old replies are ignored.
 fixture();phase="INTERACTION";beginScan("CAT","OBSERVE");configDone();auto oldScan=scanId,oldGeneration=generation;scanUsed=10;
 policyInput("MS "+String(oldScan)+" 0 0");policyInput("MA "+String(oldScan)+" TOUCH 2 2");CHECK(scanUsed==12);onTouch(1);stopped();releaseAll();policyTick();CHECK(action=="WAIT_SCAN_CONFIG");CHECK(scanOrigin==12);configDone();CHECK(commands.back().text.endsWith(" 59 0"));policyInput("SD "+String(oldScan)+" "+String(oldGeneration)+" 4");CHECK(scanId!=0);CHECK(phase=="INTERACTION");
 // A vision-wait abort must not double count the already reported VQ progress.
 fixture();phase="INTERACTION";beginScan("CAT","OBSERVE");configDone();scanRequest();CHECK(scanUsed==2);onTouch(1);stopped();releaseAll();policyTick();CHECK(scanOrigin==2);configDone();CHECK(commands.back().text.endsWith(" 69 0"));
 // Escape prey remains configured; after prey, search/center rather than ending.
 fixture();phase="INTERACTION";config["escapePreyPercent"]=0;
 for(int n=1;n<=5;n++){state("POST_ESCAPE");policyTick();CHECK(escapeRounds==n);CHECK(n==5?action=="PREY_WAIT":action=="WAIT_STOP");if(n<5){CHECK(afterFace=="CHASE_ALIGN");stopId=0;resetMotor();}}
 fixture();phase="INTERACTION";config["escapePreyPercent"]=100;state("POST_ESCAPE");policyTick();CHECK(action=="PREY_WAIT");CHECK(escapeRounds==1);
 // Exploration probability is sampled once per completed cycle, after minimum time.
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";branch=0;config["explorePreyPercent"]=100;draws={0};signal(true);auto began=exploreAt;tick=due;freshCat(.3);draws={0};policyTick();CHECK(exploreRounds==1);CHECK(action=="OBSERVE");CHECK(draws.size()==1);
 stopId=0;tick=began+30000;signal(true);auto explorationId=interactionId;onTouch(2);CHECK(exploreAt==began);CHECK(interactionId==explorationId);releaseAll();tick=due;freshCat(.3);draws={0};policyTick();CHECK(action=="PREY_WAIT");CHECK(exploreRounds==2);CHECK(draws.empty());
 auto preyDue=due;onTouch(1);CHECK(due==preyDue);CHECK(action=="PREY_WAIT");releaseAll();tick=preyDue;freshCat(.4);policyTick();CHECK(phase=="INTERACTION");CHECK(action=="WAIT_STOP");CHECK(afterFace=="CHASE_ALIGN");CHECK(lastFault!="MAX_WAIT_UNCONFIRMED: manual resume required");
 // Stationary prey absence requires new valid frames, and contact/invalid reset it.
 fixture();phase="INTERACTION";prey(false);freshCat(.1,false);policyTick();tick+=1000;freshCat(.1,false,false);policyTick();CHECK(!farAt);
 for(int n=0;n<=5;n++){tick+=1000;freshCat(.1,false);policyTick();}CHECK(phase=="INTERACTION");CHECK(action=="WAIT_STOP");CHECK(afterFace=="CHASE_ALIGN");CHECK(!scanId);
 fixture();phase="INTERACTION";prey(false);freshCat(.1,false);policyTick();tick+=1000;onTouch(2);CHECK(!farAt);CHECK(action=="PREY_WAIT");
 // Chase is not merely seeing a stationary cat: a near transition or new contact wins.
 fixture();phase="INTERACTION";freshCat(.4);behavior("WAIT_CHASE");auto chaseDue=due;tick=chaseDue;freshCat(.4);policyTick();CHECK(phase=="ENDING");CHECK(interactionExitReason=="chase wait timeout");
 fixture();phase="INTERACTION";freshCat(.2);behavior("WAIT_CHASE");freshCat(.4);policyTick();CHECK(action=="WAIT_RELEASE");CHECK(touchNext=="ESCAPE");
 fixture();phase="INTERACTION";freshCat(.4);behavior("WAIT_CHASE");onTouch(0);CHECK(chaseDetected);policyTick();CHECK(action=="WAIT_RELEASE");CHECK(touchNext=="ESCAPE");
 // Exploration inactivity causes a search; session duration alone is not a fault.
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";branch=0;sessionAt=lastTouch=tick-120000;hold("FOLLOW_WAIT",0);freshCat(.4);policyTick();CHECK(action=="WAIT_STOP");CHECK(afterFace=="CHASE_ALIGN");CHECK(phase=="INTERACTION");
 // Far/middle/near boundaries and first middle observation.
 fixture();freshCat(.2);CHECK(distanceClass()==-1);freshCat(.12);CHECK(distanceClass()==0);freshCat(.2);CHECK(distanceClass()==0);freshCat(.35);CHECK(distanceClass()==1);freshCat(.2);CHECK(distanceClass()==1);freshCat(.2,false);CHECK(distanceClass()==-1);
 // SETTLE gets prompt samples but never skips the user-specified minimum wait.
 fixture();phase="INTERACTION";config["scanMinimumMs"]=1000;settle("CAT_LOCATED");CHECK(policyTelemetryInterval()==250);
 tick+=250;encoderAt[0]=encoderAt[1]=tick;policyTick();CHECK(action=="SETTLE");tick+=250;encoderAt[0]=encoderAt[1]=tick;policyTick();CHECK(action=="SETTLE");tick+=500;encoderAt[0]=encoderAt[1]=tick;policyTick();CHECK(action=="CAT_CHECK");CHECK(policyTelemetryInterval()==2000);
 // Operator stop cancels a contact continuation and never resumes old movement.
 fixture();phase="INTERACTION";drive(23,95,"ESCAPE_MOVE");onTouch(0);CHECK(touchPaused);policyControl("stop");releaseAll();policyTick();CHECK(phase=="STOPPED");CHECK(!touchPaused);CHECK(!motorId);
 // Contact feedback runs alongside scanning, including a fast release before
 // its queued output had a chance to dispatch. No behavioral branch is chosen.
 fixture();draws={0};onTouch(3);stopped();releaseAll();auto cueUntil=touchSignalUntil;policyTick();CHECK(action=="WAIT_SCAN_CONFIG");CHECK(branch==-1);CHECK(touchSignalUntil==cueUntil);
 bool ledQueued=false;for(auto &c:commands)if(c.text.startsWith("LED FLASH "))ledQueued=true;CHECK(ledQueued);configDone();CHECK(action=="SEARCH_CAT_ACTIVE");CHECK(touchSignalUntil==cueUntil);
 policyControl("stop");CHECK(touchSignalUntil==0);CHECK(!touchCue);
 // Off-center detection cannot start an alignment loop before/after review.
 fixture();draws={99};onTouch(1);stopped();releaseAll();touchFound(0);CHECK(action=="PREY_WAIT");CHECK(branch==2);CHECK(!motorId);
 // A failed fresh review resumes the remaining scan budget; it cannot select
 // prey/exploration/escape or reuse the initial positive frame.
 fixture();draws={99};onTouch(0);stopped();releaseAll();policyTick();configDone();scanRequest();frame(true);auto reviewScan=scanId,reviewGen=generation;
 policyInput("SD "+String(scanId)+" "+String(generation)+" 1");completeConfirmation(false);CHECK(action=="WAIT_SCAN_CONFIG");CHECK(scanOrigin==2);CHECK(branch==-1);
 policyInput("SD "+String(reviewScan)+" "+String(reviewGen)+" 1");CHECK(action=="WAIT_SCAN_CONFIG");
 // Missing encoder evidence and missing new image both exit safely.
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";confirmCat("CHOOSE_TOUCH");stopped();tick+=6000;policyTick();CHECK(phase=="ENDING");CHECK(!motorId);
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";confirmCat("CHOOSE_TOUCH");stopped();tick+=250;encoderAt[0]=encoderAt[1]=tick;policyTick();tick+=250;encoderAt[0]=encoderAt[1]=tick;policyTick();tick=due;policyTick();CHECK(checkPending);tick+=3000;policyTick();CHECK(phase=="ENDING");CHECK(!motorId);
 printf("continuous cat interaction: %u checks passed\n",checks);
}
