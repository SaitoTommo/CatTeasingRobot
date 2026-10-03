#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include "../firmware/UnoCatController/scan_machine.h"
static unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);} } while(0)
int main(){
  ScanMachine s;
  CHECK(!s.begin(0,1,1,45,72,0,0));CHECK(!s.begin(1,0,1,45,72,0,0));
  CHECK(s.begin(1,7,1,45,72,0,0));CHECK(s.tick(0,0,0)==ScanMachine::START_STEP);
  s.motionStarted(0,0);s.motionStopped(100,1,1);
  CHECK(s.tick(250,1,1)==ScanMachine::NONE);
  CHECK(s.tick(300,2,1)==ScanMachine::NONE); // coasting restarts quiet window
  CHECK(s.tick(400,2,1)==ScanMachine::REQUEST);CHECK(s.used==2);
  CHECK(!s.reply(2,1,7,1,1,410));CHECK(!s.reply(1,1,6,1,1,410));
  CHECK(!s.reply(1,1,7,1,0,410));
  CHECK(s.tick(900,2,1)==ScanMachine::REQUEST);CHECK(s.step==1);
  CHECK(s.begin(1,7,1,45,72,0,900));CHECK(s.step==1); // duplicate command
  CHECK(s.reply(1,1,7,0,1,901));CHECK(!s.reply(1,1,7,0,1,902));
  CHECK(s.tick(902,2,1)==ScanMachine::START_STEP);s.motionStarted(2,1);s.motionStopped(1000,3,2);
  CHECK(s.tick(1200,3,2)==ScanMachine::REQUEST);
  CHECK(!s.reply(1,2,7,1,1,1201));CHECK(s.reply(1,2,7,1,2,1201));
  CHECK(s.tick(1201,3,2)==ScanMachine::COMPLETE);CHECK(!s.active());CHECK(s.result==ScanMachine::FOUND);
  CHECK(s.tick(1202,3,2)==ScanMachine::NONE);
  CHECK(s.begin(1,7,1,45,72,0,1202));CHECK(!s.active()); // completed duplicate cannot restart
  CHECK(s.begin(3,8,-1,45,2,0,1300));
  for(int i=0;i<2;i++){uint32_t t=1300+i*400;CHECK(s.tick(t,i,i)==ScanMachine::START_STEP);s.motionStarted(i,i);s.motionStopped(t+100,i+1,i+1);CHECK(s.tick(t+300,i+1,i+1)==ScanMachine::REQUEST);CHECK(s.reply(3,i+1,8,0,10+i,t+301));}
  CHECK(s.tick(2102,2,2)==ScanMachine::COMPLETE);CHECK(s.result==ScanMachine::NOT_FOUND);
  CHECK(s.begin(4,9,1,45,2,0,2200));s.tick(2200,0,0);s.motionStarted(0,0);s.motionStopped(2300,1,1);s.tick(2500,1,1);
  CHECK(!s.reply(4,1,9,1,12,5500));CHECK(s.tick(5500,1,1)==ScanMachine::COMPLETE);CHECK(s.result==ScanMachine::TIMEOUT);
  CHECK(s.begin(5,10,1,45,2,0,6000));s.end(ScanMachine::ABORTED);CHECK(s.tick(6000,0,0)==ScanMachine::COMPLETE);CHECK(!s.reply(5,0,10,1,13,6001));
  ScanMachine w;CHECK(w.begin(7,12,1,45,2,0,UINT32_MAX-100));w.cfg.total=200;
  CHECK(w.tick(100,0,0)==ScanMachine::COMPLETE);CHECK(w.result==ScanMachine::TIMEOUT);
  // A full first revolution is not an automatic finish; find in second lap.
  ScanMachine two;CHECK(two.begin(8,13,1,45,71,0,0));
  for(unsigned i=0;i<50;i++){auto t=i*400;CHECK(two.tick(t,i,i)==ScanMachine::START_STEP);two.motionStarted(i,i);two.motionStopped(t+100,i+1,i+1);CHECK(two.tick(t+300,i+1,i+1)==ScanMachine::REQUEST);CHECK(two.reply(8,i+1,13,i==49?1:0,i+1,t+301));}
  CHECK(two.result==ScanMachine::FOUND);CHECK(two.used==50);
  std::printf("scan core: %u checks passed\n",checks);
}
