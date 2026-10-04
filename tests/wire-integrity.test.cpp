#include "../esp32-host/ESP32CatHost/wire_integrity.h"
#include "../esp32-host/ESP32CatHost/stop_query_guard.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main(){
 for(auto s:{"C 1 GET_TELEMETRY*AB","C 65535 GET_CONFIG*01","C 12 PING*ff"})assert(stopDiagnosticQuery(s));
 for(auto s:{"C 1 MOTOR 1 1*AB","C 1 DRIVE 23 255*AB","C 1 ROTATE 20 255*AB","C 1 SR 1 2 255 71 0*AB","C 1 SESSION_START*AB","C 1 MOTOR GET_TELEMETRY*AB","C 1 GET_TELEMETRY*ABx","C 0 PING*AB","C 1 GET_CONFIG*Z1","C 1 PING*A","C 65536 PING*AB"})assert(!stopDiagnosticQuery(s));
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
