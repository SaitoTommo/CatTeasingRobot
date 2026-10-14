// Firmware version: ESP32_CAT_HOST_0.51_STRAIGHT_BALANCE_INTERVAL. Reported by /api/state.
// Uno pairing: UNO_CAT_V6.15, D13 RX and one-use receive windows.
// Board: XIAO_ESP32S3, OPI PSRAM, max_app_8MB. UART: 57600; USB log: 115200.
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_camera.h>
#include <esp_http_server.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_system.h>
#include "vision_gate.h"
#include "wire_integrity.h"
#include "pwm_floor.h"
#include <deque>
#include "debug_page.h"
#include "policy_defaults.h"
#include "host_types.h"
SET_LOOP_TASK_STACK_SIZE(16384);

// Control work stays on loop(); HTTP and camera tasks only enqueue input.
QueueHandle_t inputs;
QueueHandle_t usbPackets;
SemaphoreHandle_t stateLock, frameLock, cameraSensorLock;
String deviceId,bootId;
Preferences prefs;
HardwareSerial uno(1);
String usbLine, uartLine, mode="SIM_UNO", phase="IDLE", lastRx, lastTx;
JsonDocument config;
JsonDocument observation;
JsonDocument realVision;
String logs[160]; uint32_t logSeq=0, dropped=0, lastUnoAt=0, lastPoll=0;
uint32_t telemetryId=0,telemetryAt=0;
uint8_t *jpegCache=nullptr; size_t jpegLen=0; uint32_t frameSeq=0;
bool cameraOk=false;
uint64_t frameCaptureUs=0;
VisionGate visionGate;String visionPayload,actualVisionMode="OFF";
uint32_t visionOversize=0,visionSuperseded=0,visionRejected=0,visionConsumed=0;
String policyVisionMode();
void policyResetTagRecovery();
void policyVision();
void completeGroundCalibration(JsonObject v);
void cancelVision(){visionGate.cancel();visionPayload="";}
void requestVision(uint32_t scan,uint16_t step,uint32_t generation,const char *selected){
 xSemaphoreTake(frameLock,portMAX_DELAY);uint32_t seq=frameSeq;xSemaphoreGive(frameLock);
 visionGate.request(scan,step,generation,selected,seq,esp_timer_get_time());
}
#include "host_led.h"
// Post-inference rest, not a fixed observation period. processVision also
// enforces at least 350 ms between inference starts.
String visionMode="OFF";uint32_t visionIntervalMs=200;
#include "vision_bench.h"
#include "vision_preview.h"
void emitUsb(const String &s){String *copy=new String(s);if(xQueueSend(usbPackets,&copy,0)!=pdTRUE){delete copy;}}
void usbTask(void*){String *s;while(true){if(xQueueReceive(usbPackets,&s,portMAX_DELAY)==pdTRUE){Serial.println(*s);delete s;}}}
uint8_t crc8(const String &s){return wireCrc8(s.c_str(),s.length());}
void logLine(const char *source,const String &line) {
  JsonDocument d; d["seq"]=++logSeq; d["ms"]=millis();d["source"]=source;d["line"]=line;
  String out;serializeJson(d,out);
  xSemaphoreTakeRecursive(stateLock,portMAX_DELAY); logs[(logSeq-1)%160]=out;if(logSeq>160)dropped++;xSemaphoreGiveRecursive(stateLock);
  emitUsb(out);
}
uint32_t cmdSeq=0;
#include "network_health.h"
#include "video_stream.h"
void enqueueCamera(const String &out){xSemaphoreTakeRecursive(stateLock,portMAX_DELAY);if(out.length()>12287){visionOversize++;}else{if(visionPayload.length())visionSuperseded++;visionPayload=out;}xSemaphoreGiveRecursive(stateLock);}
void acceptCamera(JsonDocument &d){
 auto v=d["vision"].as<JsonObject>();uint32_t token=v["ticket"]|0u;String desired=policyVisionMode();if(desired=="USER")desired=visionMode;
 // Benchmark observations retain the policy's required tag/floor work.
 // Never promote a continuous benchmark frame to a requested stop/scan ticket.
 if(!token&&visionBench){String measured=v["mode"]|"";if(desired=="TAGS"&&measured=="BOTH"){v["mode"]="TAGS";v["cat"]["valid"]=false;v["cat"]["detected"]=false;}else if(desired=="FLOOR"&&measured=="CAT"){v["mode"]="FLOOR";v["cat"]["valid"]=false;v["cat"]["detected"]=false;v["front"]=!v["frontCalibrated"].as<bool>()?"INVALID":v["floorDifference"].as<float>()>(config["floorDifferenceThreshold"]|35.0f)?"BLOCKED":"CLEAR";}else if(desired=="CAT"&&measured=="BOTH")v["mode"]="CAT";}
 if(token){VisionTicket t=visionGate.ticket;t.token=token;t.scan=v["scanId"]|0u;t.step=v["stepId"]|0u;t.generation=v["generation"]|0u;if(!visionGate.finish(t,v["frameSeq"]|0u,v["captureUs"].as<uint64_t>())){visionRejected++;return;}}
 else if(desired=="OFF"||String(v["mode"]|"")!=desired||(v["epoch"]|0u)!=visionGate.serial){visionRejected++;return;}
 realVision=v;visionConsumed++;completeGroundCalibration(v);if(mode=="UART"){observation=realVision;policyVision();}
}
struct Outgoing {String wire;bool physical;};
std::deque<Outgoing> outgoing;uint32_t lastWrite=0;
uint16_t rxGrant=0;uint32_t rxGrantAt=0;bool rxGrantSeen=false;
// The Uno reserves 60 ms. Budget 45 ms including command wire time.
bool windowCanSend(const String &wire){
  uint32_t age=millis()-rxGrantAt;size_t bytes=wire.length()+10;
  return rxGrant&&age>=3&&age+(bytes*10*1000+57599)/57600+5<=45&&bytes<128;
}
bool writeWindowCommand(const String &wire,bool emergency=false){
  bool granted=windowCanSend(wire);if(!granted&&!emergency)return false;
  String actual=wire;
  if(granted){
    String payload="R "+String(rxGrant)+" "+wire.substring(0,wire.indexOf('*'));
    char suffix[5];snprintf(suffix,sizeof(suffix),"*%02X",crc8(payload));actual=payload+suffix;
  }
  rxGrant=0;uno.println(actual);logLine(granted?"UART_WINDOW_TX":"UART_EMERGENCY_TX",actual);return true;
}
uint32_t stopPendingId=0,stopSentAt=0;uint8_t stopRetries=0;bool stopUnconfirmed=false;
String stopWire;
bool stopReady(){return !stopPendingId&&!stopUnconfirmed;}
void policyCommandSent(uint32_t id);
void policyStopUnconfirmed();
void clearTx(){outgoing.clear();telemetryId=0;lastPoll=millis();}
void resendScanStart(uint32_t id,const String &command){
  String payload="C "+String(id)+" "+command;char suffix[5];snprintf(suffix,sizeof(suffix),"*%02X",crc8(payload));
  outgoing.push_back({payload+suffix,mode=="UART"});
}
bool remoteOwnsOutputs();
uint32_t sendUno(const String &command) {
  if((command=="STOP"&&!remoteOwnsOutputs())||command=="SESSION_STOP"||command=="DEBUG_OUTPUTS OFF"||command=="DEBUG_IO LED OFF"||command=="DEBUG_MOTOR_MODE DRY_RUN")hostLedStop();
  String applied=applyDrivePwmFloor(command,config["minimumDrivePwm"]|140);
  if(applied!=command)logLine("PWM_FLOOR",command+" -> "+applied);
  cmdSeq=cmdSeq%65535+1;uint32_t id=cmdSeq;String payload="C "+String(id)+" "+applied;char suffix[5];snprintf(suffix,sizeof(suffix),"*%02X",crc8(payload));
  if(outgoing.size()>=32){outgoing.clear();payload="C "+String(id)+" STOP";snprintf(suffix,sizeof(suffix),"*%02X",crc8(payload));phase="STOPPED";logLine("TX_OVERFLOW","policy stopped");}
  outgoing.push_back({payload+suffix,mode=="UART"});
  return id;
}
#include "stop_query_guard.h"
bool policyInteractionStopping();
void flushTx(){
  if(stopPendingId){
    if(millis()-stopSentAt>=300){
      if(stopRetries<3){stopRetries++;stopSentAt=millis();lastWrite=millis();writeWindowCommand(stopWire,true);logLine("STOP_RETRY",stopWire);}
      else if(!stopUnconfirmed){stopUnconfirmed=true;outgoing.clear();telemetryId=0;hostLedStop();logLine("STOP_UNCONFIRMED","No checked MOTOR_STOPPED reply; further motor commands blocked");policyStopUnconfirmed();}
    }
    if(!stopUnconfirmed){
      if(!policyInteractionStopping()||outgoing.empty()||!stopInteractionOutput(outgoing.front().wire.c_str()))return;
    }else{
    // Keep diagnosis available without allowing queries to clear a failed STOP.
    while(!outgoing.empty()&&(!outgoing.front().physical||(outgoing.front().wire.indexOf(" STOP*")<0&&!stopDiagnosticQuery(outgoing.front().wire.c_str())))){logLine("TX_BLOCKED","stop is unconfirmed: "+outgoing.front().wire);outgoing.pop_front();}
    if(outgoing.empty())return;
    if(outgoing.front().wire.indexOf(" STOP*")>=0)stopPendingId=0;
    }
  }
  if(outgoing.empty()||millis()-lastWrite<15)return;
  if(outgoing.front().physical&&outgoing.front().wire.indexOf(" STOP*")<0&&!windowCanSend(outgoing.front().wire))return;
  auto o=outgoing.front();
  if(o.physical&&!writeWindowCommand(o.wire,o.wire.indexOf(" STOP*")>=0))return;
  outgoing.pop_front();lastWrite=millis();lastTx=o.wire;
  logLine(o.physical?"UNO_TX":"SIM_TX",lastTx);
  if(o.physical){
    if(o.wire.indexOf(" STOP*")>=0){stopPendingId=o.wire.substring(2).toInt();stopWire=o.wire;stopSentAt=millis();stopRetries=0;stopUnconfirmed=false;}
  }
  if(o.wire.indexOf(" MOTOR ")>=0||o.wire.indexOf(" SR ")>=0||o.wire.indexOf(" DRIVE ")>=0||o.wire.indexOf(" ROTATE ")>=0)policyCommandSent(o.wire.substring(2).toInt());
}
void policyInput(const String &line);
void policyTick();
void policyControl(const String &action);
bool policyPreparing();
uint32_t policyTelemetryInterval();
void policySnapshot(JsonDocument &);
#include "uno_debug.h"
#include "compact_telemetry.h"
CompactTelemetry compactTelemetry;
void policyResumeFrom(int index);
#include "remote_control.h"
#include "route_editor.h"
#include "front_switch.h"
#include "balance_settings.h"
void resetLinkState(){rxGrant=0;rxGrantSeen=false;uartLine="";compactTelemetry.reset();debugReset();lastRx="";lastTx="";lastUnoAt=0;telemetryId=0;lastPoll=millis()-300;}
void acceptUno(String line,bool physical) {
  line.trim(); if(!line.length())return;
  bool pendingStopReply=physical&&stopPendingId&&line.startsWith("DONE "+String(stopPendingId)+" MOTOR_STOPPED*");
  if((mode=="UART")!=physical&&!pendingStopReply){logLine("INPUT_REJECTED","source does not match mode");return;}
  // Uno's wire protocol is ASCII. Noise must never enter JSON logs as raw
  // control bytes / invalid UTF-8, or count as evidence of a live Uno.
  if(physical){
    bool textOk=line.length()<=512;
    for(size_t i=0;i<line.length()&&textOk;i++){uint8_t b=(uint8_t)line[i];if(b<32||b>126)textOk=false;}
    if(!textOk){String hex="discarded bytes="+String(line.length())+" prefix=";for(size_t i=0;i<line.length()&&i<24;i++){char byte[4];snprintf(byte,sizeof(byte),"%02X ",(uint8_t)line[i]);hex+=byte;}logLine("UART_BAD_FRAME",hex);return;}
    // A standalone Uno bench request has no motion or policy meaning. Reply
    // once, only while stopped, and keep it out of controller freshness.
    if(line.startsWith("EVENT UART_BENCH_REQ ")){
      logLine("UART_BENCH_RX",line);
      if(phase!="IDLE"&&phase!="STOPPED"){logLine("UART_BENCH_BLOCKED","stop policy before bench test");return;}
      if(debugId||manualUntil||configStep>=0){logLine("UART_BENCH_BLOCKED","finish debug operation before bench test");return;}
      String reply="EVENT UART_BENCH_REPLY "+line.substring(21);
      uno.println(reply);logLine("UART_BENCH_TX",reply);return;
    }
    if(line.startsWith("EVENT UART_BENCH_RECEIVED ")){logLine("UART_BENCH_ACK",line);return;}
    // A checked copy of our own command proves TX/RX loopback, never a Uno
    // acknowledgement. Keep it out of policy input and freshness accounting.
    if(line.startsWith("C ")){
      int star=line.indexOf('*');String payload=star>0?line.substring(0,star):"",hex=star>0?line.substring(star+1):"";
      bool checked=star>0&&hex.length()==2&&isxdigit(hex[0])&&isxdigit(hex[1])&&strtoul(hex.c_str(),nullptr,16)==crc8(payload);
      logLine(checked?"UART_LOOPBACK":"UART_BAD_FRAME",line);return;
    }
    bool known=line.startsWith("RX ")||line.startsWith("VQ ")||line.startsWith("SD ")||line.startsWith("TB ")||line.startsWith("READY ")||line.startsWith("ACK ")||line.startsWith("AK ")||line.startsWith("DONE ")||line.startsWith("ERROR ")||line.startsWith("DATA ")||line.startsWith("EVENT ")||line.startsWith("MS ")||line.startsWith("MD ")||line.startsWith("MP ")||line.startsWith("MA ");
    if(!known){logLine("UART_BAD_FRAME","unknown ASCII reply prefix");return;}
  }
  int star=line.indexOf('*');
  if(star>=0) {String hex=line.substring(star+1);String payload=line.substring(0,star);if(hex.length()!=2||!isxdigit(hex[0])||!isxdigit(hex[1])||strtoul(hex.c_str(),nullptr,16)!=crc8(payload)){if(line.startsWith("TB ")||line.startsWith("DONE "))compactTelemetry.reset();logLine("CRC_ERROR",line);return;}line=payload;}
  else if(protectedReply(line.c_str(),physical,compactTelemetry.id!=0)){logLine("CRC_MISSING",line);return;}
  if(physical){
    rxGrant=0;
    if(line.startsWith("READY "))rxGrantSeen=false;
    if(line.startsWith("RX ")){
      unsigned window=0;char extra=0;
      if(sscanf(line.c_str(),"RX %u %c",&window,&extra)!=1||!window||window>65535){logLine("UART_BAD_FRAME",line);return;}
      rxGrant=window;rxGrantAt=millis();rxGrantSeen=true;lastRx=line;lastUnoAt=millis();logLine("UART_AVAILABLE",line);return;
    }
  }
  if(physical&&stopPendingId&&star>=0&&line=="DONE "+String(stopPendingId)+" MOTOR_STOPPED"){stopPendingId=0;stopUnconfirmed=false;stopWire="";logLine("STOP_CONFIRMED",line);if(mode!="UART")return;}
  if(line.startsWith("READY "))compactTelemetry.reset();
  if(line.startsWith("TB ")){if(!compactTelemetry.add(line,millis())){logLine("TELEMETRY_BAD_FRAME",line);return;}lastRx=line;lastUnoAt=millis();logLine(physical?"UNO_RX":"SIM_RX",line);return;}
  if(line.startsWith("DONE ")&&line.endsWith(" TELEMETRY")&&compactTelemetry.id){if(!compactTelemetry.finish(line,millis(),[](const String &row){debugAccept(row);policyInput(row);})){logLine("TELEMETRY_INCOMPLETE",line);if(telemetryCompleted(line.c_str(),telemetryId)){telemetryId=0;lastPoll=millis();logLine("TELEMETRY_RETRY","incomplete snapshot ended; retry at policy polling interval");}return;}}
  lastRx=line;lastUnoAt=millis();if(physical)hostLedAccept(line);logLine(physical?"UNO_RX":"SIM_RX",line);debugAccept(line);RemoteControl::accept(line);if(line.startsWith("DONE ")||line.startsWith("ERROR ")){if(line.substring(line.indexOf(' ')+1).toInt()==telemetryId)telemetryId=0;}policyInput(line);
}
void defaults() {
  policyDefaults(config);
  config["route"].to<JsonArray>(); // Route is supplied by the operator.
  config["searchPwm"]=75;config["alignPwm"]=52;config["centerTolerancePercent"]=5.5;
  config["patrolSearchPwm"]=45;config["patrolAlignPwm"]=30;
  config["wheelMm"]=67.5;config["trackMm"]=120;config["stopMarginCm"]=3;
  config["tagStopWidth"]=0.25;config["catNearWidth"]=0.35;config["catFarWidth"]=0.12;
  config["visionMaxAgeMs"]=1500;config["searchTimeoutMs"]=15000;
  config["initialLookSeconds"]=5;config["postEscapeWaitSeconds"]=10;
  config["explorationBranchPercent"]=20;config["touchFlashPercent"]=20;
  config["touchPreyPercent"]=20;config["touchSlowEscapePercent"]=40;config["touchFastEscapePercent"]=40;
  config["endingAfterRounds"]=3;config["endingInactivitySeconds"]=12;
  config["endingPatrolPercent"]=40;config["endingSilentPercent"]=30;config["endingSignalPercent"]=30;
  config["captureWaitMaxSeconds"]=15;config["catLeaveSeconds"]=5;config["sessionLimitSeconds"]=60;
  config["slowApproachCm"]=9;config["slowApproachPwm"]=95;
  config["escapeDistanceCm"]=26;config["slowEscapePwm"]=95;config["fastEscapePwm"]=210;
  config["catFollowProbePercent"]=5;config["catFollowObservePercent"]=35;config["robotFarProbePercent"]=15;
  config["robotNearSignalPercent"]=20;config["robotNearObservePercent"]=40;
  config["probePwm"]=90;config["probeStep1Cm"]=3;config["probeStep2Cm"]=4;config["probeStep3Cm"]=3;
  config["probePause1Ms"]=700;config["probePause2Ms"]=900;config["probePause3Ms"]=600;
  config["catProbeCooldownSeconds"]=15;config["robotProbeCooldownSeconds"]=15;config["touchReleaseTimeoutMs"]=4000;
  config["decisionIntervalMs"]=2500;config["signalFlashMs"]=1400;config["signalBeepMs"]=180;
  config["frontAvoidanceEnabled"]=true;config["frontCalibrated"]=false;config["frontBlockBottom"]=0.88;config["floorRoiTop"]=0.75;config["floorDifferenceThreshold"]=35;config["floorReference"].to<JsonArray>();
}
#include "camera_settings_runtime.h"
#include "ground_calibration_runtime.h"
String snapshot(uint32_t afterLog,uint16_t logLimit) {
  JsonDocument d;d["firmware"]="ESP32_CAT_HOST_0.51_STRAIGHT_BALANCE_INTERVAL";d["deviceId"]=deviceId;d["bootId"]=bootId;cameraSettingsSnapshot(d);d["ms"]=millis();d["mode"]=mode;d["phase"]=phase;RemoteControl::snapshot(d);RouteEditor::snapshot(d);debugSnapshot(d);hostLedSnapshot(d);policySnapshot(d);catPreviewSnapshot(d);groundCalibrationSnapshot(d);
  if(mode=="SIM_UNO"){auto t=d["simVisionRequest"].to<JsonObject>();t["scanId"]=visionGate.ticket.scan;t["stepId"]=visionGate.ticket.step;t["generation"]=visionGate.ticket.generation;t["mode"]=visionGate.ticket.mode;t["pending"]=visionGate.pending;}
  d["lastUnoAgeMs"]=lastUnoAt?millis()-lastUnoAt:UINT32_MAX;d["lastRx"]=lastRx;d["lastTx"]=lastTx;
  d["uartWindow"]["seen"]=rxGrantSeen;d["uartWindow"]["sequence"]=rxGrant;d["uartWindow"]["ageMs"]=rxGrantSeen?millis()-rxGrantAt:UINT32_MAX;d["uartWindow"]["queued"]=outgoing.size();d["stopPendingId"]=stopPendingId;d["stopUnconfirmed"]=stopUnconfirmed;
  d["cameraOk"]=cameraOk;d["frameSeq"]=frameSeq;d["heap"]=ESP.getFreeHeap();d["psram"]=ESP.getFreePsram();
  d["actualVisionMode"]=actualVisionMode;d["visionMailbox"]["oversize"]=visionOversize;d["visionMailbox"]["superseded"]=visionSuperseded;d["visionMailbox"]["rejected"]=visionRejected;d["visionMailbox"]["consumed"]=visionConsumed;d["visionMode"]=visionMode;d["visionIntervalMs"]=visionIntervalMs;
  d["apIP"]=WiFi.softAPIP().toString();d["stationIP"]=WiFi.localIP().toString();networkSnapshot(d);videoSnapshot(d);d["groundReferenceChangedAt"]=groundReferenceChangedAt;d["config"]=config;d["vision"]=observation;d["cameraVision"]=realVision;
  d["droppedLogs"]=dropped;JsonArray arr=d["logs"].to<JsonArray>();uint32_t start=logSeq>160?logSeq-160:0;
  if(afterLog<=logSeq)start=max(start,afterLog); // A newer client cursor means ESP rebooted.
  start=max(start,logSeq>logLimit?logSeq-logLimit:0u);
  d["logSeq"]=logSeq;d["logsFromSeq"]=start+1;
  for(uint32_t n=start;n<logSeq;n++){JsonDocument item;deserializeJson(item,logs[n%160]);arr.add(item);}
  String out;serializeJson(d,out);return out;
}
void handleInput(const Input &in) {
  JsonDocument d;if(deserializeJson(d,in.text)){logLine("INPUT_ERROR","invalid JSON");return;}
  String op=d["op"]|"";
  if(d["expectedMode"].is<const char*>()&&String(d["expectedMode"].as<const char*>())!=mode){logLine("INPUT_REJECTED","mode changed before requested operation");return;}
  if(op=="uno")acceptUno(d["line"]|"",in.physical);
  else if(op=="uno_batch"&&mode=="SIM_UNO"&&!in.physical){JsonArray lines=d["lines"].as<JsonArray>();if(!lines.size()||lines.size()>32){logLine("INPUT_ERROR","invalid SIM batch");return;}for(JsonVariant row:lines){if(!row.is<const char*>()||strlen(row.as<const char*>())>512){logLine("INPUT_ERROR","invalid SIM row");return;}}for(JsonVariant row:lines)acceptUno(row.as<const char*>(),false);}
  else if(op=="state"){xSemaphoreTakeRecursive(stateLock,portMAX_DELAY);String out=snapshot(0,160);xSemaphoreGiveRecursive(stateLock);emitUsb(out);}
  else if(op=="control") {
    String control=d["action"]|"";
    RemoteControl::patrol(d);
  }
  else if(op=="remote")RemoteControl::input(d);
  else if(op=="route_save")RouteEditor::input(d);
  else if(op=="manual_test"||op=="uno_config"||op=="bench_safety")debugInput(d);
  else if(op=="command") {if((phase!="IDLE"&&phase!="STOPPED")||debugId||manualUntil||configStep>=0){logLine("INPUT_ERROR","stop policy and finish debug operation before raw commands");return;}String command=d["command"]|"PING";command.trim();if(!stopReady()&&command!="STOP"){logLine("INPUT_ERROR","wait for confirmed STOP");return;}if(!command.length()||command.length()>96||command.indexOf('\n')>=0||command.indexOf('\r')>=0){logLine("INPUT_ERROR","invalid command line");return;}sendUno(command);}
  else if(op=="mode") {RemoteControl::abort("source changed");hostLedStop();policyControl("stop");rxGrant=0;rxGrantSeen=false;mode=String(d["mode"]|"SIM_UNO")=="UART"?"UART":"SIM_UNO";debugReset();compactTelemetry.reset();lastRx="";lastTx="";lastUnoAt=0;telemetryId=0;lastPoll=millis()-300;observation.clear();policyInput("READY SOURCE_CHANGED");sendUno("STOP");sendUno("SESSION_STOP");sendUno("GET_CONFIG");logLine("MODE",mode);}
  else if(op=="vision" && mode=="SIM_UNO") {observation=d["vision"];observation["source"]="SIM_CAMERA";observation["at"]=millis();policyVision();}
  else if(op=="camera"&&in.physical) acceptCamera(d);
  else if(op=="vision_bench") {
    if(!d["enabled"].is<bool>()||!d["gapMs"].is<unsigned>()||!d["captureGapMs"].is<unsigned>()||d["gapMs"].as<unsigned>()>5000||d["captureGapMs"].as<unsigned>()>2000){logLine("INPUT_ERROR","invalid benchmark interval");return;}
    bool reset=d["reset"]|false;bool enable=d["enabled"].as<bool>();
    if(reset||(enable&&!visionBench)){xSemaphoreTake(frameLock,portMAX_DELAY);benchEpoch++;benchCount=benchPreviousStart=benchPreviousEnd=0;benchJpegLen=0;benchResult="";xSemaphoreGive(frameLock);}
    visionBench=enable;benchGapMs=d["gapMs"];benchCaptureMs=d["captureGapMs"];logLine("VISION_BENCH",enable?"enabled":"disabled");
  }
  else if(op=="camera_config"||op=="camera_reset")cameraSettingsInput(d,op=="camera_reset");
  else if(op=="vision_mode") {String m=d["mode"]|"OFF";if(m!="OFF"&&m!="TAGS"&&m!="CAT"&&m!="BOTH"){logLine("INPUT_ERROR","invalid vision mode");return;}visionMode=m;if(policyVisionMode()=="USER"){cancelVision();realVision.clear();if(mode=="UART")observation.clear();}logLine("VISION_MODE",visionMode);}
  else if(op=="calibrate_front")startGroundCalibration();
  else if(op=="config") {
    if(phase!="IDLE"&&phase!="STOPPED"||!stopReady()||debugId||manualUntil||configStep>=0){logLine("CONFIG_ERROR","confirmed stop required first");return;}
    JsonObject obj=d["config"].as<JsonObject>();
    if(obj.size()==1&&obj.containsKey("straightBalanceIntervalMs")){
      if(!obj["straightBalanceIntervalMs"].is<int>()||obj["straightBalanceIntervalMs"].as<int>()<20||obj["straightBalanceIntervalMs"].as<int>()>1000){logLine("CONFIG_ERROR","balance interval must be an integer from 20 to 1000 ms");return;}
      String error;uint16_t value=obj["straightBalanceIntervalMs"].as<uint16_t>();
      if(!BalanceSettings::persist(value,error)){logLine("CONFIG_ERROR",error);return;}
      config["straightBalanceIntervalMs"]=value;logLine("CONFIG","balance interval saved and verified");return;
    }
    if(obj.size()==1&&!obj["frontAvoidanceEnabled"].isNull()){
      if(!obj["frontAvoidanceEnabled"].is<bool>()){logLine("CONFIG_ERROR","invalid front avoidance flag");return;}
      String error;bool enabled=obj["frontAvoidanceEnabled"].as<bool>();if(!FrontSwitch::persist(enabled,error)){logLine("CONFIG_ERROR",error);return;}
      config["frontAvoidanceEnabled"]=enabled;policyResetTagRecovery();logLine("CONFIG","front switch saved and verified");return;
    }
    if(obj.size()==1&&obj["route"].is<JsonArray>()){JsonDocument request;request["requestId"]="legacy-route-"+String(millis());request["route"]=obj["route"];RouteEditor::input(request);return;}
    if(obj["route"].is<JsonArray>()) {auto route=obj["route"].as<JsonArray>();if(!route.size()||route.size()>128){logLine("CONFIG_ERROR","route needs 1..128 IDs");return;}for(JsonVariant v:route)if(!v.is<int>()||v.as<int>()<0||v.as<int>()>586){logLine("CONFIG_ERROR","tag36h11 IDs must be 0..586");return;}}
    JsonDocument candidate;candidate.set(config);for(JsonPair kv:obj){if(config[kv.key()].isNull()){logLine("CONFIG_ERROR",String("unknown setting ")+kv.key().c_str());return;}candidate[kv.key()]=kv.value();}String error;if(!validateSettings(candidate,error)){logLine("CONFIG_ERROR",error);return;}if(candidate["floorRoiTop"].as<float>()!=config["floorRoiTop"].as<float>()){groundReferenceChangedAt=millis();candidate["frontCalibrated"]=false;candidate["floorReference"].to<JsonArray>();logLine("CALIBRATION","ROI changed; ground reference invalidated");}
    const bool balanceChange=obj.containsKey("straightBalanceIntervalMs");
    if(!balanceChange)config=candidate;
    String saved;serializeJson(candidate,saved);size_t savedBytes=prefs.putString("config",saved);
    if(savedBytes!=saved.length()){logLine("CONFIG_ERROR",balanceChange?"balance interval NVS save failed; previous RAM value retained":"NVS save failed; RAM applied but restart persistence unconfirmed");return;}
    if(balanceChange){
      JsonDocument stored;
      if(deserializeJson(stored,prefs.getString("config",""))||!stored["straightBalanceIntervalMs"].is<int>()||stored["straightBalanceIntervalMs"].as<int>()!=candidate["straightBalanceIntervalMs"].as<int>()){
        logLine("CONFIG_ERROR","balance interval NVS readback mismatch; previous RAM value retained");return;
      }
      if(!BalanceSettings::persist(candidate["straightBalanceIntervalMs"].as<uint16_t>(),error)){logLine("CONFIG_ERROR",error);return;}
      config=candidate;
    }
    policyResetTagRecovery();
    if(obj["route"].is<JsonArray>()&&!RouteEditor::persist(obj["route"].as<JsonArray>(),error)){logLine("CONFIG_ERROR",error);return;}
    if(obj.containsKey("frontAvoidanceEnabled")&&!FrontSwitch::persist(config["frontAvoidanceEnabled"].as<bool>(),error)){logLine("CONFIG_ERROR",error);return;}
    logLine("CONFIG","saved");
  }
  else if(op=="wifi") {String ssid=d["ssid"]|"",pass=d["password"]|"";prefs.putString("ssid",ssid);prefs.putString("password",pass);networkBegin(ssid,pass);logLine("WIFI","station connecting; AP remains available");}
  else logLine("INPUT_ERROR","unsupported op or source");
}
esp_err_t pageHandler(httpd_req_t *r) {httpd_resp_set_type(r,"text/html; charset=utf-8");httpd_resp_set_hdr(r,"Content-Encoding","gzip");return httpd_resp_send(r,(const char*)DEBUG_PAGE_GZIP,sizeof(DEBUG_PAGE_GZIP));}
esp_err_t stateHandler(httpd_req_t *r) {
 uint32_t afterLog=0;uint16_t logLimit=160;char query[96]={},value[24]={};
 if(httpd_req_get_url_query_str(r,query,sizeof(query))==ESP_OK){
  if(httpd_query_key_value(query,"afterLog",value,sizeof(value))==ESP_OK)afterLog=strtoul(value,nullptr,10);
  if(httpd_query_key_value(query,"logLimit",value,sizeof(value))==ESP_OK)logLimit=constrain(strtoul(value,nullptr,10),1ul,160ul);
 }
 if(httpd_query_key_value(query,"bootId",value,sizeof(value))==ESP_OK&&String(value)!=bootId)afterLog=0;
 if(xSemaphoreTakeRecursive(stateLock,pdMS_TO_TICKS(250))!=pdTRUE){httpd_resp_set_status(r,"503 Service Unavailable");return httpd_resp_sendstr(r,"controller busy; retry");}
 String s=snapshot(afterLog,logLimit);xSemaphoreGiveRecursive(stateLock);httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Cache-Control","no-store");httpd_resp_set_hdr(r,"Access-Control-Allow-Origin","*");return httpd_resp_send(r,s.c_str(),s.length());
}
void policyFlowSnapshot(JsonDocument &);
esp_err_t flowHandler(httpd_req_t *r){
 JsonDocument d;xSemaphoreTakeRecursive(stateLock,portMAX_DELAY);d["ms"]=millis();d["phase"]=phase;policyFlowSnapshot(d);d["uartWindow"]["seen"]=rxGrantSeen;d["uartWindow"]["sequence"]=rxGrant;d["uartWindow"]["ageMs"]=rxGrantSeen?millis()-rxGrantAt:UINT32_MAX;d["uartWindow"]["queued"]=outgoing.size();d["stopPendingId"]=stopPendingId;d["stopUnconfirmed"]=stopUnconfirmed;
 String recent[8];int n=0;for(uint32_t id=logSeq;id&&logSeq-id<160&&n<8;id--){const String &row=logs[(id-1)%160];if(row.indexOf("\"source\":\"DECISION\"")>=0&&row.indexOf("\"line\":\"PATROL ")>=0)recent[n++]=row;}
 xSemaphoreGiveRecursive(stateLock);auto events=d["logs"].to<JsonArray>();while(n){JsonDocument item;if(!deserializeJson(item,recent[--n]))events.add(item.as<JsonObject>());}
 String out;serializeJson(d,out);httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Cache-Control","no-store");return httpd_resp_send(r,out.c_str(),out.length());
}
esp_err_t captureHandler(httpd_req_t *r){uint8_t *copy=nullptr;size_t len=0;xSemaphoreTake(frameLock,portMAX_DELAY);if(jpegLen){len=jpegLen;copy=(uint8_t*)ps_malloc(len);if(copy)memcpy(copy,jpegCache,len);}xSemaphoreGive(frameLock);if(!copy){httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"camera unavailable");return ESP_FAIL;}httpd_resp_set_type(r,"image/jpeg");httpd_resp_set_hdr(r,"Access-Control-Allow-Origin","*");esp_err_t e=httpd_resp_send(r,(const char*)copy,len);free(copy);return e;}
esp_err_t inputHandler(httpd_req_t *r) {
  if(r->content_len>4095){httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"body too large");return ESP_FAIL;}
  Input in={};size_t n=0;while(n<r->content_len){int got=httpd_req_recv(r,in.text+n,r->content_len-n);if(got<=0)return ESP_FAIL;n+=got;}
  if(xQueueSend(inputs,&in,0)!=pdTRUE){httpd_resp_set_status(r,"503 Service Unavailable");httpd_resp_sendstr(r,"input queue full");return ESP_FAIL;}
  httpd_resp_set_type(r,"application/json");return httpd_resp_sendstr(r,"{\"queued\":true}");
}
void startServers() {
  videoStart();
  httpd_config_t c=HTTPD_DEFAULT_CONFIG();c.server_port=80;c.stack_size=8192;c.max_uri_handlers=10;c.max_open_sockets=SHARED_HTTP_MAX_SOCKETS;c.lru_purge_enable=true;c.recv_wait_timeout=2;c.send_wait_timeout=2;httpd_handle_t h=nullptr;
  if(httpd_start(&h,&c)==ESP_OK){httpd_uri_t p={.uri="/",.method=HTTP_GET,.handler=pageHandler};httpd_register_uri_handler(h,&p);p.uri="/api/state";p.handler=stateHandler;httpd_register_uri_handler(h,&p);p.uri="/api/flow";p.handler=flowHandler;httpd_register_uri_handler(h,&p);p.uri="/capture";p.handler=captureHandler;httpd_register_uri_handler(h,&p);p.uri="/api/vision-bench";p.handler=benchStateHandler;httpd_register_uri_handler(h,&p);p.uri="/vision-bench-frame";p.handler=benchFrameHandler;httpd_register_uri_handler(h,&p);p.uri="/cat-preview-frame";p.handler=catPreviewHandler;httpd_register_uri_handler(h,&p);p.uri="/stream";p.handler=streamHandler;httpd_register_uri_handler(h,&p);p.uri="/api/input";p.method=HTTP_POST;p.handler=inputHandler;httpd_register_uri_handler(h,&p);}
}
void processVision(camera_fb_t *,const VisionTicket &,uint32_t,uint64_t,uint32_t,uint32_t);
void visionTask(void*){
 uint32_t previous=0,lastContinuous=0,lastFinished=0;
 while(true){
  xSemaphoreTakeRecursive(stateLock,portMAX_DELAY);VisionTicket ticket=visionGate.ticket;bool requested=visionGate.pending;uint32_t epoch=visionGate.serial,benchRun=visionBench?benchEpoch:UINT32_MAX;
  String policyMode=policyVisionMode();bool bench=visionBench;uint32_t gap=benchGapMs;String normalMode=policyMode=="USER"?visionMode:policyMode;String selected=requested?String(ticket.mode):bench?(normalMode=="TAGS"||normalMode=="BOTH"?String("BOTH"):String("CAT")):normalMode;actualVisionMode=selected;xSemaphoreGiveRecursive(stateLock);
  if(selected!="OFF"){
   camera_fb_t fb={};uint32_t seq=0;uint64_t capture=0;xSemaphoreTake(frameLock,portMAX_DELAY);seq=frameSeq;capture=frameCaptureUs;
   if(jpegLen&&seq!=previous&&(requested||(bench?millis()-lastFinished>=gap:millis()-lastContinuous>=350))){fb.len=jpegLen;fb.buf=(uint8_t*)ps_malloc(fb.len);if(fb.buf){memcpy(fb.buf,jpegCache,fb.len);fb.width=320;fb.height=240;fb.format=PIXFORMAT_JPEG;}}xSemaphoreGive(frameLock);
   if(fb.buf){xSemaphoreTakeRecursive(stateLock,portMAX_DELAY);bool eligible=!requested||visionGate.frame(ticket,seq,capture);xSemaphoreGiveRecursive(stateLock);
    if(eligible){if(!requested){ticket.token=0;strcpy(ticket.mode,selected.c_str());lastContinuous=millis();}processVision(&fb,ticket,seq,capture,epoch,benchRun);lastFinished=millis();previous=seq;}free(fb.buf);
   }
  }
  vTaskDelay(bench?1:pdMS_TO_TICKS(20));
 }
}
void cameraTask(void*){
 while(true){
  xSemaphoreTake(cameraSensorLock,portMAX_DELAY);
  camera_fb_t *fb=esp_camera_fb_get();
  if(fb){
   xSemaphoreTake(frameLock,portMAX_DELAY);
   uint64_t captured=uint64_t(fb->timestamp.tv_sec)*1000000+fb->timestamp.tv_usec;
   bool discard=cameraDiscardFrames>0;if(discard)cameraDiscardFrames--;
   if(!discard&&captured>cameraSettingsBarrier&&fb->len<160000){
    memcpy(jpegCache,fb->buf,fb->len);jpegLen=fb->len;frameCaptureUs=captured;frameSeq++;
   }
   xSemaphoreGive(frameLock);esp_camera_fb_return(fb);
  }
  // Release sensor lock before stateLock: settings run while holding stateLock.
  xSemaphoreGive(cameraSensorLock);
  xSemaphoreTakeRecursive(stateLock,portMAX_DELAY);uint32_t captureRest=visionBench?benchCaptureMs:200;xSemaphoreGiveRecursive(stateLock);
  vTaskDelay(captureRest?max((TickType_t)1,pdMS_TO_TICKS(captureRest)):(TickType_t)1);
 }
}
void initCamera() {
  camera_config_t c={};c.ledc_channel=LEDC_CHANNEL_0;c.ledc_timer=LEDC_TIMER_0;
  c.pin_d0=15;c.pin_d1=17;c.pin_d2=18;c.pin_d3=16;c.pin_d4=14;c.pin_d5=12;c.pin_d6=11;c.pin_d7=48;
  c.pin_xclk=10;c.pin_pclk=13;c.pin_vsync=38;c.pin_href=47;c.pin_sccb_sda=40;c.pin_sccb_scl=39;c.pin_pwdn=-1;c.pin_reset=-1;
  c.xclk_freq_hz=20000000;c.pixel_format=PIXFORMAT_JPEG;c.frame_size=FRAMESIZE_QVGA;c.jpeg_quality=12;c.fb_count=2;c.fb_location=CAMERA_FB_IN_PSRAM;c.grab_mode=CAMERA_GRAB_LATEST;
  esp_err_t err=esp_camera_init(&c);cameraOk=err==ESP_OK;logLine("CAMERA",String("init=")+err+" psram="+ESP.getPsramSize());
  if(cameraOk){auto s=esp_camera_sensor_get();if(s->id.PID==OV3660_PID)s->set_vflip(s,1);cameraSettingsRestore(s);jpegCache=(uint8_t*)ps_malloc(160000);if(jpegCache){xTaskCreatePinnedToCore(cameraTask,"camera",4096,nullptr,2,nullptr,0);xTaskCreatePinnedToCore(visionTask,"vision",16384,nullptr,1,nullptr,0);}else cameraOk=false;}
}
void setup() {
  hostLedInit();
  heap_caps_malloc_extmem_enable(1024);Serial.setTxBufferSize(4096);Serial.setRxBufferSize(8192);Serial.setTxTimeoutMs(20);Serial.begin(115200);uno.begin(57600,SERIAL_8N1,44,43);inputs=xQueueCreate(8,sizeof(Input));usbPackets=xQueueCreate(12,sizeof(String*));stateLock=xSemaphoreCreateRecursiveMutex();frameLock=xSemaphoreCreateMutex();cameraSensorLock=xSemaphoreCreateMutex();char identity[24];snprintf(identity,sizeof(identity),"%012llx",(unsigned long long)ESP.getEfuseMac());deviceId=identity;snprintf(identity,sizeof(identity),"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());bootId=identity;xTaskCreate(usbTask,"usb-log",4096,nullptr,1,nullptr);
  prefs.begin("cat-host",false);defaults();String saved=prefs.getString("config","");if(saved.length()){JsonDocument d;if(!deserializeJson(d,saved)){JsonDocument candidate;candidate.set(config);for(JsonPair kv:d.as<JsonObject>())candidate[kv.key()]=kv.value();migrateCenterSettings(candidate,d);if(!d["catObserveTimeoutSeconds"].is<float>())candidate["catObserveTimeoutSeconds"]=max(1.0f,candidate["postEscapeWaitSeconds"].as<float>());if(!d["escapeMinDistanceCm"].is<float>()||!d["escapeMaxDistanceCm"].is<float>()){float legacy=max(1.0f,candidate["escapeDistanceCm"].as<float>()-candidate["stopMarginCm"].as<float>());candidate["escapeMinDistanceCm"]=legacy;candidate["escapeMaxDistanceCm"]=legacy;}String error;if(validateSettings(candidate,error))config=candidate;else logLine("CONFIG_ERROR","saved config rejected: "+error);}}
  networkEvents=xQueueCreate(8,sizeof(NetworkEvent));WiFi.onEvent(networkEvent);
  RouteEditor::restore();FrontSwitch::restore();BalanceSettings::restore();
  WiFi.mode(WIFI_AP_STA);WiFi.setAutoReconnect(true);WiFi.setSleep(false);WiFi.softAP("ESP32-CatHost","catrobot32");networkBegin(prefs.getString("ssid",""),prefs.getString("password",""));
  logLine("READY","ESP32_CAT_HOST AP=192.168.4.1 HTTP=80 STREAM=81 UART_RX=44 TX=43 BAUD=57600 mode=SIM_UNO");initCamera();startServers();
}
void readLines(Stream &s,String &buffer,bool physical) {static Input in;static bool usbOverflow=false,uartOverflow=false;bool &overflow=physical?uartOverflow:usbOverflow;int count=0;while(s.available()&&count++<1024){char c=s.read();if(c=='\n'){if(!overflow){if(physical)acceptUno(buffer,true);else {buffer.toCharArray(in.text,sizeof(in.text));handleInput(in);}}overflow=false;buffer="";}else if(c!='\r'&&!overflow){if(buffer.length()<4095)buffer+=c;else{buffer="";overflow=true;logLine("OVERFLOW","serial line discarded");}}}}
void loop() {
  xSemaphoreTakeRecursive(stateLock,portMAX_DELAY);networkTick();xSemaphoreGiveRecursive(stateLock);
  xSemaphoreTakeRecursive(stateLock,portMAX_DELAY);readLines(Serial,usbLine,false);readLines(uno,uartLine,true);static Input in;for(int i=0;i<8&&xQueueReceive(inputs,&in,0)==pdTRUE;i++)handleInput(in);
  if(visionPayload.length()){JsonDocument d;String payload=visionPayload;visionPayload="";if(!deserializeJson(d,payload))acceptCamera(d);else visionRejected++;}
  debugTick();RemoteControl::tick();policyTick();groundCalibrationTick();hostLedTick();xSemaphoreGiveRecursive(stateLock);
  if(telemetryId&&millis()-telemetryAt>5000){telemetryId=0;lastPoll=millis();logLine("TELEMETRY_TIMEOUT","retry in 2s");}
  if(!policyPreparing()&&(int32_t)(millis()-lastPoll)>=int32_t(mode=="SIM_UNO"?min(uint32_t(1000),policyTelemetryInterval()):policyTelemetryInterval())&&!telemetryId&&outgoing.size()<4){lastPoll=millis();telemetryAt=millis();telemetryId=sendUno("GET_TELEMETRY");}
  flushTx();
  delay(2);
}



