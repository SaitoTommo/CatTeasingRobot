#include "../esp32-host/ESP32CatHost/vision_gate.h"
#include <assert.h>
#include <stdio.h>
int main(){
  VisionGate g;assert(!g.request(1,0,1,"CAT",9,100));assert(!g.request(1,1,0,"CAT",9,100));
  assert(!g.request(1,1,1,"BOTH",9,100));assert(g.request(1,1,1,"CAT",9,100));auto t=g.ticket;
  assert(!g.frame(t,9,110));assert(!g.frame(t,10,99));assert(!g.frame(t,10,100));
  assert(g.request(1,1,1,"CAT",10,120));assert(g.ticket.token==t.token);assert(g.ticket.barrier==100);
  assert(g.frame(t,10,101));assert(g.finish(t,10,101));assert(!g.finish(t,10,101));
  assert(g.request(1,2,1,"CAT",10,120));assert(!g.finish(t,11,121));t=g.ticket;g.cancel();assert(!g.finish(t,11,121));
  assert(g.request(1,2,2,"TAGS",10,130));assert(!g.frame(t,12,140));t=g.ticket;assert(g.finish(t,12,140));
  assert(g.request(0,1,3,"CAT",0xffffffff,150));t=g.ticket;assert(g.finish(t,1,151));
  puts("vision gate: old cache, late inference, duplicate request, cancellation and wraparound passed");
}
