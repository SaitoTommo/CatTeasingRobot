#pragma once
#include "signal_sequences.h"
// Called only by the ESP control loop, never from HTTP tasks.
namespace RemoteControl {
enum Stage {OFF,ENTER_STOP,ENTER_LOCK,CONFIG,START,ACTIVE,EXIT_STOP,EXIT_LOCK,MOVE_STOP};
Stage stage=OFF;
String client,token,error,afterExit,pendingCommand;
uint32_t sequence=0,pending=0,pendingAt=0,lastInput=0,motionAt=0,motorId=0,beepAt=0;
int left=0,right=0,targetIndex=0,light=-1,lastLight=-1,sound=0;
uint32_t lightAt=0,soundAt=0;
uint8_t pulse=0;
bool motorConfirmed=false,beepHeld=false,capable=false,configSeen=false;
void wait(Stage next,const String &command){stage=next;pendingCommand=command;pending=sendUno(command);pendingAt=millis();}
void resetOutputs(){left=right=0;motorId=0;motorConfirmed=false;beepHeld=false;sound=0;light=-1;hostLedStop();}
void abort(const String &reason){stage=OFF;token="";afterExit="";resetOutputs();error=reason;debugCancel();policyControl("stop");logLine("REMOTE_STOP",reason);}
bool authorize(JsonDocument &d){
  if((stage!=ACTIVE&&stage!=MOVE_STOP&&stage!=START)||String(d["client"]|"")!=client||String(d["session"]|"")!=token||!d["seq"].is<uint32_t>())return false;
  uint32_t next=d["seq"].as<uint32_t>();if(next<=sequence)return false;sequence=next;return true;
}
void beginExit(const String &next,int index){
  afterExit=next;targetIndex=index;token="";resetOutputs();debugCancel();policyControl("stop");clearTx();
  phase="REMOTE";wait(EXIT_STOP,"STOP");
}
bool ownsLed(){return stage==ACTIVE&&light>=0;}
void accept(const String &line){
  if(line.startsWith("READY")){capable=false;if(stage!=OFF)abort("Uno reboot; enter remote again");return;}
  if(stage==OFF)return;
  unsigned id=0;char extra=0;
  if(stage==CONFIG&&line.startsWith(("DATA "+String(pending)+" CONFIG ").c_str())){configSeen=line.indexOf(" REMOTE_V1")>=0;capable=configSeen;return;}
  if(sscanf(line.c_str(),"ERROR %u",&id)==1){if(id==pending||id==motorId)abort(line);return;}
  if(sscanf(line.c_str(),"AK %u %c",&id,&extra)==1&&id==motorId){motorConfirmed=true;return;}
  if((line.startsWith("MA ")||line.startsWith("MD "))&&line.substring(3).toInt()==motorId&&motorId){abort("Uno remote motion stopped; enter remote again");return;}
  String completion="DONE "+String(pending);
  // GET_CONFIG completes with a bare DONE ID on the real Uno.
  if(!(stage==CONFIG&&line==completion)&&!line.startsWith((completion+" ").c_str()))return;
  if(stage==ENTER_STOP&&line.endsWith(" MOTOR_STOPPED"))wait(ENTER_LOCK,"SESSION_STOP");
  else if(stage==ENTER_LOCK&&line.endsWith(" SESSION_STOPPED")){configSeen=false;wait(CONFIG,"GET_CONFIG");}
  else if(stage==CONFIG){if(!configSeen){abort("Uno REMOTE_V1 required (V6.13); autonomous patrol remains available");return;}wait(START,"SESSION_START");}
  else if(stage==MOVE_STOP&&line.endsWith(" MOTOR_STOPPED"))wait(START,"SESSION_START");
  else if(stage==START&&line.endsWith(" SESSION_STARTED")){stage=ACTIVE;pending=0;lastInput=millis();error="";}
  else if(stage==EXIT_STOP&&line.endsWith(" MOTOR_STOPPED"))wait(EXIT_LOCK,"SESSION_STOP");
  else if(stage==EXIT_LOCK&&line.endsWith(" SESSION_STOPPED")){
    String next=afterExit;int index=targetIndex;stage=OFF;pending=0;afterExit="";phase="STOPPED";
    if(next=="start"||next=="resume_from")policyResumeFrom(index);
    else if(next=="alert")policyControl("alert");
  }
}
void inputStop();
void input(JsonDocument &d){
  String action=d["action"]|"";
  if(action=="enter"){
    if(stage!=OFF){error="remote session busy";return;}
    String owner=d["client"]|"";if(owner.length()<1||owner.length()>64){error="invalid remote client";return;}
    if(!stopReady()||debugId||manualUntil||configStep>=0){error="wait for confirmed stop and debug completion";return;}
    client=owner;token=bootId+"-"+String(esp_random());sequence=0;afterExit="";error="";resetOutputs();
    debugCancel();policyControl("stop");clearTx();phase="REMOTE";wait(ENTER_STOP,"STOP");return;
  }
  if(!authorize(d)){logLine("REMOTE_REJECTED","inactive, stale or foreign session");return;}
  lastInput=millis();
  if(action=="exit"){beginExit("",0);return;}
  if(action=="heartbeat")return;
  if(action=="move"){
    if(stage!=ACTIVE||!stopReady()||!d["pwm"].is<int>()){error="invalid speed or stop unconfirmed";return;}
    int pwm=d["pwm"],l=0,r=0;String direction=d["direction"]|"";
    if(pwm<0||pwm>255){error="PWM must be 0..255";return;}
    if(direction=="forward")l=r=pwm;else if(direction=="backward")l=r=-pwm;
    else if(direction=="left"){l=-pwm;r=pwm;}else if(direction=="right"){l=pwm;r=-pwm;}
    else {error="invalid direction";return;}
    if(!pwm){inputStop();return;}
    left=l;right=r;motionAt=millis();motorConfirmed=false;
    // Replace pending renewals; never accumulate old direction/speed packets.
    for(auto it=outgoing.begin();it!=outgoing.end();)if(it->wire.indexOf(" REMOTE*")>=0)it=outgoing.erase(it);else ++it;
    motorId=sendUno("TEST_MOTOR "+String(l)+" "+String(r)+" 600 REMOTE");return;
  }
  if(action=="move_stop"){inputStop();return;}
  if(action=="beep_hold"){
    if(!d["held"].is<bool>()){error="invalid buzzer state";return;}
    sound=0;beepHeld=d["held"].as<bool>();beepAt=millis();sendUno(beepHeld?"BEEP 600":"BEEP 0");return;
  }
  if(action=="sound"){
    if(!d["pattern"].is<int>()||d["pattern"].as<int>()<1||d["pattern"].as<int>()>3){error="invalid sound pattern";return;}
    beepHeld=false;sound=d["pattern"];soundAt=millis();pulse=0;sendUno("BEEP 0");return;
  }
  if(action=="light"){
    int pick=esp_random()%4;if(pick==lastLight)pick=(pick+1+esp_random()%3)%4;
    light=lastLight=pick;lightAt=millis();return;
  }
  if(action=="quiet"){beepHeld=false;sound=0;light=-1;hostLedStop();sendUno("BEEP 0");return;}
  error="unknown remote operation";
}
void inputStop(){
  left=right=0;motorId=0;motorConfirmed=false;
  for(auto it=outgoing.begin();it!=outgoing.end();)if(it->wire.indexOf(" REMOTE*")>=0)it=outgoing.erase(it);else ++it;
  wait(MOVE_STOP,"STOP");
}
void patrol(JsonDocument &d){
  String action=d["action"]|"";int index=0;
  if(action=="stop"){abort("operator stop");return;}
  if(action=="resume_from"){
    if(!d["index"].is<int>()){error="select a route node";return;}index=d["index"];
    String requested,saved;serializeJson(d["route"],requested);serializeJson(config["route"],saved);
    if(requested!=saved){error="route changed; select again";return;}
  }else if(action!="start"&&action!="resume"&&action!="alert"){error="unknown patrol control";return;}
  if(action!="alert"&&(index<0||index>=int(config["route"].size()))){error="invalid patrol route index";return;}
  if(!stopReady()||stage!=OFF&&stage!=ACTIVE){error="wait for stop or session confirmation";return;}
  // Normal non-remote alert retains the existing in-patrol interaction path.
  if(action=="alert"&&stage==OFF){debugCancel();policyControl("alert");return;}
  error="";beginExit(action=="resume"?"start":action,index);
}
void tick(){
  if(stage==OFF)return;
  if(stopUnconfirmed||phase!="REMOTE"){abort("remote interrupted or STOP_UNCONFIRMED");return;}
  uint32_t now=millis();
  if(stage!=ACTIVE){if(now-pendingAt>2500)abort("Uno confirmation timeout: "+pendingCommand+" #"+String(pending));return;}
  if(now-lastInput>1500||mode=="UART"&&(!lastUnoAt||now-lastUnoAt>2500)){abort("remote connection lost");return;}
  if((left||right)&&now-motionAt>=600){abort("direction hold expired");return;}
  if(beepHeld&&now-beepAt>=600){beepHeld=false;sendUno("BEEP 0");}
  if(light>=0){uint32_t t=now-lightAt;hostLedManual(SignalSequence::lightOn(light,t));if(t>=SignalSequence::lightLengths[light]){hostLedStop();light=-1;}}
  if(sound){uint32_t t=now-soundAt;while(pulse<SignalSequence::counts[sound]&&t>=SignalSequence::offsets[sound][pulse]){
    uint32_t end=SignalSequence::offsets[sound][pulse]+SignalSequence::beeps[sound][pulse];if(t<end)sendUno("BEEP "+String(end-t));++pulse;
  }if(t>=SignalSequence::soundLengths[sound])sound=0;}
}
void snapshot(JsonDocument &d){auto r=d["remote"].to<JsonObject>();r["active"]=stage==ACTIVE;r["busy"]=stage!=OFF;r["stage"]=int(stage);r["client"]=client;r["session"]=token;r["capable"]=capable;r["error"]=error;r["leftPwm"]=left;r["rightPwm"]=right;r["motorId"]=motorId;r["motorConfirmed"]=motorConfirmed;r["beepHeld"]=beepHeld;r["sound"]=sound;r["light"]=light;r["lastLight"]=lastLight;}
}
bool remoteOwnsOutputs(){return RemoteControl::ownsLed();}
