#define main originalPolicyMain
#include "policy-core.test.cpp"
#undef main
String bootId="boot";
uint32_t debugId=0,manualUntil=0,stopPendingId=0;
int configStep=-1;bool stopUnconfirmed=false;
struct Outgoing {String wire;};std::deque<Outgoing> outgoing;
void debugCancel(){debugId=manualUntil=0;configStep=-1;}
#include "../esp32-host/ESP32CatHost/remote_control.h"
JsonDocument packet(const char *op,uint32_t serial){JsonDocument d;d["action"]=op;d["client"]="phone";d["session"]=RemoteControl::token;d["seq"]=serial;return d;}
void reply(const char *suffix){RemoteControl::accept("DONE "+String(RemoteControl::pending)+" "+suffix);}
void enter(bool compatible=true){
 fixture();phase="STOPPED";RemoteControl::stage=RemoteControl::OFF;RemoteControl::error="";outgoing.clear();
 JsonDocument d;d["action"]="enter";d["client"]="phone";RemoteControl::input(d);CHECK(RemoteControl::stage==RemoteControl::ENTER_STOP);CHECK(phase=="REMOTE");
 reply("MOTOR_STOPPED");CHECK(RemoteControl::stage==RemoteControl::ENTER_LOCK);reply("SESSION_STOPPED");CHECK(RemoteControl::stage==RemoteControl::CONFIG);
 RemoteControl::accept("DATA "+String(RemoteControl::pending)+" CONFIG WHEEL_MM=67.5 TRACK_MM=120 SLOTS_PER_REV=20 SAFE_LEFT_CM=1 SAFE_RIGHT_CM=1"+(compatible?" REMOTE_V1":""));
 RemoteControl::accept("DONE "+String(RemoteControl::pending+1));CHECK(RemoteControl::stage==RemoteControl::CONFIG);
 RemoteControl::accept("DONE "+String(RemoteControl::pending));if(compatible){CHECK(RemoteControl::stage==RemoteControl::START);reply("SESSION_STARTED");CHECK(RemoteControl::stage==RemoteControl::ACTIVE);}
}
int main(){
 enter(false);CHECK(RemoteControl::stage==RemoteControl::OFF&&phase=="STOPPED");CHECK(RemoteControl::error.indexOf("REMOTE_V1")>=0);
 enter();auto token=RemoteControl::token;auto d=packet("move",1);d["direction"]="forward";d["pwm"]=35;RemoteControl::input(d);CHECK(RemoteControl::left==35&&commands.back().text=="TEST_MOTOR 35 35 600 REMOTE");
 auto id=RemoteControl::motorId;RemoteControl::accept("AK "+String(id));CHECK(RemoteControl::motorConfirmed);
 auto previous=commands.size();d["direction"]="backward";RemoteControl::input(d);CHECK(commands.size()==previous&&RemoteControl::left==35);d["seq"]=2u;d["client"]="other";RemoteControl::input(d);CHECK(RemoteControl::left==35);
 d=packet("move",2);d["direction"]="right";d["pwm"]=70;RemoteControl::input(d);CHECK(RemoteControl::left==70&&RemoteControl::right==-70);
 d=packet("beep_hold",3);d["held"]=true;RemoteControl::input(d);CHECK(RemoteControl::beepHeld);d=packet("quiet",4);RemoteControl::input(d);CHECK(!RemoteControl::beepHeld&&RemoteControl::left==70);
 tick+=601;RemoteControl::tick();CHECK(RemoteControl::stage==RemoteControl::OFF&&phase=="STOPPED");d=packet("move",5);d["session"]=token;RemoteControl::input(d);CHECK(RemoteControl::stage==RemoteControl::OFF);
 enter();d=packet("move",1);d["direction"]="left";d["pwm"]=80;RemoteControl::input(d);d=packet("move_stop",2);RemoteControl::input(d);CHECK(RemoteControl::stage==RemoteControl::MOVE_STOP);reply("MOTOR_STOPPED");reply("SESSION_STARTED");CHECK(RemoteControl::stage==RemoteControl::ACTIVE&&RemoteControl::left==0);
 d=packet("sound",3);d["pattern"]=2;RemoteControl::input(d);RemoteControl::tick();CHECK(commands.back().text=="BEEP 180");tick+=800;RemoteControl::tick();CHECK(commands.back().text=="BEEP 180");d=packet("beep_hold",4);d["held"]=true;RemoteControl::input(d);CHECK(RemoteControl::sound==0);d=packet("beep_hold",5);d["held"]=false;RemoteControl::input(d);CHECK(commands.back().text=="BEEP 0");
 d=packet("light",6);RemoteControl::input(d);int first=RemoteControl::lastLight;d=packet("light",7);RemoteControl::input(d);CHECK(RemoteControl::lastLight!=first);
 d.clear();d["action"]="resume_from";d["index"]=1;d["route"]=config["route"];RemoteControl::patrol(d);CHECK(RemoteControl::stage==RemoteControl::EXIT_STOP);reply("MOTOR_STOPPED");reply("SESSION_STOPPED");CHECK(phase=="STARTING"&&routeIndex==1);CHECK(action=="WAIT_PATROL_STOP"&&RemoteControl::stage==RemoteControl::OFF);
 fixture();phase="STOPPED";d["index"]=5;RemoteControl::patrol(d);CHECK(phase=="STOPPED");d["index"]=0;d["route"][0]=99;RemoteControl::patrol(d);CHECK(phase=="STOPPED");
 policyResumeFrom(1);CHECK(routeIndex==1&&phase=="STARTING");phase="STOPPED";policyResumeFrom(-1);CHECK(phase=="STOPPED"&&routeIndex==1);stopGate=false;policyResumeFrom(0);CHECK(routeIndex==1);stopGate=true;
 fixture();phase="STARTING";action="WAIT_PATROL_CONFIG";sessionId=555;geometryConfirmed=false;policyInput("DATA 555 CONFIG WHEEL_MM=67.5 TRACK_MM=120 SLOTS_PER_REV=20 SAFE_LEFT_CM=1 SAFE_RIGHT_CM=1 REMOTE_V1");CHECK(geometryConfirmed);
 geometryConfirmed=false;policyInput("DATA 555 CONFIG WHEEL_MM=67.5 TRACK_MM=120 SLOTS_PER_REV=20 SAFE_LEFT_CM=1 SAFE_RIGHT_CM=1 REMOTE_V1 EXTRA");CHECK(!geometryConfirmed);
 enter();tick+=1501;RemoteControl::tick();CHECK(RemoteControl::stage==RemoteControl::OFF);enter();stopUnconfirmed=true;RemoteControl::tick();CHECK(RemoteControl::stage==RemoteControl::OFF);stopUnconfirmed=false;
 enter();RemoteControl::accept("READY UNO_CAT_V6.13");CHECK(RemoteControl::stage==RemoteControl::OFF&&!RemoteControl::capable);
 printf("actual remote handoffs, leases, compatibility and resume: %u checks passed\n",checks);
}
