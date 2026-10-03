#pragma once
#include <stdint.h>
#include <string.h>
// Shared by firmware and native tests. Times are ESP-local boot microseconds.
struct VisionTicket {
  uint32_t token=0, scan=0, generation=0, baseline=0;
  uint16_t step=0;
  uint64_t barrier=0;
  char mode[8]="OFF";
};
struct VisionGate {
  VisionTicket ticket;
  uint32_t serial=0;
  bool pending=false;
  void cancel(){pending=false;ticket.token=++serial;if(!serial)ticket.token=++serial;}
  bool request(uint32_t scan,uint16_t step,uint32_t generation,const char *mode,uint32_t frame,uint64_t time){
    if(!generation||!step||strcmp(mode,"CAT")&&strcmp(mode,"TAGS"))return false;
    if(pending&&ticket.scan==scan&&ticket.step==step&&ticket.generation==generation)return true;
    cancel();pending=true;ticket.scan=scan;ticket.step=step;ticket.generation=generation;
    ticket.baseline=frame;ticket.barrier=time;strcpy(ticket.mode,mode);return true;
  }
  bool frame(const VisionTicket &t,uint32_t seq,uint64_t capture) const {
    return pending&&t.token==ticket.token&&t.scan==ticket.scan&&t.step==ticket.step&&t.generation==ticket.generation&&int32_t(seq-ticket.baseline)>0&&capture>ticket.barrier;
  }
  bool finish(const VisionTicket &t,uint32_t seq,uint64_t capture){
    if(!frame(t,seq,capture))return false;pending=false;return true;
  }
};
