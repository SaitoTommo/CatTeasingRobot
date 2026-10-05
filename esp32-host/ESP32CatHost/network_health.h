#pragma once
constexpr uint8_t CONTROL_HTTP_MAX_SOCKETS=9,STREAM_HTTP_MAX_SOCKETS=4;
constexpr uint8_t SHARED_HTTP_MAX_SOCKETS=CONTROL_HTTP_MAX_SOCKETS+STREAM_HTTP_MAX_SOCKETS;
// One HTTP service shares its three internal sockets across API and video.
static_assert(SHARED_HTTP_MAX_SOCKETS+3<=CONFIG_LWIP_MAX_SOCKETS,"HTTP server exceeds the SDK socket budget");
// Event callbacks never take controller locks or write logs. loop() owns records.
struct NetworkEvent { arduino_event_id_t id; uint8_t reason; };
QueueHandle_t networkEvents=nullptr;
String networkSsid;
uint32_t wifiDisconnects=0,wifiReconnectAttempts=0,wifiLastDisconnectAt=0,wifiRetryAt=0;
uint8_t wifiLastReason=0;
void networkEvent(WiFiEvent_t id,WiFiEventInfo_t info){
 if(id!=ARDUINO_EVENT_WIFI_STA_DISCONNECTED&&id!=ARDUINO_EVENT_WIFI_STA_GOT_IP)return;
 NetworkEvent event={id,id==ARDUINO_EVENT_WIFI_STA_DISCONNECTED?info.wifi_sta_disconnected.reason:uint8_t(0)};
 if(networkEvents)xQueueSend(networkEvents,&event,0);
}
void networkBegin(const String &ssid,const String &password){
 networkSsid=ssid;wifiRetryAt=millis();
 if(ssid.length())WiFi.begin(ssid.c_str(),password.c_str());
}
void networkTick(){
 NetworkEvent event;
 while(networkEvents&&xQueueReceive(networkEvents,&event,0)==pdTRUE){
  if(event.id==ARDUINO_EVENT_WIFI_STA_DISCONNECTED){
   wifiDisconnects++;wifiLastReason=event.reason;wifiLastDisconnectAt=millis();
   logLine("WIFI_DISCONNECTED","reason="+String(event.reason)+"; AP remains available");
  }else logLine("WIFI_GOT_IP",WiFi.localIP().toString()+" RSSI="+String(WiFi.RSSI()));
 }
 // Core auto-reconnect handles normal transient losses. Retry failed association
 // too, with a long interval so scans do not continuously disturb the soft AP.
 if(WiFi.status()==WL_CONNECTED){wifiRetryAt=millis();return;}
 if(networkSsid.length()&&millis()-wifiRetryAt>=30000){
  wifiRetryAt=millis();wifiReconnectAttempts++;
  WiFi.reconnect();logLine("WIFI_RECONNECT","station retry="+String(wifiReconnectAttempts));
 }
}
void networkSnapshot(JsonDocument &d){
 auto n=d["network"].to<JsonObject>();bool connected=WiFi.status()==WL_CONNECTED;
 n["stationConnected"]=connected;n["status"]=int(WiFi.status());
 if(connected)n["rssiDbm"]=WiFi.RSSI();else n["rssiDbm"]=nullptr;
 n["disconnects"]=wifiDisconnects;n["lastDisconnectReason"]=wifiLastReason;
 n["lastDisconnectAt"]=wifiLastDisconnectAt;n["reconnectAttempts"]=wifiReconnectAttempts;
 n["sleepEnabled"]=WiFi.getSleep()!=WIFI_PS_NONE;
 n["controlMaxSockets"]=CONTROL_HTTP_MAX_SOCKETS;n["streamMaxSockets"]=STREAM_HTTP_MAX_SOCKETS;
 n["sdkMaxSockets"]=CONFIG_LWIP_MAX_SOCKETS;
 n["sharedHttpMaxSockets"]=SHARED_HTTP_MAX_SOCKETS;n["httpInternalSockets"]=3;
 n["resetReason"]=int(esp_reset_reason());
 n["minFreeHeap"]=ESP.getMinFreeHeap();
 n["largestFreeBlock"]=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
}
