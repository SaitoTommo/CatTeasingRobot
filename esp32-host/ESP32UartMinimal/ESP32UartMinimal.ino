#include <Arduino.h>
#include <string.h>

HardwareSerial benchUart(1);
char input[64];
uint8_t used = 0;
bool overflow = false, awaitingPong = false;
uint32_t nextPing = 0, pingAt = 0, pongCount = 0;

void sendLine(const char *line) {
  benchUart.println(line);
  Serial.print("TX "); Serial.println(line);
}

void acceptLine() {
  input[used] = '\0';
  Serial.print("RXHEX");
  for (uint8_t i = 0; i < used; ++i) {
    Serial.print(' ');
    if ((uint8_t)input[i] < 16) Serial.print('0');
    Serial.print((uint8_t)input[i], HEX);
  }
  Serial.println();
  if (overflow) { Serial.println("LINE_OVERFLOW"); return; }
  if (strcmp(input, "PING UNO") == 0) {
    Serial.println("RX PING UNO");
    sendLine("PONG ESP");
  } else if (strcmp(input, "PONG UNO") == 0 && awaitingPong) {
    awaitingPong = false;
    Serial.print("ROUND_TRIP_OK count="); Serial.println(++pongCount);
  } else if (strncmp(input, "DBG ", 4) == 0) {
    Serial.println(input);
  } else Serial.println("UNEXPECTED_LINE");
}

void setup() {
  Serial.begin(115200);
  benchUart.begin(9600, SERIAL_8N1, 44, 43);
  // Do not wait for a USB monitor; GPIO UART works without one.
  Serial.println("READY ESP32_UART_MINIMAL_1 UART=9600 USB=115200 RX=D7/GPIO44 TX=D6/GPIO43");
  nextPing = millis() + 1000;
}

void loop() {
  while (benchUart.available()) {
    char c = benchUart.read();
    if (c == '\r') continue;
    if (c == '\n') { acceptLine(); used = 0; overflow = false; }
    else if (used < sizeof(input) - 1) input[used++] = c;
    else overflow = true;
  }
  uint32_t now = millis();
  if (awaitingPong && now - pingAt >= 1500) {
    awaitingPong = false;
    Serial.println("PONG_TIMEOUT");
  }
  if ((int32_t)(now - nextPing) >= 0) {
    nextPing = now + 4000; pingAt = now; awaitingPong = true;
    sendLine("PING ESP");
  }
  delay(1);
}
