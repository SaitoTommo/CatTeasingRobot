#include <Arduino.h>
#include <cassert>
#include <string>
#include "../firmware/UnoCatController/command_hex.h"
#include "../firmware/UnoCatController/command_integer.h"
#include "../esp32-host/ESP32CatHost/wire_integrity.h"
#define PSTR(s) s
#define F(s) s
#define strncmp_P strncmp
#define strcmp_P strcmp
uint32_t now=1000,rxWindowAt=990;
uint16_t rxWindow=123,accepted=0;
bool rxQuiet=true,dispatchingPhysical=true;
std::string lastError,acceptedVerb;
uint32_t millis(){return now;}
bool rxWindowActive(){return rxQuiet&&millis()-rxWindowAt<60;}
uint8_t serialCrc8(const char *s){return wireCrc8(s,strlen(s));}
void diagBadChecksum(const char *,uint8_t,uint8_t){}
void diagCommand(uint16_t){}
void diagTrace(uint8_t){}
void error(uint16_t,const char *s){lastError=s;rxQuiet=false;}
bool parseLong(const char *s,long lo,long hi,long &out){int32_t n;if(!commandLong(s,lo,hi,n))return false;out=n;return true;}
#include UART_WINDOW_SOURCE
std::string checked(const std::string &s){char h[5];snprintf(h,sizeof(h),"*%02X",serialCrc8(s.c_str()));return s+h;}
void run(std::string line,bool physical=true,uint32_t age=10){
  rxQuiet=true;dispatchingPhysical=physical;rxWindowAt=now-age;accepted=0;lastError.clear();acceptedVerb.clear();
  processCommand(line.data());
}
int main(){
  run(checked("R 123 C 7 GET_TELEMETRY"));assert(accepted==7&&acceptedVerb=="GET_TELEMETRY"&&!rxQuiet);
  std::string replay=checked("R 123 C 8 DRIVE 1 120");accepted=0;processCommand(replay.data());assert(!accepted&&lastError=="RX_WINDOW_EXPIRED");
  run(checked("R 123 C 8 DRIVE 1 120"),true,60);assert(!accepted&&lastError=="RX_WINDOW_EXPIRED");
  run(checked("R 124 C 8 DRIVE 1 120"));assert(!accepted&&lastError=="RX_WINDOW_EXPIRED");
  for(const char *prefix:{"R x ","R 65536 ","R -1 ","R 123x ","R 123 ","R "}){run(checked(std::string(prefix)+"C 8 DRIVE 1 120"));assert(!accepted);}
  run("R 123 C 8 DRIVE 1 120*00");assert(!accepted&&lastError=="BAD_CHECKSUM");
  run("R 123 C 8 DRIVE 1 120");assert(!accepted&&lastError=="BAD_CHECKSUM");
  run(checked("C 8 DRIVE 1 120"));assert(!accepted&&lastError=="RX_WINDOW_REQUIRED");
  run(checked("C 9 STOP"),true,100);assert(accepted==9&&acceptedVerb=="STOP");
  run(checked("C 10 GET_TELEMETRY"),false,100);assert(accepted==10);
  assert(protectedReply("RX 123",true,false));
  puts("actual Uno window gate: CRC, stale/replayed tokens, emergency STOP and USB independence passed");
}
