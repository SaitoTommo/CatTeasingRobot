#include "../esp32-host/ESP32CatHost/wire_integrity.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main(){
 assert(wireCrc8("123456789",9)==0xf4);
 for(auto s:{"MS 1 0 0","MP 1 2 2","MD 1 2 2","MA 1 TOUCH 0 0","AK 1","TB 1 0 00","VQ 1 2 3 4","SD 1 2 0"}){
  assert(protectedReply(s,true,false));assert(protectedReply(s,false,false));
 }
 assert(protectedReply("DONE 42 MOTOR_STOPPED",true,false));
 assert(protectedReply("DONE 42 SESSION_STARTED",true,false));
 assert(protectedReply("DONE 42 SCAN_CONFIGURED",true,false));
 assert(protectedReply("DONE 42 TELEMETRY",false,true));
 assert(!protectedReply("DONE 42 MOTOR_STOPPED",false,false)); // legacy explicit SIM namespace only
 assert(!protectedReply("DATA 42 CONFIG WHEEL_MM=67.5",true,false));
 assert(!protectedReply("EVENT CAPABILITIES SCAN_V1",true,false));
 puts("actual ESP wire guard: checked physical STOP/session and all critical motion/scan replies passed");
}
