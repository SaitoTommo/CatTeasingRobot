#include <Arduino.h>
#include <string.h>

// USB and D0/D1 share Serial on Uno. Replies are already visible to USB;
// never echo them again, otherwise the two boards can form an endless loop.
char line[160];
uint8_t used = 0;
bool overflow = false, automatic = true;
uint32_t lastSend = 0, sequence = 0;

void sendRequest(const char *text) {
  Serial.print(F("EVENT UART_BENCH_REQ "));
  Serial.println(text);
}

void handleLine() {
  line[used] = '\0';
  if (!used || overflow) return;
  if (strncmp(line, "EVENT UART_BENCH_REPLY ", 23) == 0) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    // One receipt marker, not another request. Also visible in USB monitor.
    Serial.print(F("EVENT UART_BENCH_RECEIVED "));
    Serial.println(line + 23);
  } else if (strcmp(line, "AUTO OFF") == 0) {
    automatic = false;
    Serial.println(F("EVENT UART_BENCH_AUTO OFF"));
  } else if (strcmp(line, "AUTO ON") == 0) {
    automatic = true;
    Serial.println(F("EVENT UART_BENCH_AUTO ON"));
  } else if (strncmp(line, "EVENT ", 6) != 0 && strncmp(line, "C ", 2) != 0) {
    sendRequest(line);
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(9600);
  Serial.println(F("READY UNO_UART_BENCH_1 baud=9600"));
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      handleLine(); used = 0; overflow = false;
    } else if (used < sizeof(line) - 1) {
      line[used++] = c;
    } else overflow = true;
  }
  if (automatic && millis() - lastSend >= 2000) {
    lastSend = millis();
    char message[32];
    snprintf(message, sizeof(message), "HELLO_%lu", (unsigned long)++sequence);
    sendRequest(message);
  }
}
