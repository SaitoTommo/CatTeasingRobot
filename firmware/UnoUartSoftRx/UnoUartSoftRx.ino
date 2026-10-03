#include <Arduino.h>
#include <SoftwareSerial.h>
#include <string.h>

// Receive only on D12. SoftwareSerial requires a TX argument, but its
// write methods are never used: hardware Serial alone owns D1 transmission.
SoftwareSerial hostRx(12, 1);
char input[64];
uint32_t rxBytes = 0, reportedRxBytes = 0, reportAt = 0;
uint8_t used = 0;
bool overflow = false, awaitingPong = false;
uint32_t nextPing = 0, pingAt = 0, pongCount = 0;

void sendLine(const char *line) { Serial.println(line); }

void acceptLine() {
  input[used] = '\0';
  // USB and GPIO share Serial. DBG output is also transmitted to ESP;
  // the peer ignores DBG lines and never echoes them.
  Serial.print(F("DBG UNO RXHEX"));
  for (uint8_t i = 0; i < used; ++i) {
    Serial.print(' ');
    if ((uint8_t)input[i] < 16) Serial.print('0');
    Serial.print((uint8_t)input[i], HEX);
  }
  Serial.println();
  if (overflow) { Serial.println(F("DBG UNO LINE_OVERFLOW")); return; }
  if (strcmp(input, "PING ESP") == 0) {
    sendLine("PONG UNO");
    Serial.println(F("DBG UNO REPLIED_TO_ESP"));
  } else if (strcmp(input, "PONG ESP") == 0 && awaitingPong) {
    awaitingPong = false;
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    Serial.print(F("DBG UNO ROUND_TRIP_OK count="));
    Serial.println(++pongCount);
  } else Serial.println(F("DBG UNO UNEXPECTED_LINE"));
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  hostRx.begin(9600);
  Serial.begin(9600);
  Serial.println(F("DBG READY UNO_UART_SOFT_RX_1 baud=9600 RX=D12 TX=D1"));
  nextPing = millis() + 3000;
}

void loop() {
  while (hostRx.available()) {
    char c = hostRx.read();
    ++rxBytes;
    if (c == '\r') continue;
    if (c == '\n') { acceptLine(); used = 0; overflow = false; }
    else if (used < sizeof(input) - 1) input[used++] = c;
    else overflow = true;
  }
  uint32_t now = millis();
  if (rxBytes != reportedRxBytes && now - reportAt >= 1000) {
    reportedRxBytes = rxBytes; reportAt = now;
    Serial.print(F("DBG UNO SOFT_RX_BYTES=")); Serial.println(rxBytes);
  }
  if (hostRx.overflow()) Serial.println(F("DBG UNO SOFT_RX_OVERFLOW"));
  if (awaitingPong && now - pingAt >= 1500) {
    awaitingPong = false;
    Serial.println(F("DBG UNO PONG_TIMEOUT"));
  }
  if ((int32_t)(now - nextPing) >= 0) {
    nextPing = now + 4000; pingAt = now; awaitingPong = true;
    sendLine("PING UNO");
  }
}
