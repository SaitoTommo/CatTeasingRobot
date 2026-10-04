#pragma once
// XIAO onboard user LED is GPIO21, active LOW; it is not external pin D0.
constexpr uint8_t HOST_LED_PIN = LED_BUILTIN;
uint8_t hostLedMode = 0;
uint32_t hostLedStarted = 0, hostLedUntil = 0, hostLedInterval = 0;
bool hostLedOn = false;
void hostLedStop(){hostLedMode=0;hostLedOn=false;digitalWrite(HOST_LED_PIN,HIGH);}
// Mode 3 belongs to the ESP response sequencer. Delayed Uno LED echoes must
// not overwrite it; STOP, reset and link loss still turn it off.
void hostLedResponse(bool on,bool active){
  if(!active){if(hostLedMode==3)hostLedStop();return;}
  if(mode!="UART")return;
  hostLedMode=3;hostLedOn=on;digitalWrite(HOST_LED_PIN,on?LOW:HIGH);
}
void hostLedManual(bool on){hostLedMode=4;hostLedOn=on;digitalWrite(HOST_LED_PIN,on?LOW:HIGH);}
void hostLedInit(){digitalWrite(HOST_LED_PIN,HIGH);pinMode(HOST_LED_PIN,OUTPUT);}
void hostLedAccept(const String &line){
  if(line.startsWith("READY ")){hostLedStop();return;}
  if(hostLedMode==3||hostLedMode==4)return;
  if(line=="EVENT HOST_LED OFF"){hostLedStop();return;}
  if(line=="EVENT HOST_LED ON"){hostLedMode=1;hostLedOn=true;digitalWrite(HOST_LED_PIN,LOW);return;}
  unsigned interval=0,duration=0;char trailing=0;
  if(sscanf(line.c_str(),"EVENT HOST_LED FLASH %u %u %c",&interval,&duration,&trailing)==2&&interval>=20&&interval<=5000&&duration>=1&&duration<=10000){
    hostLedMode=2;hostLedStarted=millis();hostLedUntil=hostLedStarted+duration;hostLedInterval=interval;hostLedOn=true;digitalWrite(HOST_LED_PIN,LOW);
  }
}
void hostLedTick(){
  if(hostLedMode&&hostLedMode!=4&&(mode!="UART"||!lastUnoAt||millis()-lastUnoAt>2500||(hostLedMode==2&&(int32_t)(millis()-hostLedUntil)>=0))){hostLedStop();return;}
  if(hostLedMode==2){hostLedOn=((millis()-hostLedStarted)/hostLedInterval)%2==0;digitalWrite(HOST_LED_PIN,hostLedOn?LOW:HIGH);}
}
void hostLedSnapshot(JsonDocument &d){
  d["hostLed"]["pin"]="onboard/GPIO21";d["hostLed"]["on"]=hostLedOn;d["hostLed"]["mode"]=hostLedMode==4?(hostLedOn?"ON":"OFF"):hostLedMode==3?"RESPONSE":hostLedMode==2?"FLASH":hostLedMode==1?"ON":"OFF";
  d["hostLed"]["gpioLevel"]=hostLedOn?0:1;d["hostLed"]["source"]="ESP_GPIO_CONTROL";
}
