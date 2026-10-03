#pragma once
#include <stdint.h>

// No Arduino, heap, clocks or I/O: this is the exact core used by the Uno.
struct ScanMachine {
  enum Phase : uint8_t { IDLE, NEXT, TURN, SETTLE, WAIT_VISION, TERMINAL };
  enum Event : uint8_t { NONE, START_STEP, REQUEST, COMPLETE };
  enum Result : uint8_t { NOT_FOUND, FOUND, INVALID, TIMEOUT, ABORTED, SETTLE_TIMEOUT };
  struct Config { uint16_t minimum=200, quiet=100, vision=3000, settle=3000; uint32_t total=300000; } cfg;
  Phase phase=IDLE;
  uint16_t id=0, step=0, budget=0, used=0;
  uint32_t generation=0, began=0, stopped=0, quietAt=0, requested=0, sent=0;
  uint32_t baseL=0, baseR=0, quietL=0, quietR=0, lastFrame=0;
  int16_t increment=1;
  uint8_t pwm=45, flags=0, result=NOT_FOUND;
  bool terminalSent=false;
  bool active() const { return phase!=IDLE && phase!=TERMINAL; }
  bool matches(uint16_t i,uint16_t s,uint32_t g) const { return i && i==id && s==step && g && g==generation; }
  bool begin(uint16_t i,uint32_t g,int16_t inc,uint8_t p,uint16_t limit,uint8_t f,uint32_t now) {
    if (!i || !g || !inc || !p || !limit || f>1) return false;
    if (i==id && g==generation) return true; // retransmission never turns again
    if (active()) return false;
    id=i;generation=g;increment=inc;pwm=p;budget=limit;flags=f;
    used=step=0;lastFrame=0;began=now;phase=NEXT;terminalSent=false;result=NOT_FOUND;return true;
  }
  uint16_t stepPulses() const {
    uint16_t n=increment<0?-increment:increment, remaining=budget-used;
    return n<remaining?n:remaining;
  }
  void end(uint8_t r) { result=r;phase=TERMINAL;terminalSent=false; }
  void motionStarted(uint32_t l,uint32_t r) { baseL=l;baseR=r;phase=TURN; }
  void motionStopped(uint32_t now,uint32_t l,uint32_t r) {
    if (phase!=TURN) return;
    stopped=quietAt=now;quietL=l;quietR=r;phase=SETTLE;
  }
  bool reply(uint16_t i,uint16_t s,uint32_t g,uint8_t r,uint32_t frame,uint32_t now) {
    if (phase!=WAIT_VISION || !matches(i,s,g) || r>INVALID) return false;
    if (r!=INVALID && (!frame || (lastFrame && int32_t(frame-lastFrame)<=0))) return false;
    if (uint32_t(now-requested)>=cfg.vision) return false;
    lastFrame=frame;
    if (r!=NOT_FOUND || used>=budget) end(r);
    else phase=NEXT;
    return true;
  }
  Event tick(uint32_t now,uint32_t l,uint32_t r) {
    if (active() && uint32_t(now-began)>=cfg.total) end(TIMEOUT);
    if (phase==TERMINAL && !terminalSent) { terminalSent=true;return COMPLETE; }
    if (phase==NEXT) {
      if (used>=budget) { end(NOT_FOUND);return tick(now,l,r); }
      ++step;phase=TURN;return START_STEP;
    }
    if (phase==SETTLE) {
      // A caller may hold a loop timestamp older than motionStopped().
      // Do not interpret that small backwards interval as uint32_t wrap.
      if (int32_t(now-stopped)<0) return NONE;
      if (l!=quietL || r!=quietR) { quietL=l;quietR=r;quietAt=now; }
      if (uint32_t(now-stopped)>=cfg.settle) { end(SETTLE_TIMEOUT);return tick(now,l,r); }
      if (uint32_t(now-stopped)>=cfg.minimum && uint32_t(now-quietAt)>=cfg.quiet) {
        uint32_t dl=l-baseL,dr=r-baseR,n=dl>dr?dl:dr;
        used=uint32_t(used)+n>=budget?budget:used+n;
        requested=sent=now;phase=WAIT_VISION;return REQUEST;
      }
    }
    if (phase==WAIT_VISION) {
      if (uint32_t(now-requested)>=cfg.vision) { end(TIMEOUT);return tick(now,l,r); }
      if (uint32_t(now-sent)>=500) { sent=now;return REQUEST; }
    }
    return NONE;
  }
};
