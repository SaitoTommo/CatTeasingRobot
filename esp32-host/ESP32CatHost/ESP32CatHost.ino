#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_camera.h>
#include <esp_http_server.h>
#include <esp_heap_caps.h>
#include <deque>
#include "debug_page.h"
#include "policy_defaults.h"
#include "host_types.h"
SET_LOOP_TASK_STACK_SIZE(16384);

// Control work stays on loop(); HTTP and camera tasks only enqueue input.
QueueHandle_t inputs;
QueueHandle_t usbPackets;
SemaphoreHandle_t stateLock, frameLock;
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
#include "host_led.h"
// Post-inference rest, not a fixed observation period. processVision also
// enforces at least 350 ms between inference starts.
String visionMode="OFF";uint32_t visionIntervalMs=200;
void emitUsb(const String &s){String *copy=new String(s);if(xQueueSend(usbPackets,&copy,0)!=pdTRUE){delete copy;}}
void usbTask(void*){String *s;while(true){if(xQueueReceive(usbPackets,&s,portMAX_DELAY)==pdTRUE){Serial.println(*s);delete s;}}}
uint8_t crc8(const String &s) { uint8_t c=0; for(size_t n=0;n<s.length();n++) {c^=s[n];for(int j=0;j<8;j++)c=(c<<1)^((c&128)?7:0);}return c; }
void logLine(const char *source,const String &line) {
  JsonDocument d; d["seq"]=++logSeq; d["ms"]=millis();d["source"]=source;d["line"]=line;
  String out;serializeJson(d,out);
  xSemaphoreTakeRecursive(stateLock,portMAX_DELAY); logs[(logSeq-1)%160]=out;if(logSeq>160)dropped++;xSemaphoreGiveRecursive(stateLock);
  emitUsb(out);
}
uint32_t cmdSeq=0;
void enqueueCamera(const String &out) {Input in={};in.physical=true;out.toCharArray(in.text,sizeof(in.text));xQueueSend(inputs,&in,0);}
struct Outgoing {String wire;bool physical;};
std::deque<Outgoing> outgoing;uint32_t lastWrite=0;
uint32_t stopPendingId=0,stopSentAt=0;uint8_t stopRetries=0;bool stopUnconfirmed=false;
String stopWire;
void policyCommandSent(uint32_t id);
void policyStopUnconfirmed();
void clearTx(){outgoing.clear();telemetryId=0;lastPoll=millis();}
uint32_t sendUno(const String &command) {
  if(command=="SESSION_STOP"||command=="DEBUG_OUTPUTS OFF"||command=="DEBUG_IO LED OFF"||command=="DEBUG_MOTOR_MODE DRY_RUN")hostLedStop();
  cmdSeq=cmdSeq%65535+1;uint32_t id=cmdSeq;String payload="C "+String(id)+" "+command;char suffix[5];snprintf(suffix,sizeof(suffix),"*%02X",crc8(payload));
  if(outgoing.size()>=32){outgoing.clear();payload="C "+String(id)+" STOP";snprintf(suffix,sizeof(suffix),"*%02X",crc8(payload));phase="STOPPED";logLine("TX_OVERFLOW","policy stopped");}
  outgoing.push_back({payload+suffix,mode=="UART"});
  return id;
}
void flushTx(){
  if(stopPendingId){
    if(millis()-stopSentAt>=300){
      if(stopRetries<3){stopRetries++;stopSentAt=millis();lastWrite=millis();uno.println(stopWire);logLine("STOP_RETRY",stopWire);}
      else if(!stopUnconfirmed){stopUnconfirmed=true;outgoing.clear();telemetryId=0;logLine("STOP_UNCONFIRMED","No checked MOTOR_STOPPED reply; further motor commands blocked");policyStopUnconfirmed();}
    }
    if(!stopUnconfirmed)return;
    // Only a new explicit STOP may restart the bounded confirmation attempt.
    while(!outgoing.empty()&&(!outgoing.front().physical||outgoing.front().wire.indexOf(" STOP*")<0)){logLine("TX_BLOCKED","stop is unconfirmed: "+outgoing.front().wire);outgoing.pop_front();}
    if(outgoing.empty())return;
    stopPendingId=0;
  }
  if(outgoing.empty()||millis()-lastWrite<15)return;
  auto o=outgoing.front();outgoing.pop_front();lastWrite=millis();lastTx=o.wire;
  logLine(o.physical?"UNO_TX":"SIM_TX",lastTx);
  if(o.physical){
    if(o.wire.indexOf(" STOP*")>=0){stopPendingId=o.wire.substring(2).toInt();stopWire=o.wire;stopSentAt=millis();stopRetries=0;stopUnconfirmed=false;}
    uno.println(lastTx);
  }
  if(o.wire.indexOf(" MOTOR ")>=0)policyCommandSent(o.wire.substring(2).toInt());
}
void policyInput(const String &line);
void policyTick();
void policyControl(const String &action);
bool policyPreparing();
void policySnapshot(JsonDocument &);
#include "uno_debug.h"
#include "compact_telemetry.h"
CompactTelemetry compactTelemetry;
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
    bool known=line.startsWith("TB ")||line.startsWith("READY ")||line.startsWith("ACK ")||line.startsWith("AK ")||line.startsWith("DONE ")||line.startsWith("ERROR ")||line.startsWith("DATA ")||line.startsWith("EVENT ")||line.startsWith("MS ")||line.startsWith("MD ")||line.startsWith("MP ")||line.startsWith("MA ");
    if(!known){logLine("UART_BAD_FRAME","unknown ASCII reply prefix");return;}
  }
  int star=line.indexOf('*');
  if(star>=0) {String hex=line.substring(star+1);String payload=line.substring(0,star);if(hex.length()!=2||!isxdigit(hex[0])||!isxdigit(hex[1])||strtoul(hex.c_str(),nullptr,16)!=crc8(payload)){if(line.startsWith("TB ")||line.startsWith("DONE "))compactTelemetry.reset();logLine("CRC_ERROR",line);return;}line=payload;}
  else if(line.startsWith("TB ")||(line.startsWith("DONE ")&&line.endsWith(" TELEMETRY")&&compactTelemetry.id)||line.startsWith("MP ")||line.startsWith("MS ")||line.startsWith("MD ")||line.startsWith("MA ")||line.startsWith("AK ")){logLine("CRC_MISSING",line);return;}
  if(physical&&stopPendingId&&star>=0&&line=="DONE "+String(stopPendingId)+" MOTOR_STOPPED"){stopPendingId=0;stopUnconfirmed=false;stopWire="";logLine("STOP_CONFIRMED",line);if(mode!="UART")return;}
  if(line.startsWith("READY "))compactTelemetry.reset();
  if(line.startsWith("TB ")){if(!compactTelemetry.add(line,millis())){logLine("TELEMETRY_BAD_FRAME",line);return;}lastRx=line;lastUnoAt=millis();logLine(physical?"UNO_RX":"SIM_RX",line);return;}
  if(line.startsWith("DONE ")&&line.endsWith(" TELEMETRY")&&compactTelemetry.id){if(!compactTelemetry.finish(line,millis(),[](const String &row){debugAccept(row);policyInput(row);})){logLine("TELEMETRY_INCOMPLETE",line);return;}}
  lastRx=line;lastUnoAt=millis();if(physical)hostLedAccept(line);logLine(physical?"UNO_RX":"SIM_RX",line);debugAccept(line);if(line.startsWith("DONE ")||line.startsWith("ERROR ")){if(line.substring(line.indexOf(' ')+1).toInt()==telemetryId)telemetryId=0;}policyInput(line);
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
String snapshot() {
  JsonDocument d;d["firmware"]="ESP32_CAT_HOST_0.11";d["ms"]=millis();d["mode"]=mode;d["phase"]=phase;debugSnapshot(d);hostLedSnapshot(d);policySnapshot(d);
  d["lastUnoAgeMs"]=lastUnoAt?millis()-lastUnoAt:UINT32_MAX;d["lastRx"]=lastRx;d["lastTx"]=lastTx;
  d["stopPendingId"]=stopPendingId;d["stopUnconfirmed"]=stopUnconfirmed;
  d["cameraOk"]=cameraOk;d["frameSeq"]=frameSeq;d["heap"]=ESP.getFreeHeap();d["psram"]=ESP.getFreePsram();
  d["visionMode"]=visionMode;d["visionIntervalMs"]=visionIntervalMs;
  d["apIP"]=WiFi.softAPIP().toString();d["stationIP"]=WiFi.localIP().toString();d["config"]=config;d["vision"]=observation;d["cameraVision"]=realVision;
  d["droppedLogs"]=dropped;JsonArray arr=d["logs"].to<JsonArray>();uint32_t start=logSeq>160?logSeq-160:0;
  for(uint32_t n=start;n<logSeq;n++){JsonDocument item;deserializeJson(item,logs[n%160]);arr.add(item);}
  String out;serializeJson(d,out);return out;
}
void handleInput(const Input &in) {
  JsonDocument d;if(deserializeJson(d,in.text)){logLine("INPUT_ERROR","invalid JSON");return;}
  String op=d["op"]|"";
  if(d["expectedMode"].is<const char*>()&&String(d["expectedMode"].as<const char*>())!=mode){logLine("INPUT_REJECTED","mode changed before requested operation");return;}
  if(op=="uno")acceptUno(d["line"]|"",in.physical);
  else if(op=="state"){xSemaphoreTakeRecursive(stateLock,portMAX_DELAY);String out=snapshot();xSemaphoreGiveRecursive(stateLock);emitUsb(out);}
  else if(op=="control") {
    String control=d["action"]|"";
    if((control=="start"||control=="resume")&&mode=="UART"&&config["route"].size()&&visionMode!="TAGS"&&visionMode!="BOTH"){
      visionMode="BOTH";realVision.clear();observation.clear();logLine("VISION_MODE","BOTH automatically enabled for patrol");
    }
    debugCancel();policyControl(control);
  }
  else if(op=="manual_test"||op=="uno_config"||op=="bench_safety")debugInput(d);
  else if(op=="command") {if((phase!="IDLE"&&phase!="STOPPED")||debugId||manualUntil||configStep>=0){logLine("INPUT_ERROR","stop policy and finish debug operation before raw commands");return;}String command=d["command"]|"PING";command.trim();if(!command.length()||command.length()>96||command.indexOf('\n')>=0||command.indexOf('\r')>=0){logLine("INPUT_ERROR","invalid command line");return;}sendUno(command);}
  else if(op=="mode") {hostLedStop();policyControl("stop");mode=String(d["mode"]|"SIM_UNO")=="UART"?"UART":"SIM_UNO";debugReset();compactTelemetry.reset();lastRx="";lastTx="";lastUnoAt=0;telemetryId=0;lastPoll=millis()-300;observation.clear();policyInput("READY SOURCE_CHANGED");sendUno("STOP");sendUno("SESSION_STOP");sendUno("GET_CONFIG");logLine("MODE",mode);}
  else if(op=="vision" && mode=="SIM_UNO") {observation=d["vision"];observation["source"]="SIM_CAMERA";observation["at"]=millis();}
  else if(op=="camera"&&in.physical) {if(visionMode!="OFF"&&String(d["vision"]["mode"]|"")==visionMode){realVision=d["vision"];if(mode=="UART")observation=realVision;}}
  else if(op=="vision_mode") {String m=d["mode"]|"OFF";if(m!="OFF"&&m!="TAGS"&&m!="CAT"&&m!="BOTH"){logLine("INPUT_ERROR","invalid vision mode");return;}visionMode=m;realVision.clear();if(mode=="UART")observation.clear();logLine("VISION_MODE",visionMode);}
  else if(op=="calibrate_front") {if(phase!="IDLE"&&phase!="STOPPED"){logLine("CONFIG_ERROR","stop first");return;}if(millis()-realVision["at"].as<uint32_t>()>5000||realVision["floorSignature"].size()!=72){logLine("CONFIG_ERROR","no fresh camera signature");return;}config["floorReference"]=realVision["floorSignature"];config["frontCalibrated"]=true;String saved;serializeJson(config,saved);prefs.putString("config",saved);logLine("CALIBRATION","ground appearance reference captured; experimental qualitative front input");}
  else if(op=="config") {
    if(phase!="IDLE"&&phase!="STOPPED"){logLine("CONFIG_ERROR","stop first");return;}
    JsonObject obj=d["config"].as<JsonObject>();
    if(obj["route"].is<JsonArray>()) {auto route=obj["route"].as<JsonArray>();if(!route.size()||route.size()>128){logLine("CONFIG_ERROR","route needs 1..128 IDs");return;}for(JsonVariant v:route)if(!v.is<int>()||v.as<int>()<0||v.as<int>()>586){logLine("CONFIG_ERROR","tag36h11 IDs must be 0..586");return;}}
    JsonDocument candidate;candidate.set(config);for(JsonPair kv:obj){if(config[kv.key()].isNull()){logLine("CONFIG_ERROR",String("unknown setting ")+kv.key().c_str());return;}candidate[kv.key()]=kv.value();}String error;if(!validateSettings(candidate,error)){logLine("CONFIG_ERROR",error);return;}config=candidate;
    String saved;serializeJson(config,saved);prefs.putString("config",saved);logLine("CONFIG","saved");
  }
  else if(op=="wifi") {String ssid=d["ssid"]|"",pass=d["password"]|"";prefs.putString("ssid",ssid);prefs.putString("password",pass);WiFi.begin(ssid.c_str(),pass.c_str());logLine("WIFI","station connecting; AP remains available");}
  else logLine("INPUT_ERROR","unsupported op or source");
}
esp_err_t pageHandler(httpd_req_t *r) {httpd_resp_set_type(r,"text/html; charset=utf-8");httpd_resp_set_hdr(r,"Content-Encoding","gzip");return httpd_resp_send(r,(const char*)DEBUG_PAGE_GZIP,sizeof(DEBUG_PAGE_GZIP));}
esp_err_t stateHandler(httpd_req_t *r) {xSemaphoreTakeRecursive(stateLock,portMAX_DELAY);String s=snapshot();xSemaphoreGiveRecursive(stateLock);httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Cache-Control","no-store");httpd_resp_set_hdr(r,"Access-Control-Allow-Origin","*");return httpd_resp_send(r,s.c_str(),s.length());}
esp_err_t captureHandler(httpd_req_t *r){uint8_t *copy=nullptr;size_t len=0;xSemaphoreTake(frameLock,portMAX_DELAY);if(jpegLen){len=jpegLen;copy=(uint8_t*)ps_malloc(len);if(copy)memcpy(copy,jpegCache,len);}xSemaphoreGive(frameLock);if(!copy){httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"camera unavailable");return ESP_FAIL;}httpd_resp_set_type(r,"image/jpeg");httpd_resp_set_hdr(r,"Access-Control-Allow-Origin","*");esp_err_t e=httpd_resp_send(r,(const char*)copy,len);free(copy);return e;}
esp_err_t inputHandler(httpd_req_t *r) {
  if(r->content_len>4095){httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"body too large");return ESP_FAIL;}
  Input in={};size_t n=0;while(n<r->content_len){int got=httpd_req_recv(r,in.text+n,r->content_len-n);if(got<=0)return ESP_FAIL;n+=got;}
  if(xQueueSend(inputs,&in,0)!=pdTRUE){httpd_resp_set_status(r,"503 Service Unavailable");httpd_resp_sendstr(r,"input queue full");return ESP_FAIL;}
  httpd_resp_set_type(r,"application/json");return httpd_resp_sendstr(r,"{\"queued\":true}");
}
esp_err_t streamHandler(httpd_req_t *r) {
  httpd_resp_set_type(r,"multipart/x-mixed-replace;boundary=frame");httpd_resp_set_hdr(r,"Access-Control-Allow-Origin","*");
  uint32_t previous=0;
  while(true) {
    uint8_t *copy=nullptr;size_t len=0;xSemaphoreTake(frameLock,portMAX_DELAY);
    if(jpegLen&&frameSeq!=previous){len=jpegLen;copy=(uint8_t*)ps_malloc(len);if(copy){memcpy(copy,jpegCache,len);previous=frameSeq;}}
    xSemaphoreGive(frameLock);
    if(copy){char head[100];int n=snprintf(head,sizeof(head),"\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",(unsigned)len);esp_err_t e=httpd_resp_send_chunk(r,head,n);if(e==ESP_OK)e=httpd_resp_send_chunk(r,(char*)copy,len);free(copy);if(e!=ESP_OK)return e;}
    vTaskDelay(pdMS_TO_TICKS(80));
  }
}
void startServers() {
  httpd_config_t c=HTTPD_DEFAULT_CONFIG();c.server_port=80;c.stack_size=8192;c.max_uri_handlers=8;httpd_handle_t h=nullptr;
  if(httpd_start(&h,&c)==ESP_OK){httpd_uri_t p={.uri="/",.method=HTTP_GET,.handler=pageHandler};httpd_register_uri_handler(h,&p);p.uri="/api/state";p.handler=stateHandler;httpd_register_uri_handler(h,&p);p.uri="/capture";p.handler=captureHandler;httpd_register_uri_handler(h,&p);p.uri="/api/input";p.method=HTTP_POST;p.handler=inputHandler;httpd_register_uri_handler(h,&p);}
  c.server_port=81;c.ctrl_port=32769;c.stack_size=4096;c.max_open_sockets=2;if(httpd_start(&h,&c)==ESP_OK){httpd_uri_t p={.uri="/stream",.method=HTTP_GET,.handler=streamHandler};httpd_register_uri_handler(h,&p);}
}
void processVision(camera_fb_t *fb);
void visionTask(void*){while(true){xSemaphoreTakeRecursive(stateLock,portMAX_DELAY);bool enabled=visionMode!="OFF";xSemaphoreGiveRecursive(stateLock);if(enabled){camera_fb_t fb={};xSemaphoreTake(frameLock,portMAX_DELAY);if(jpegLen){fb.len=jpegLen;fb.buf=(uint8_t*)ps_malloc(fb.len);if(fb.buf){memcpy(fb.buf,jpegCache,fb.len);fb.width=320;fb.height=240;fb.format=PIXFORMAT_JPEG;}}xSemaphoreGive(frameLock);if(fb.buf){processVision(&fb);free(fb.buf);}}vTaskDelay(pdMS_TO_TICKS(visionIntervalMs));}}
void cameraTask(void*) {
  while(true) {camera_fb_t *fb=esp_camera_fb_get();if(fb){xSemaphoreTake(frameLock,portMAX_DELAY);if(fb->len<160000){memcpy(jpegCache,fb->buf,fb->len);jpegLen=fb->len;frameSeq++;}xSemaphoreGive(frameLock);esp_camera_fb_return(fb);}vTaskDelay(pdMS_TO_TICKS(200));}
}
void initCamera() {
  camera_config_t c={};c.ledc_channel=LEDC_CHANNEL_0;c.ledc_timer=LEDC_TIMER_0;
  c.pin_d0=15;c.pin_d1=17;c.pin_d2=18;c.pin_d3=16;c.pin_d4=14;c.pin_d5=12;c.pin_d6=11;c.pin_d7=48;
  c.pin_xclk=10;c.pin_pclk=13;c.pin_vsync=38;c.pin_href=47;c.pin_sccb_sda=40;c.pin_sccb_scl=39;c.pin_pwdn=-1;c.pin_reset=-1;
  c.xclk_freq_hz=20000000;c.pixel_format=PIXFORMAT_JPEG;c.frame_size=FRAMESIZE_QVGA;c.jpeg_quality=12;c.fb_count=2;c.fb_location=CAMERA_FB_IN_PSRAM;c.grab_mode=CAMERA_GRAB_LATEST;
  esp_err_t err=esp_camera_init(&c);cameraOk=err==ESP_OK;logLine("CAMERA",String("init=")+err+" psram="+ESP.getPsramSize());
  if(cameraOk){auto s=esp_camera_sensor_get();if(s->id.PID==OV3660_PID)s->set_vflip(s,1);jpegCache=(uint8_t*)ps_malloc(160000);if(jpegCache){xTaskCreatePinnedToCore(cameraTask,"camera",4096,nullptr,2,nullptr,0);xTaskCreatePinnedToCore(visionTask,"vision",16384,nullptr,1,nullptr,0);}else cameraOk=false;}
}
void setup() {
  hostLedInit();
  heap_caps_malloc_extmem_enable(1024);Serial.setTxBufferSize(4096);Serial.setRxBufferSize(8192);Serial.setTxTimeoutMs(20);Serial.begin(115200);uno.begin(57600,SERIAL_8N1,44,43);inputs=xQueueCreate(8,sizeof(Input));usbPackets=xQueueCreate(12,sizeof(String*));stateLock=xSemaphoreCreateRecursiveMutex();frameLock=xSemaphoreCreateMutex();xTaskCreate(usbTask,"usb-log",4096,nullptr,1,nullptr);
  prefs.begin("cat-host",false);defaults();String saved=prefs.getString("config","");if(saved.length()){JsonDocument d;if(!deserializeJson(d,saved)){JsonDocument candidate;candidate.set(config);for(JsonPair kv:d.as<JsonObject>())candidate[kv.key()]=kv.value();String error;if(validateSettings(candidate,error))config=candidate;else logLine("CONFIG_ERROR","saved config rejected: "+error);}}
  WiFi.mode(WIFI_AP_STA);WiFi.softAP("ESP32-CatHost","catrobot32");String ssid=prefs.getString("ssid","");if(ssid.length())WiFi.begin(ssid.c_str(),prefs.getString("password","").c_str());
  logLine("READY","ESP32_CAT_HOST AP=192.168.4.1 HTTP=80 STREAM=81 UART_RX=44 TX=43 BAUD=57600 mode=SIM_UNO");initCamera();startServers();
}
void readLines(Stream &s,String &buffer,bool physical) {static Input in;static bool usbOverflow=false,uartOverflow=false;bool &overflow=physical?uartOverflow:usbOverflow;int count=0;while(s.available()&&count++<1024){char c=s.read();if(c=='\n'){if(!overflow){if(physical)acceptUno(buffer,true);else {buffer.toCharArray(in.text,sizeof(in.text));handleInput(in);}}overflow=false;buffer="";}else if(c!='\r'&&!overflow){if(buffer.length()<4095)buffer+=c;else{buffer="";overflow=true;logLine("OVERFLOW","serial line discarded");}}}}
void loop() {
  xSemaphoreTakeRecursive(stateLock,portMAX_DELAY);readLines(Serial,usbLine,false);readLines(uno,uartLine,true);static Input in;for(int i=0;i<4&&xQueueReceive(inputs,&in,0)==pdTRUE;i++)handleInput(in);
  debugTick();policyTick();hostLedTick();xSemaphoreGiveRecursive(stateLock);
  if(telemetryId&&millis()-telemetryAt>5000){telemetryId=0;lastPoll=millis();logLine("TELEMETRY_TIMEOUT","retry in 2s");}
  if(!policyPreparing()&&(int32_t)(millis()-lastPoll)>=2000&&!telemetryId&&outgoing.size()<4){lastPoll=millis();telemetryAt=millis();telemetryId=sendUno("GET_TELEMETRY");}
  flushTx();
  delay(2);
}



