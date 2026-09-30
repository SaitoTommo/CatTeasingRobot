#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_camera.h>
#include <esp_http_server.h>
#include "debug_page.h"

// Control work stays on loop(); HTTP and camera tasks only enqueue input.
struct Input { char text[4096]; bool physical; };
QueueHandle_t inputs;
SemaphoreHandle_t stateLock, frameLock;
Preferences prefs;
HardwareSerial uno(1);
String usbLine, uartLine, mode="SIM_UNO", phase="IDLE", lastRx, lastTx;
JsonDocument config;
JsonDocument observation;
String logs[160]; uint32_t logSeq=0, dropped=0, lastUnoAt=0, lastPoll=0;
uint8_t *jpegCache=nullptr; size_t jpegLen=0; uint32_t frameSeq=0;
bool cameraOk=false;
uint8_t crc8(const String &s) { uint8_t c=0; for(size_t n=0;n<s.length();n++) {c^=s[n];for(int j=0;j<8;j++)c=(c<<1)^((c&128)?7:0);}return c; }
void logLine(const char *source,const String &line) {
  JsonDocument d; d["seq"]=++logSeq; d["ms"]=millis();d["source"]=source;d["line"]=line;
  String out;serializeJson(d,out);
  xSemaphoreTakeRecursive(stateLock,portMAX_DELAY); logs[(logSeq-1)%160]=out;if(logSeq>160)dropped++;xSemaphoreGiveRecursive(stateLock);
  Serial.println(out);
}
uint32_t cmdSeq=0;
uint32_t sendUno(const String &command) {
  uint32_t id=++cmdSeq;String payload="C "+String(id)+" "+command;char suffix[5];snprintf(suffix,sizeof(suffix),"*%02X",crc8(payload));
  lastTx=payload+suffix;logLine(mode=="UART"?"UNO_TX":"SIM_TX",lastTx);
  if(mode=="UART")uno.println(lastTx);
  return id;
}
void policyInput(const String &line);
void policyTick();
void policyControl(const String &action);
void acceptUno(String line,bool physical) {
  line.trim(); if(!line.length())return;
  if((mode=="UART")!=physical){logLine("INPUT_REJECTED","source does not match mode");return;}
  int star=line.indexOf('*');
  if(star>=0) {String hex=line.substring(star+1);String payload=line.substring(0,star);if(hex.length()!=2||strtoul(hex.c_str(),nullptr,16)!=crc8(payload)){logLine("CRC_ERROR",line);return;}line=payload;}
  else if(line.startsWith("MP ")||line.startsWith("MS ")||line.startsWith("MD ")||line.startsWith("MA ")||line.startsWith("AK ")){logLine("CRC_MISSING",line);return;}
  lastRx=line;lastUnoAt=millis();logLine(physical?"UNO_RX":"SIM_RX",line);policyInput(line);
}
void defaults() {
  config["route"][0]=0;config["route"][1]=1;config["route"][2]=2;config["route"][3]=3;
  config["searchPwm"]=75;config["alignPwm"]=52;config["centerTolerancePercent"]=5.5;
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
  config["frontCalibrated"]=false;config["frontBlockBottom"]=0.88;
}
String snapshot() {
  JsonDocument d;d["firmware"]="ESP32_CAT_HOST_0.1";d["ms"]=millis();d["mode"]=mode;d["phase"]=phase;
  d["lastUnoAgeMs"]=lastUnoAt?millis()-lastUnoAt:UINT32_MAX;d["lastRx"]=lastRx;d["lastTx"]=lastTx;
  d["cameraOk"]=cameraOk;d["frameSeq"]=frameSeq;d["heap"]=ESP.getFreeHeap();d["psram"]=ESP.getFreePsram();
  d["apIP"]=WiFi.softAPIP().toString();d["stationIP"]=WiFi.localIP().toString();d["config"]=config;d["vision"]=observation;
  d["droppedLogs"]=dropped;JsonArray arr=d["logs"].to<JsonArray>();uint32_t start=logSeq>160?logSeq-160:0;
  for(uint32_t n=start;n<logSeq;n++){JsonDocument item;deserializeJson(item,logs[n%160]);arr.add(item);}
  String out;serializeJson(d,out);return out;
}
void handleInput(const Input &in) {
  JsonDocument d;if(deserializeJson(d,in.text)){logLine("INPUT_ERROR","invalid JSON");return;}
  String op=d["op"]|"";
  if(op=="uno")acceptUno(d["line"]|"",in.physical);
  else if(op=="state"){xSemaphoreTakeRecursive(stateLock,portMAX_DELAY);String out=snapshot();xSemaphoreGiveRecursive(stateLock);Serial.println(out);}
  else if(op=="control")policyControl(d["action"]|"");
  else if(op=="command") {if(phase!="IDLE"&&phase!="STOPPED"){logLine("INPUT_ERROR","stop policy before manual commands");return;}sendUno(d["command"]|"PING");}
  else if(op=="mode") {policyControl("stop");mode=String(d["mode"]|"SIM_UNO")=="UART"?"UART":"SIM_UNO";lastUnoAt=0;logLine("MODE",mode);}
  else if(op=="vision" && mode=="SIM_UNO") {observation=d["vision"];observation["source"]="SIM_CAMERA";observation["at"]=millis();}
  else if(op=="config") {
    if(phase!="IDLE"&&phase!="STOPPED"){logLine("CONFIG_ERROR","stop first");return;}
    JsonObject obj=d["config"].as<JsonObject>();
    if(obj["route"].is<JsonArray>()) {auto route=obj["route"].as<JsonArray>();if(!route.size()||route.size()>128){logLine("CONFIG_ERROR","route needs 1..128 IDs");return;}for(JsonVariant v:route)if(!v.is<int>()||v.as<int>()<0||v.as<int>()>586){logLine("CONFIG_ERROR","tag36h11 IDs must be 0..586");return;}}
    for(JsonPair kv:obj) {if(kv.key()=="route"||kv.key()=="frontCalibrated"||config[kv.key()].isNull()||!kv.value().is<float>())continue;float v=kv.value();if(!isfinite(v)||v<0||v>60000){logLine("CONFIG_ERROR","invalid numeric setting");return;}}
    for(JsonPair kv:obj)if(!config[kv.key()].isNull())config[kv.key()]=kv.value();
    String saved;serializeJson(config,saved);prefs.putString("config",saved);logLine("CONFIG","saved");
  }
  else if(op=="wifi") {String ssid=d["ssid"]|"",pass=d["password"]|"";prefs.putString("ssid",ssid);prefs.putString("password",pass);WiFi.begin(ssid.c_str(),pass.c_str());logLine("WIFI","station connecting; AP remains available");}
  else logLine("INPUT_ERROR","unsupported op or source");
}
esp_err_t pageHandler(httpd_req_t *r) {httpd_resp_set_type(r,"text/html; charset=utf-8");return httpd_resp_send(r,DEBUG_PAGE,HTTPD_RESP_USE_STRLEN);}
esp_err_t stateHandler(httpd_req_t *r) {xSemaphoreTakeRecursive(stateLock,portMAX_DELAY);String s=snapshot();xSemaphoreGiveRecursive(stateLock);httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Cache-Control","no-store");httpd_resp_set_hdr(r,"Access-Control-Allow-Origin","*");return httpd_resp_send(r,s.c_str(),s.length());}
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
  if(httpd_start(&h,&c)==ESP_OK){httpd_uri_t p={.uri="/",.method=HTTP_GET,.handler=pageHandler};httpd_register_uri_handler(h,&p);p.uri="/api/state";p.handler=stateHandler;httpd_register_uri_handler(h,&p);p.uri="/api/input";p.method=HTTP_POST;p.handler=inputHandler;httpd_register_uri_handler(h,&p);}
  c.server_port=81;c.ctrl_port=32769;c.stack_size=4096;c.max_open_sockets=2;if(httpd_start(&h,&c)==ESP_OK){httpd_uri_t p={.uri="/stream",.method=HTTP_GET,.handler=streamHandler};httpd_register_uri_handler(h,&p);}
}
void processVision(camera_fb_t *fb);
void cameraTask(void*) {
  while(true) {camera_fb_t *fb=esp_camera_fb_get();if(fb){xSemaphoreTake(frameLock,portMAX_DELAY);if(fb->len<160000){memcpy(jpegCache,fb->buf,fb->len);jpegLen=fb->len;frameSeq++;}xSemaphoreGive(frameLock);processVision(fb);esp_camera_fb_return(fb);}vTaskDelay(pdMS_TO_TICKS(40));}
}
void initCamera() {
  camera_config_t c={};c.ledc_channel=LEDC_CHANNEL_0;c.ledc_timer=LEDC_TIMER_0;
  c.pin_d0=15;c.pin_d1=17;c.pin_d2=18;c.pin_d3=16;c.pin_d4=14;c.pin_d5=12;c.pin_d6=11;c.pin_d7=48;
  c.pin_xclk=10;c.pin_pclk=13;c.pin_vsync=38;c.pin_href=47;c.pin_sccb_sda=40;c.pin_sccb_scl=39;c.pin_pwdn=-1;c.pin_reset=-1;
  c.xclk_freq_hz=20000000;c.pixel_format=PIXFORMAT_JPEG;c.frame_size=FRAMESIZE_QVGA;c.jpeg_quality=12;c.fb_count=2;c.fb_location=CAMERA_FB_IN_PSRAM;c.grab_mode=CAMERA_GRAB_LATEST;
  esp_err_t err=esp_camera_init(&c);cameraOk=err==ESP_OK;logLine("CAMERA",String("init=")+err+" psram="+ESP.getPsramSize());
  if(cameraOk){auto s=esp_camera_sensor_get();if(s->id.PID==OV3660_PID)s->set_vflip(s,1);jpegCache=(uint8_t*)ps_malloc(160000);if(jpegCache)xTaskCreatePinnedToCore(cameraTask,"camera",16384,nullptr,1,nullptr,0);else cameraOk=false;}
}
void setup() {
  Serial.begin(115200);uno.begin(57600,SERIAL_8N1,44,43);inputs=xQueueCreate(8,sizeof(Input));stateLock=xSemaphoreCreateRecursiveMutex();frameLock=xSemaphoreCreateMutex();
  prefs.begin("cat-host",false);defaults();String saved=prefs.getString("config","");if(saved.length()){JsonDocument d;if(!deserializeJson(d,saved))for(JsonPair kv:d.as<JsonObject>())config[kv.key()]=kv.value();}
  WiFi.mode(WIFI_AP_STA);WiFi.softAP("ESP32-CatHost","catrobot32");String ssid=prefs.getString("ssid","");if(ssid.length())WiFi.begin(ssid.c_str(),prefs.getString("password","").c_str());
  logLine("READY","ESP32_CAT_HOST AP=192.168.4.1 HTTP=80 STREAM=81 UART_RX=44 TX=43 BAUD=57600 mode=SIM_UNO");initCamera();startServers();
}
void readLines(Stream &s,String &buffer,bool physical) {int count=0;while(s.available()&&count++<1024){char c=s.read();if(c=='\n'){if(physical)acceptUno(buffer,true);else {Input in={};buffer.toCharArray(in.text,sizeof(in.text));handleInput(in);}buffer="";}else if(c!='\r'){if(buffer.length()<4095)buffer+=c;else{buffer="";logLine("OVERFLOW","serial line discarded");}}}}
void loop() {
  xSemaphoreTakeRecursive(stateLock,portMAX_DELAY);readLines(Serial,usbLine,false);readLines(uno,uartLine,true);Input in;for(int i=0;i<4&&xQueueReceive(inputs,&in,0)==pdTRUE;i++)handleInput(in);
  policyTick();xSemaphoreGiveRecursive(stateLock);
  if(millis()-lastPoll>=300){lastPoll=millis();if(mode=="UART")sendUno("GET_TELEMETRY");}
  delay(2);
}


