#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include "../firmware/UnoCatController/scan_machine.h"
static unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);} } while(0)
int main(){
  ScanMachine duplicate;CHECK(duplicate.begin(95,95,2,45,71,0,0));duplicate.tick(0,0,0);duplicate.motionStarted(0,0);
  CHECK(duplicate.begin(95,95,2,45,71,0,100));CHECK(duplicate.phase==ScanMachine::TURN);CHECK(duplicate.step==1);CHECK(duplicate.began==0);
  duplicate.motionStopped(200,2,2);CHECK(duplicate.begin(95,95,2,45,71,0,201));CHECK(duplicate.phase==ScanMachine::SETTLE);CHECK(duplicate.stopped==200);
  duplicate.tick(400,2,2);CHECK(duplicate.begin(95,95,2,45,71,0,401));CHECK(duplicate.phase==ScanMachine::WAIT_VISION);CHECK(duplicate.used==2);CHECK(duplicate.requested==400);
  duplicate.reply(95,1,95,0,1,402);CHECK(duplicate.begin(95,95,2,45,71,0,403));CHECK(duplicate.phase==ScanMachine::NEXT);CHECK(duplicate.used==2);CHECK(duplicate.step==1);
  duplicate.end(ScanMachine::ABORTED);CHECK(duplicate.begin(95,95,2,45,71,0,404));CHECK(duplicate.phase==ScanMachine::TERMINAL);CHECK(duplicate.result==ScanMachine::ABORTED);CHECK(duplicate.used==2);
  ScanMachine stale;CHECK(stale.begin(99,99,2,45,71,0,900));
  CHECK(stale.tick(900,0,0)==ScanMachine::START_STEP);
  stale.motionStarted(0,0);stale.motionStopped(1001,2,2);
  CHECK(stale.tick(1000,2,2)==ScanMachine::NONE);
  CHECK(stale.phase==ScanMachine::SETTLE);
  CHECK(stale.tick(1200,2,2)==ScanMachine::NONE);
  CHECK(stale.tick(1201,2,2)==ScanMachine::REQUEST);
  ScanMachine rollover;CHECK(rollover.begin(98,98,2,45,71,0,UINT32_MAX-500));
  rollover.tick(UINT32_MAX-500,0,0);rollover.motionStarted(0,0);
  rollover.motionStopped(UINT32_MAX-100,2,2);
  CHECK(rollover.tick(98,2,2)==ScanMachine::NONE);
  CHECK(rollover.tick(99,2,2)==ScanMachine::REQUEST);
  ScanMachine waiting;CHECK(waiting.begin(97,97,2,45,71,0,0));
  waiting.cfg.minimum=1000;waiting.tick(0,0,0);waiting.motionStarted(0,0);waiting.motionStopped(100,2,2);
  CHECK(waiting.tick(1099,2,2)==ScanMachine::NONE);
  CHECK(waiting.tick(1100,2,2)==ScanMachine::REQUEST);
  ScanMachine noisy;CHECK(noisy.begin(96,96,2,45,71,0,0));
  noisy.tick(0,0,0);noisy.motionStarted(0,0);noisy.motionStopped(100,2,2);
  CHECK(noisy.tick(3099,3,3)==ScanMachine::NONE);
  CHECK(noisy.tick(3100,4,4)==ScanMachine::COMPLETE);
  CHECK(noisy.result==ScanMachine::SETTLE_TIMEOUT);
  ScanMachine s;
  CHECK(!s.begin(0,1,1,45,72,0,0));CHECK(!s.begin(1,0,1,45,72,0,0));
  CHECK(s.begin(1,7,1,45,72,0,0));CHECK(s.tick(0,0,0)==ScanMachine::START_STEP);
  s.motionStarted(0,0);s.motionStopped(100,1,1);
  CHECK(s.tick(250,1,1)==ScanMachine::NONE);
  CHECK(s.tick(300,2,1)==ScanMachine::NONE); // coasting restarts quiet window
  CHECK(s.tick(400,2,1)==ScanMachine::REQUEST);CHECK(s.used==1);CHECK(s.usedHalf==3);
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
  // Lost terminal frames: four spaced repeats, bounded and no new turn.
  ScanMachine terminal;CHECK(terminal.begin(20,30,2,45,71,0,100));
  terminal.end(ScanMachine::TIMEOUT);CHECK(terminal.tick(100,0,0)==ScanMachine::COMPLETE);
  for(unsigned i=1;i<=4;i++){
    CHECK(terminal.tick(100+i*500-1,0,0)==ScanMachine::NONE);
    CHECK(terminal.tick(100+i*500,0,0)==ScanMachine::COMPLETE);
    CHECK(terminal.result==ScanMachine::TIMEOUT);CHECK(!terminal.active());
  }
  CHECK(terminal.tick(3100,0,0)==ScanMachine::NONE);
  CHECK(terminal.begin(20,30,2,45,71,0,3101));CHECK(!terminal.active());
  terminal.terminalSent=false;CHECK(terminal.tick(3102,0,0)==ScanMachine::COMPLETE);
  CHECK(terminal.tick(3602,0,0)==ScanMachine::COMPLETE);CHECK(!terminal.active());
  // Asymmetric travel counts the mean, carries halves, and handles counter wrap.
  ScanMachine mean;CHECK(mean.begin(30,40,-1,45,10,0,0));
  uint32_t l=UINT32_MAX-1,r=UINT32_MAX-2;
  for(int i=0;i<3;i++){
    auto t=i*400;CHECK(mean.tick(t,l,r)==ScanMachine::START_STEP);mean.motionStarted(l,r);
    l+=2;r+=1;mean.motionStopped(t+100,l,r);
    CHECK(mean.tick(t+300,l,r)==ScanMachine::REQUEST);
    CHECK(mean.usedHalf==unsigned((i+1)*3));CHECK(mean.used==unsigned((i+1)*3/2));
    CHECK(mean.tick(t+301,l,r)==ScanMachine::NONE);CHECK(mean.usedHalf==unsigned((i+1)*3));
    CHECK(mean.reply(30,i+1,40,0,i+1,t+302));
  }
  CHECK(mean.used==4);CHECK(mean.usedHalf==9);
  ScanMachine cap;CHECK(cap.begin(31,41,1,45,2,0,0));cap.tick(0,0,0);cap.motionStarted(0,0);
  cap.motionStopped(100,UINT32_MAX,UINT32_MAX);CHECK(cap.tick(300,UINT32_MAX,UINT32_MAX)==ScanMachine::REQUEST);
  CHECK(cap.used==2&&cap.usedHalf==4);
  std::printf("scan core: %u checks passed\n",checks);
}
