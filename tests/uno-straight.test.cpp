#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <cctype>
#include <string>
#include <algorithm>
#include "../firmware/UnoCatController/straight_balance.h"
#include "../firmware/UnoCatController/command_hex.h"
#include "../firmware/UnoCatController/command_integer.h"
struct __FlashStringHelper {};
#define F(s) reinterpret_cast<const __FlashStringHelper*>(s)
#define PSTR(s) s
void snprintf_P(char *buffer,size_t,const char*,...){buffer[0]=0;}
uint32_t clockMs=0,counts[2]={};int16_t output[2]={};bool boardDryRun=false,sessionStopped=false,touch=false,motionBypassAvoidance=false;
enum {MOTOR_LEFT,MOTOR_RIGHT,ENCODER_LEFT,ENCODER_RIGHT};bool physicalIo[4]={true,true,true,true};
constexpr uint32_t ENCODER_STALL_MS=1200;constexpr int SONAR_IDLE=0;int sonarPhase=0;uint32_t motionFeedbackAtMs=0;
StraightBalance straightBalance;const __FlashStringHelper *safety=nullptr;
bool receiveWindow=false;
bool rxWindowActive(){return receiveWindow;}
struct {int availableForWrite(){return 0;}} Serial;
struct ScanMachine {enum {ABORTED};uint16_t id=0;bool active(){return false;}void end(int){}void motionStopped(uint32_t,uint32_t,uint32_t){}} scan;
uint32_t millis(){return clockMs;}bool anyTouchPressed(){return touch;}
const __FlashStringHelper *motionSafetyReason(int16_t,int16_t,uint32_t,bool){return safety;}
void error(uint16_t,const __FlashStringHelper*){}void ack(uint16_t){}void printProtectedLine(const char*){}
bool deadlineReached(uint32_t now,uint32_t due){return int32_t(now-due)>=0;}
void getPulses(uint32_t &l,uint32_t &r){l=counts[0];r=counts[1];}void applyMotors(int16_t l,int16_t r){output[0]=l;output[1]=r;}
// Extracted actual firmware functions; globals are declared before those bodies.
#define motion motionRecord
// The generator emits the actual Motion declaration before the functions.
#include UNO_MOTION_SOURCE
unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
void begin(MotionKind kind,int16_t l,int16_t r,uint32_t target=100){stopOutputs();motion.kind=MOTION_NONE;clockMs=0;counts[0]=counts[1]=0;safety=nullptr;startMotion(1,kind,l,r,10000,target,false,F("TEST"));}
int main(){
 begin(MOTION_CONTINUOUS,100,100);CHECK(straightBalance.enabled);counts[0]=3;counts[1]=1;clockMs=199;tickMotion(clockMs);CHECK(output[0]==100);clockMs=200;tickMotion(clockMs);CHECK(output[0]==98&&output[1]==100&&motion.left==100);
 for(int i=2;i<=10;i++){clockMs=i*200;counts[0]=i*3;counts[1]=i;tickMotion(clockMs);}CHECK(straightBalance.reduction==20&&output[0]==80&&output[1]==100);
 counts[1]=counts[0]+20;clockMs+=200;tickMotion(clockMs);CHECK(straightBalance.reduction==18&&output[1]==100);for(int i=0;i<9;i++){clockMs+=200;counts[0]++;counts[1]++;tickMotion(clockMs);}CHECK(straightBalance.reduction==0&&output[0]==100&&output[1]==100);clockMs+=200;counts[0]++;counts[1]++;tickMotion(clockMs);CHECK(output[1]==98&&output[0]==100);
 stopOutputs();CHECK(!straightBalance.enabled&&straightBalance.reduction==0&&output[0]==0&&output[1]==0);
 begin(MOTION_TIMED,-100,-100);counts[0]=1;counts[1]=3;clockMs=200;tickMotion(clockMs);CHECK(output[0]==-100&&output[1]==-98);clockMs=10000;counts[0]++;counts[1]++;tickMotion(clockMs);CHECK(motion.kind==MOTION_NONE&&output[0]==0&&output[1]==0);
 for(auto pair:{std::pair<int,int>{100,-100},{-100,100},{100,90},{100,0},{0,100}}){begin(MOTION_CONTINUOUS,pair.first,pair.second);CHECK(!straightBalance.enabled);clockMs=200;counts[0]=3;counts[1]=1;tickMotion(clockMs);CHECK(output[0]==pair.first&&output[1]==pair.second);}
 for(int i=0;i<4;i++){physicalIo[i]=false;begin(MOTION_CONTINUOUS,100,100);CHECK(!straightBalance.enabled);physicalIo[i]=true;}
 boardDryRun=true;begin(MOTION_CONTINUOUS,100,100);CHECK(!straightBalance.enabled);boardDryRun=false;
 stopOutputs();motion.kind=MOTION_NONE;startMotion(1,MOTION_TIMED,100,100,1000,0,false,F("TEST"),false,true);CHECK(!straightBalance.enabled);abortMotion(F("STOP"));
 begin(MOTION_PULSES,100,100,3);counts[0]=3;counts[1]=1;clockMs=200;tickMotion(clockMs);CHECK(output[0]==0&&output[1]==100);clockMs=400;counts[1]=2;tickMotion(clockMs);CHECK(output[0]==0);counts[1]=3;clockMs=600;tickMotion(clockMs);CHECK(motion.kind==MOTION_NONE&&output[0]==0&&output[1]==0);
 begin(MOTION_CONTINUOUS,100,100);safety=F("TOUCH");receiveWindow=true;clockMs=200;tickMotion(clockMs);CHECK(motion.kind==MOTION_NONE&&output[0]==0&&!straightBalance.enabled);safety=nullptr;receiveWindow=false;
 begin(MOTION_CONTINUOUS,100,100);clockMs=1201;tickMotion(clockMs);CHECK(motion.kind==MOTION_NONE&&output[0]==0);
 begin(MOTION_TIMED,100,100);abortMotion(F("STOP_COMMAND"));CHECK(!straightBalance.enabled&&straightBalance.reduction==0);
 // Remote renewals preserve stall detection and bypass only environmental gates.
 stopOutputs();motion.kind=MOTION_NONE;sessionStopped=false;touch=true;safety=F("OBSTACLE");clockMs=0;counts[0]=counts[1]=0;
 startMotion(20,MOTION_REMOTE,100,100,600,0,false,F("REMOTE"),false,true);CHECK(motion.kind==MOTION_REMOTE&&!straightBalance.enabled&&output[0]==100);
 clockMs=150;renewRemote(21,80,80,600);CHECK(output[0]==80&&motion.deadlineMs==750&&motion.lastProgressMs[0]==0);
 clockMs=300;renewRemote(22,-80,80,600);CHECK(motion.lastProgressMs[0]==300&&motion.lastProgressMs[1]==0&&output[0]==-80);
 clockMs=500;counts[0]=counts[1]=1;tickMotion(clockMs);CHECK(motion.kind==MOTION_REMOTE);
 clockMs=899;tickMotion(clockMs);CHECK(motion.kind==MOTION_REMOTE);clockMs=900;tickMotion(clockMs);CHECK(motion.kind==MOTION_NONE&&sessionStopped&&output[0]==0&&output[1]==0);
 sessionStopped=false;touch=false;safety=nullptr;clockMs=0;motion.kind=MOTION_NONE;startMotion(30,MOTION_REMOTE,100,100,600,0,false,F("REMOTE"),false,true);
 for(int i=1;i<=8;i++){clockMs=i*150;renewRemote(30+i,100,100,600);tickMotion(clockMs);}CHECK(motion.kind==MOTION_REMOTE&&motion.lastProgressMs[0]==0);
 clockMs=1201;tickMotion(clockMs);CHECK(motion.kind==MOTION_NONE&&sessionStopped&&output[0]==0);
 sessionStopped=false;
 StraightBalance b;b.reset(UINT32_MAX-99);CHECK(!b.update(99,3,1,100));CHECK(b.update(100,3,1,100)&&b.reduction==2);b.reset(0);CHECK(b.update(200,uint32_t(1-(UINT32_MAX-1)),uint32_t(0-(UINT32_MAX-1)),255)&&b.reduction==0);
 for(int pwm=1;pwm<=255;pwm++){b.reset(0);for(int i=1;i<=20;i++)b.update(i*200,i*10,i,pwm);CHECK(b.reduction<=32&&b.reduction<=pwm/5&&pwm-b.reduction>0);}
 uint8_t hex=0;for(int a=0;a<256;a++)for(int c=0;c<256;c++){char text[]={char(a),char(c),0};bool valid=a&&c&&std::isxdigit(a)&&std::isxdigit(c);CHECK(commandCrcByte(text,hex)==valid);if(valid)CHECK(hex==std::strtoul(text,nullptr,16));}for(auto s:{"","A","ABC","+1"," 1","G0"})CHECK(!commandCrcByte(s,hex));
 int32_t n=42;for(auto s:{"","+","-","1x","1 ","2147483648","-2147483649","99999999999999999999"}){CHECK(!commandLong(s,INT32_MIN,INT32_MAX,n));CHECK(n==42);}CHECK(commandLong("2147483647",INT32_MIN,INT32_MAX,n)&&n==INT32_MAX);CHECK(commandLong("-2147483648",INT32_MIN,INT32_MAX,n)&&n==INT32_MIN);CHECK(commandLong(" \t+001",1,65535,n)&&n==1);
 for(int x=-1000;x<=1000;x++){auto s=std::to_string(x);CHECK(commandLong(s.c_str(),-255,255,n)==(x>=-255&&x<=255));if(x>=-255&&x<=255)CHECK(n==x);}
 printf("actual Uno straight motion and compact parsers: %u checks passed\n",checks);
}
