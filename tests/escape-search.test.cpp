// Exercise the real ESP policy and command/vision transaction harness.
#define main existingPolicyChecks
#include "policy-core.test.cpp"
#undef main

void escapeFixture(){fixture();config["frontAvoidanceEnabled"]=true;phase="INTERACTION";initiator="CAT_TOUCH";branch=1;turnDeg=180;state("ESCAPE_DIRECTION");}
void nextQuarter(){CHECK(action=="WAIT_STOP");escapeSettle("ESCAPE_SIDE_CHECK");escapeSideSample();CHECK(action=="ESCAPE_TURN");CHECK(turnDeg==90);}
int main(){
 // A cat/block in the old forward direction does not prevent a side-gated turn.
 escapeFixture();observation["front"]="BLOCKED";startEscapeTurn();CHECK(escapeTurns==1);CHECK(policyVisionMode()=="FLOOR");
 md(9,9);escapeSettle("ESCAPE_FRONT_CHECK");CHECK(policyVisionMode()=="FLOOR");
 // Real VisionGate has no FLOOR ticket support: use continuous results, not a request.
 CHECK(!cameraGate.pending);CHECK(!checkPending);for(auto &r:requests)CHECK(r.mode!="FLOOR");
 // No repeated frame or capture from before the settled barrier may authorize DRIVE.
 auto seq=escapeObserveFrame;observation["generation"]=generation;observation["scanId"]=0;observation["stepId"]=checkStep;
 observation["mode"]="FLOOR";observation["frameSeq"]=seq;observation["at"]=tick+1;policyVision();policyTick();CHECK(action=="ESCAPE_FRONT_CHECK");CHECK(!motorId);
 observation["frameSeq"]=seq+1;observation["at"]=escapeObserveAt;policyVision();policyTick();CHECK(action=="ESCAPE_FRONT_CHECK");CHECK(!motorId);
 // A new CAT/TAGS observation cannot stand in for the required continuous FLOOR frame.
 observation["at"]=tick+1;observation["mode"]="CAT";policyTick();CHECK(action=="ESCAPE_FRONT_CHECK");
 observation["mode"]="TAGS";policyTick();CHECK(action=="ESCAPE_FRONT_CHECK");
 escapeFrontFrame("BLOCKED");CHECK(!motorId);nextQuarter();CHECK(escapeTurns==2);
 finishEscapeTurn();CHECK(escapeTargetCm==23);CHECK(escapeRemainingCm==23);
 CHECK(commands.back().text=="DRIVE 23.00 140");CHECK(policyVisionMode()=="FLOOR");
 // Rotation permission is independent even of invalid, stale or uncalibrated front input.
 for(int kind=0;kind<3;kind++){
   escapeFixture();CHECK(policyVisionMode()=="FLOOR");
   if(kind==0)observation["front"]="INVALID";
   if(kind==1)observation["at"]=tick-1500;
   if(kind==2)config["frontCalibrated"]=false;
   startEscapeTurn();CHECK(turnDeg==90);CHECK(escapeTurns==1);
 }
 // The threshold is strictly exceeded; equality and unknown sonar cannot authorize rotation.
 escapeFixture();policyTick();escapeSettle("ESCAPE_SIDE_CHECK");escapeSideSample(15,15);CHECK(phase=="ENDING");CHECK(!motorId);
 escapeFixture();config["escapeTurnClearanceCm"]=25;policyTick();escapeSettle("ESCAPE_SIDE_CHECK");escapeSideSample(20,20);CHECK(phase=="ENDING");
 escapeFixture();safe[1]=30;startEscapeTurn(20,20);CHECK(turnDeg==-90);CHECK(escapeDirection==-1);
 escapeFixture();policyTick();escapeSettle("ESCAPE_SIDE_CHECK");side[0]=side[1]=-1;tick+=2500;policyTick();CHECK(phase=="ENDING");CHECK(!motorId);
 // Old side measurements cannot authorize a new quarter-turn, even if still under 2.5 seconds old.
 escapeFixture();policyTick();escapeSettle("ESCAPE_SIDE_CHECK");policyTick();CHECK(action=="ESCAPE_SIDE_CHECK");CHECK(!motorId);
 // Three alternative headings maximum; no fourth/repeated turn or forward probing.
 escapeFixture();startEscapeTurn();
 for(int n=1;n<=3;n++){
   CHECK(escapeTurns==n);md(9,9);escapeSettle("ESCAPE_FRONT_CHECK");escapeFrontFrame("BLOCKED");
   if(n<3)nextQuarter();else{CHECK(phase=="ENDING");CHECK(!motorId);}
 }
 for(auto &c:commands)CHECK(!c.text.startsWith("DRIVE "));
 // Unknown/uncalibrated vision stops, instead of searching on an invented CLEAR.
 for(const char *front:{"INVALID","UNAVAILABLE"}){escapeFixture();startEscapeTurn();md(9,9);escapeSettle("ESCAPE_FRONT_CHECK");escapeFrontFrame(front);CHECK(phase=="ENDING");}
 escapeFixture();startEscapeTurn();md(9,9);escapeSettle("ESCAPE_FRONT_CHECK");config["frontCalibrated"]=false;escapeFrontFrame();CHECK(phase=="ENDING");
 // During the continuous run, BLOCKED stops first and preserves checked remaining travel.
 escapeFixture();startEscapeTurn();finishEscapeTurn();auto target=escapeTargetCm;
 pulsesL=pulsesR=5;observation["front"]="BLOCKED";draws={123};policyTick();CHECK(action=="WAIT_STOP");CHECK(!motorId);CHECK(escapeRemainingCm>17&&escapeRemainingCm<18);
 nextQuarter();finishEscapeTurn();CHECK(escapeTurns==2);CHECK(escapeTargetCm==target);CHECK(driveCm>17&&driveCm<18);CHECK(draws.size()==1);
 // With the front switch off, no FLOOR frame or calibration is needed to start or continue escape.
 for(int kind=0;kind<6;kind++){
   escapeFixture();config["frontAvoidanceEnabled"]=false;
   if(kind==0)observation["front"]="BLOCKED";
   if(kind==1)observation["front"]="INVALID";
   if(kind==2)observation["at"]=tick-1500;
   if(kind==3)config["frontCalibrated"]=false;
   if(kind==4)observation["frontCalibrated"]=false;
   if(kind==5)observation["frameSeq"]=0;
   startEscapeTurn();CHECK(policyVisionMode()=="OFF");
   md(9,9);escapeSettle("ESCAPE_FRONT_CHECK");policyTick();CHECK(action=="ESCAPE_DRIVE");
   CHECK(escapeFrontClear());policyTick();CHECK(action=="ESCAPE_MOVE");CHECK(policyVisionMode()=="OFF");
   auto driveId=motorId;policyTick();CHECK(phase=="INTERACTION");CHECK(action=="ESCAPE_MOVE");CHECK(motorId==driveId);
 }
 // The disabled front switch still requires side clearance before rotating.
 escapeFixture();config["frontAvoidanceEnabled"]=false;policyTick();escapeSettle("ESCAPE_SIDE_CHECK");escapeSideSample(15,15);CHECK(phase=="ENDING");CHECK(!motorId);
 // Turn interruption ends this round; it must not silently resume toward an uncertain heading.
 for(bool enabled:{true,false}){escapeFixture();config["frontAvoidanceEnabled"]=enabled;startEscapeTurn();onTouch(0);stopped();touch[0]=false;policyTick();CHECK(phase=="ENDING");CHECK(!motorId);}
 // Missing continuous frames still time out without sending DRIVE.
 escapeFixture();startEscapeTurn();md(9,9);escapeSettle("ESCAPE_FRONT_CHECK");tick+=3000;policyTick();CHECK(phase=="ENDING");
 // New settings are bounded and saved configurations obtain the missing default by overlay.
 fixture();config["wheelMm"]=67.5;config["trackMm"]=120;config["frontBlockBottom"]=.88;config["floorRoiTop"]=.75;config["floorDifferenceThreshold"]=35;config["floorReference"].to<JsonArray>();String error;CHECK(config["escapeTurnClearanceCm"].as<float>()==15);
 for(float good:{1.f,15.f,100.f}){config["escapeTurnClearanceCm"]=good;if(!validateSettings(config,error)){fprintf(stderr,"settings error: %s\n",error.c_str());CHECK(false);}}
 for(float bad:{0.f,101.f,NAN,INFINITY}){config["escapeTurnClearanceCm"]=bad;CHECK(!validateSettings(config,error));}
 printf("escape quarter search: %u checks passed\n",checks);
}
