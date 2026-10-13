#include <Arduino.h>
#include <deque>
#include <vector>
#include <cassert>
#include "../esp32-host/ESP32CatHost/stop_query_guard.h"
#include "../esp32-host/ESP32CatHost/wire_integrity.h"
struct Outgoing{String wire;bool physical;};
std::deque<Outgoing> outgoing;
uint32_t tick=1000,lastWrite=0,stopPendingId=1,stopSentAt=1000,telemetryId=0;
uint8_t stopRetries=0;bool stopUnconfirmed=false,interacting=true,failed=false;
uint16_t rxGrant=0;uint32_t rxGrantAt=0;
uint8_t crc8(const String &s){return wireCrc8(s.c_str(),s.length());}
String stopWire="C 1 STOP*AB",lastTx;
std::vector<String> sent;std::vector<uint32_t> motors;
uint32_t millis(){return tick;}
struct Serial{void println(const String &s){sent.push_back(s);}}uno;
void logLine(const char *,const String &){}
void hostLedStop(){}
void policyStopUnconfirmed(){failed=true;interacting=false;}
bool policyInteractionStopping(){return interacting;}
void policyCommandSent(uint32_t id){motors.push_back(id);}
#include STOP_OUTPUT_SOURCE
void reset(){outgoing.clear();sent.clear();motors.clear();tick=stopSentAt=1000;lastWrite=0;stopPendingId=1;stopRetries=0;stopUnconfirmed=failed=false;interacting=true;rxGrant=123;rxGrantAt=tick-3;}
int main(){
  // Run the real UART flush: output commands are dispatched before a STOP reply.
  reset();for(const char *wire:{"C 2 LED FLASH 200 1400*AB","C 3 BEEP 180*AB"}){
    rxGrant=123;rxGrantAt=tick-3;outgoing.push_back({wire,true});flushTx();assert(sent.back().startsWith("R 123 ")&&sent.back().indexOf(String(wire).substring(0,String(wire).indexOf('*')).c_str())>=0&&stopPendingId==1);tick+=20;
  }
  assert(motors.empty());
  for(const char *wire:{"C 2 MOTOR 110 110*AB","C 2 DRIVE 3 110*AB","C 2 ROTATE 20 110*AB","C 2 SR 1 2 110 71 0*AB","C 2 SESSION_START*AB"}){
    reset();outgoing.push_back({wire,true});flushTx();assert(sent.empty()&&outgoing.size()==1&&stopPendingId==1);
  }
  reset();interacting=false;outgoing.push_back({"C 2 BEEP 180*AB",true});flushTx();assert(sent.empty());
  reset();stopUnconfirmed=true;outgoing.push_back({"C 2 BEEP 180*AB",true});outgoing.push_back({"C 3 GET_TELEMETRY*AB",true});flushTx();assert(sent.size()==1&&sent[0].startsWith("R 123 C 3 GET_TELEMETRY*")&&stopPendingId==1);
  reset();stopPendingId=0;outgoing.push_back({"C 2 DRIVE 3 110*AB",true});flushTx();assert(motors.size()==1&&motors[0]==2);
  reset();outgoing.push_back({"C 2 BEEP 180*AB",true});tick+=300;flushTx();assert(sent[0]==stopWire&&stopRetries==1&&outgoing.size()==1);
  stopRetries=3;tick+=300;flushTx();assert(failed&&stopUnconfirmed&&outgoing.empty());
  // No grant / expired grant cannot drain the queue or start a motion timer.
  reset();stopPendingId=0;rxGrant=0;outgoing.push_back({"C 4 DRIVE 3 110*AB",true});flushTx();assert(sent.empty()&&motors.empty()&&outgoing.size()==1);
  rxGrant=124;rxGrantAt=tick-60;flushTx();assert(sent.empty());
  rxGrantAt=tick-3;flushTx();assert(sent.size()==1&&rxGrant==0&&motors.size()==1);
  outgoing.push_back({"C 5 GET_TELEMETRY*AB",true});tick+=20;flushTx();assert(sent.size()==1&&outgoing.size()==1);
  reset();stopPendingId=0;rxGrant=0;outgoing.push_back({"C 6 STOP*AB",true});flushTx();assert(sent.size()==1&&sent[0]=="C 6 STOP*AB"&&stopPendingId==6);
  reset();stopPendingId=0;rxGrant=0;outgoing.push_back({"C 7 DRIVE 3 110*AB",false});flushTx();assert(sent.empty()&&outgoing.empty()&&motors.size()==1);
  puts("actual UART STOP gate: immediate interaction outputs, blocked motion, retries and failed-stop diagnostics passed");
}
