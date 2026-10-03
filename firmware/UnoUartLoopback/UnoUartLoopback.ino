#include <Arduino.h>
#include <string.h>

// Uno Serial is shared by USB and D0/D1. Upload with both pins disconnected;
// afterwards connect D1 to D0. Received test packets are never echoed again.
char received[96], expected[64];
uint8_t used = 0;
bool overflow = false, waiting = false;
uint32_t sequence = 0, sentAt = 0, nextAt = 0, passed = 0, failed = 0;

void handleLine() {
  received[used] = '\0';
  if (waiting && !overflow && strcmp(received, expected) == 0) {
    waiting = false;
    ++passed;
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    Serial.print(F("LOOPBACK_OK id=")); Serial.print(sequence);
    Serial.print(F(" passed=")); Serial.print(passed);
    Serial.print(F(" failed=")); Serial.println(failed);
  }
  // Our READY/result lines also return through the jumper; ignore them.
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(57600);
  Serial.println(F("READY UNO_UART_LOOPBACK_1 baud=57600"));
  nextAt = millis() + 2000;
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      handleLine(); used = 0; overflow = false;
    } else if (used < sizeof(received) - 1) received[used++] = c;
    else overflow = true;
  }
  uint32_t now = millis();
  if (waiting && now - sentAt >= 500) {
    waiting = false; ++failed;
    Serial.print(F("LOOPBACK_TIMEOUT id=")); Serial.print(sequence);
    Serial.print(F(" passed=")); Serial.print(passed);
    Serial.print(F(" failed=")); Serial.println(failed);
    // A partial packet must not contaminate the next test.
    used = 0; overflow = false;
  }
  if (!waiting && (int32_t)(now - nextAt) >= 0) {
    ++sequence;
    snprintf(expected, sizeof(expected), "LB_TEST:%lu:0123456789:AbCdEf", (unsigned long)sequence);
    waiting = true; sentAt = now; nextAt = now + 2000;
    Serial.println(expected);
  }
}
