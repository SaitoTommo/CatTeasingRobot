// Stage-one transport build. Replaced by the autonomous policy in the next commit.
#include <Arduino.h>
extern String phase;
extern uint32_t sendUno(const String &);
void policyInput(const String &) {}
void policyTick() {}
void policyControl(const String &action) {if(action=="stop"){sendUno("STOP");phase="STOPPED";}}
