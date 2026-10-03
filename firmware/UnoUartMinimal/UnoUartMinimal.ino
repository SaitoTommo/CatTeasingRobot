#include <Arduino.h>
#include <string.h>

char input[64];
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
  Serial.begin(9600);
  Serial.println(F("DBG READY UNO_UART_MINIMAL_1 baud=9600"));
  nextPing = millis() + 3000;
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') { acceptLine(); used = 0; overflow = false; }
    else if (used < sizeof(input) - 1) input[used++] = c;
    else overflow = true;
  }
  uint32_t now = millis();
  if (awaitingPong && now - pingAt >= 1500) {
    awaitingPong = false;
    Serial.println(F("DBG UNO PONG_TIMEOUT"));
  }
  if ((int32_t)(now - nextPing) >= 0) {
    nextPing = now + 4000; pingAt = now; awaitingPong = true;
    sendLine("PING UNO");
  }
}
