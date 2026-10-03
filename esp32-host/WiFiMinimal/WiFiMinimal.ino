#include <Arduino.h>
#include <WiFi.h>
#include "secrets.local.h"

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("READY WIFI_MINIMAL");
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      Serial.printf("DISCONNECTED reason=%u\n", info.wifi_sta_disconnected.reason);
    }
  });
  WiFi.mode(WIFI_STA);
  Serial.printf("Connecting to SSID: [%s]\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("CONNECTED IP=%s RSSI=%d uptime=%lus\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI(), millis() / 1000);
  } else {
    Serial.printf("WAIT status=%d uptime=%lus\n", WiFi.status(), millis() / 1000);
  }
  delay(1000);
}
