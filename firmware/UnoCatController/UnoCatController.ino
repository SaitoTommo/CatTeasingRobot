#include <Arduino.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <SoftwareSerial.h>
#include "scan_machine.h"

// D12 receives the ESP command channel at 57600; D1 remains hardware TX.
// SoftwareSerial requires a TX pin but its write methods are never used.
// USB commands remain available on D0 with their own line assembly buffer.
constexpr uint8_t PIN_HOST_RX = 12;
SoftwareSerial hostRx(PIN_HOST_RX, 1);

// SRAM breadcrumbs survive some resets, not loss of power. No EEPROM writes,
// watchdog reset or serial output from an interrupt is used for diagnostics.
struct DiagnosticBreadcrumb {
  uint16_t magic, command, commandInverse;
  uint8_t stage, stageInverse;
};
volatile DiagnosticBreadcrumb diagBreadcrumb __attribute__((section(".noinit")));
uint8_t diagMode = 1; // 0 off, 1 heartbeat, 2 command/telemetry trace as well.
uint16_t diagLoops = 0, diagMaxLoopMs = 0, diagRxOverflow = 0, diagDropped = 0;
uint32_t diagLastAt = 0;

void diagStage(uint8_t stage) {
  diagBreadcrumb.stage = stage;
  diagBreadcrumb.stageInverse = static_cast<uint8_t>(~stage);
}
void diagCommand(uint16_t id) {
  diagBreadcrumb.command = id;
  diagBreadcrumb.commandInverse = static_cast<uint16_t>(~id);
}
void diagWrite(const char *line) {
  const size_t length = strlen(line);
  // Write only a whole line that fits the TX ring; never wait for headroom.
  if (length + 2 > static_cast<size_t>(Serial.availableForWrite())) return;
  Serial.write(reinterpret_cast<const uint8_t *>(line), length);
  Serial.write('\r'); Serial.write('\n');
}
void diagTrace(uint8_t stage) {
  diagStage(stage);
  if (diagMode != 2) return;
  char line[40];
  snprintf_P(line, sizeof(line), PSTR("EVENT DIAG S %u %u"), stage, diagBreadcrumb.command);
  diagWrite(line);
}
// L298N: left wheel OUT3/OUT4 (IN3/IN4/ENB); right wheel OUT1/OUT2 (IN1/IN2/ENA).
constexpr uint8_t PIN_ENC_LEFT = 2;
constexpr uint8_t PIN_ENC_RIGHT = 3;
constexpr uint8_t PIN_L_IN1 = 8;
constexpr uint8_t PIN_L_PWM = 6;
constexpr uint8_t PIN_R_PWM = 5;
constexpr uint8_t PIN_L_IN2 = 9;
constexpr uint8_t PIN_R_IN1 = 4;
constexpr uint8_t PIN_R_IN2 = 7;
// PWM is allowed at 1..255; actual starting/holding PWM requires measurement.
constexpr uint8_t MOTOR_MIN_DRIVE_PWM = 1;
constexpr uint8_t SONAR_TRIG_PINS[2] = { 10, A4 };  // LEFT, RIGHT
constexpr uint8_t SONAR_ECHO_PINS[2] = { 11, A5 };
constexpr uint8_t PIN_BUZZER = 13;  // LED output is delegated to ESP GPIO21.
constexpr uint8_t TOUCH_PINS[4] = { A0, A1, A2, A3 };
const char TOUCH_NAMES[4][6] PROGMEM = { "TOP", "LEFT", "RIGHT", "REAR" };

#ifndef UNO_ENABLE_DEBUG_COMMANDS
#define UNO_ENABLE_DEBUG_COMMANDS 1
#endif

constexpr uint8_t SLOTS_PER_WHEEL_REV = 20;         // One FALLING edge per slot, on the wheel axle.
constexpr float DEFAULT_WHEEL_DIAMETER_MM = 67.5f;  // Measured wheel diameter.
constexpr float DEFAULT_SAFE_CM = 1.0f;
constexpr float MIN_SAFE_CM = 1.0f;
constexpr float MAX_SAFE_CM = 100.0f;
constexpr uint32_t TOUCH_DEBOUNCE_MS = 45;
constexpr uint32_t SONAR_PERIOD_MS = 60;
constexpr uint32_t SONAR_TIMEOUT_US = 24000;
constexpr uint32_t SONAR_FRESH_MS = 500;
constexpr uint32_t CAMERA_FRESH_MS = 500;
constexpr uint32_t ENCODER_STALL_MS = 1200;

volatile uint32_t wheelPulses[2] = { 0, 0 };
volatile uint32_t lastEdgeUs[2] = { 0, 0 };
void onLeftPulse() {
  const uint32_t now = micros();
  if (static_cast<uint32_t>(now - lastEdgeUs[0]) >= 150) {
    ++wheelPulses[0];
    lastEdgeUs[0] = now;
  }
}
void onRightPulse() {
  const uint32_t now = micros();
  if (static_cast<uint32_t>(now - lastEdgeUs[1]) >= 150) {
    ++wheelPulses[1];
    lastEdgeUs[1] = now;
  }
}

struct TouchState {
  bool physicalRaw;
  bool stable;
  uint32_t changedAt;
  uint32_t pressedAt;
  int8_t overrideValue;  // -1 = physical, 0 = released, 1 = pressed.
};
TouchState touches[4];

enum SonarPhase : uint8_t { SONAR_IDLE,
                            SONAR_WAIT_RISE,
                            SONAR_WAIT_FALL };
SonarPhase sonarPhase = SONAR_IDLE;
uint8_t activeSonar = 0;
uint32_t sonarAtUs = 0;
uint32_t echoRiseUs = 0;
uint32_t lastSonarStartMs = 0;
uint32_t lastDistanceMs[2] = { 0, 0 };
float physicalDistanceCm[2] = { 0, 0 };
bool physicalDistanceValid[2] = { false, false };
enum DistanceOverride : uint8_t { DISTANCE_AUTO,
                                  DISTANCE_VALUE,
                                  DISTANCE_INVALID };
DistanceOverride distanceOverride[2] = { DISTANCE_AUTO, DISTANCE_AUTO };
float injectedDistanceCm[2] = { 0, 0 };
uint32_t injectedDistanceAtMs[2] = { 0, 0 };
float safeDistanceCm[2] = { DEFAULT_SAFE_CM, DEFAULT_SAFE_CM };
enum CameraFrontState : uint8_t { CAMERA_INVALID,
                                  CAMERA_CLEAR,
                                  CAMERA_BLOCKED,
                                  CAMERA_DISTANCE };
CameraFrontState cameraFrontState = CAMERA_INVALID;
float cameraFrontDistanceCm = 0;
uint32_t lastCameraFrontMs = 0;

bool sessionStopped = false;
bool debugEnabled = false;
bool boardDryRun = false; // Allows simulated inputs, independently of physical outputs.
enum IoChannel : uint8_t { MOTOR_LEFT, MOTOR_RIGHT, LED_OUT, BUZZER_OUT, SONAR_LEFT_TRIG, SONAR_RIGHT_TRIG, SONAR_LEFT_ECHO, SONAR_RIGHT_ECHO, ENCODER_LEFT, ENCODER_RIGHT, TOUCH_TOP, TOUCH_LEFT, TOUCH_RIGHT, TOUCH_REAR, IO_COUNT };
const char IO_NAMES[IO_COUNT][17] PROGMEM = { "MOTOR_LEFT", "MOTOR_RIGHT", "LED", "BUZZER", "SONAR_LEFT_TRIG", "SONAR_RIGHT_TRIG", "SONAR_LEFT_ECHO", "SONAR_RIGHT_ECHO", "ENCODER_LEFT", "ENCODER_RIGHT", "TOUCH_TOP", "TOUCH_LEFT", "TOUCH_RIGHT", "TOUCH_REAR" };
bool physicalIo[IO_COUNT] = { true,true,true,true,true,true,true,true,true,true,true,true,true,true };
bool ledOn = false;
bool ledFlash = false;
uint32_t ledFlashUntilMs = 0;
uint32_t ledFlashIntervalMs = 0;
uint32_t ledFlashStartedMs = 0;
uint32_t buzzerUntilMs = 0;
uint8_t remoteLedMode = 0;
uint32_t remoteLedDeadline = 0, remoteLedInterval = 0;
float wheelDiameterMm = DEFAULT_WHEEL_DIAMETER_MM;
float trackWidthMm = 120.0f;  // User-selected default; SET_GEOMETRY may override it.
float rpm[2] = { 0, 0 };
uint32_t rpmAtMs = 0;
uint32_t rpmPulses[2] = { 0, 0 };
float dryRunFraction[2] = { 0, 0 };
uint32_t dryRunAtMs = 0;

enum MotionKind : uint8_t { MOTION_NONE,
                            MOTION_TIMED,
                            MOTION_PULSES,
                            MOTION_CONTINUOUS };
struct Motion {
  MotionKind kind;
  uint16_t id;
  int16_t left;
  int16_t right;
  uint32_t startedAtMs;
  uint32_t deadlineMs;
  uint32_t basePulses[2];
  uint32_t lastPulse[2];
  uint32_t lastProgressMs[2];
  uint32_t targetPulses;
  bool estimated;
};
Motion motion = { MOTION_NONE, 0, 0, 0, 0, 0, { 0, 0 }, { 0, 0 }, { 0, 0 }, 0, false };
// Applies only to one bounded TEST_MOTOR transaction, never patrol commands.
bool motionBypassAvoidance = false;
ScanMachine scan;

struct GuardedCommandBuffer {
  uint16_t before = 0xA55A;
  char data[128] = {};
  uint16_t after = 0x5AA5;
  bool valid() const { return before == 0xA55A && after == 0x5AA5; }
};
GuardedCommandBuffer inputBuffers[2], pendingBuffer, executingBuffer;
uint8_t inputLength[2] = { 0, 0 };
bool inputOverflow[2] = { false, false };

void getPulses(uint32_t &left, uint32_t &right) {
  noInterrupts();
  left = wheelPulses[0];
  right = wheelPulses[1];
  interrupts();
}

void pumpSerialIn();  // Forward declaration; see the V5.1 RX-hardening block near readSerial().

uint8_t serialCrc8(const char *text) {
  uint8_t crc = 0;
  while (*text) {
    crc ^= static_cast<uint8_t>(*text++);
    for (uint8_t i = 0; i < 8; ++i) crc = (crc << 1) ^ (crc & 0x80 ? 0x07 : 0);
  }
  return crc;
}

void printProtectedLine(const char *line) {
  diagStage(21);
  const uint8_t crc = serialCrc8(line);
  Serial.print(line); Serial.print('*');
  if (crc < 16) Serial.print('0');
  Serial.println(crc, HEX);
  pumpSerialIn();
}

void ack(uint16_t id) {
  char line[12];
  snprintf_P(line, sizeof(line), PSTR("AK %u"), id);
  printProtectedLine(line);
}
void done(uint16_t id, const __FlashStringHelper *suffix = nullptr) {
  diagStage(22);
  char line[40];
  if(suffix)snprintf_P(line,sizeof(line),PSTR("DONE %u %S"),id,reinterpret_cast<PGM_P>(suffix));
  else snprintf_P(line,sizeof(line),PSTR("DONE %u"),id);
  printProtectedLine(line);
}
void error(uint16_t id, const __FlashStringHelper *reason) {
  diagStage(23);
  Serial.print(F("ERROR "));
  Serial.print(id);
  Serial.print(' ');
  Serial.println(reason);
  pumpSerialIn();
}

bool deadlineReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

int16_t appliedMotor[2] = { 0, 0 };
uint16_t telemetryId = 0;
uint8_t telemetryStage = 0;
uint32_t motionFeedbackAtMs = 0;

void setMotorSide(uint8_t pwmPin, uint8_t in1, uint8_t in2, int16_t value) {
  const uint8_t side = pwmPin == PIN_L_PWM ? 0 : 1;
  // Apply requested PWM directly; retain the valid 1..255 bounds and STOP.
  if (value != 0 && physicalIo[MOTOR_LEFT + side]) {
    const int16_t magnitude = constrain(abs(value), MOTOR_MIN_DRIVE_PWM, 255);
    value = value > 0 ? magnitude : -magnitude;
  }
  appliedMotor[side] = physicalIo[MOTOR_LEFT + side] ? value : 0;
  if (value == 0 || !physicalIo[MOTOR_LEFT + side]) {
    analogWrite(pwmPin, 0);
    digitalWrite(in1, LOW);
    digitalWrite(in2, LOW);
    return;
  }
  digitalWrite(in1, value > 0 ? HIGH : LOW);
  digitalWrite(in2, value > 0 ? LOW : HIGH);
  analogWrite(pwmPin, abs(value));
}
void applyMotors(int16_t left, int16_t right) {
  setMotorSide(PIN_L_PWM, PIN_L_IN1, PIN_L_IN2, left);
  setMotorSide(PIN_R_PWM, PIN_R_IN1, PIN_R_IN2, right);
}
void stopOutputs() {
  applyMotors(0, 0);
}

void abortMotion(const __FlashStringHelper *reason) {
  if (scan.active()) scan.end(ScanMachine::ABORTED);
  if (motion.kind == MOTION_NONE) return;
  const uint16_t id = motion.id;
  uint32_t leftPulses, rightPulses;
  getPulses(leftPulses, rightPulses);
  leftPulses -= motion.basePulses[0];
  rightPulses -= motion.basePulses[1];
  motion.kind = MOTION_NONE;
  motionBypassAvoidance = false;
  stopOutputs();
  char line[88];
  snprintf_P(line, sizeof(line), PSTR("MA %u %S %lu %lu"), id,
    reinterpret_cast<const char *>(reason), leftPulses, rightPulses);
  printProtectedLine(line);
}

void finishMotion() {
  const uint16_t id = motion.id;
  uint32_t leftPulses, rightPulses;
  getPulses(leftPulses, rightPulses);
  leftPulses -= motion.basePulses[0];
  rightPulses -= motion.basePulses[1];
  motion.kind = MOTION_NONE;
  motionBypassAvoidance = false;
  stopOutputs();
  if (scan.active() && id==scan.id) scan.motionStopped(millis(),leftPulses+motion.basePulses[0],rightPulses+motion.basePulses[1]);
  char line[34];
  snprintf_P(line, sizeof(line), PSTR("MD %u %lu %lu"), id, leftPulses, rightPulses);
  printProtectedLine(line);
}

bool anyTouchPressed() {
  for (uint8_t i = 0; i < 4; ++i)
    if (touches[i].stable) return true;
  return false;
}

void setTouch(uint8_t index, bool pressed, const __FlashStringHelper *source) {
  TouchState &touch = touches[index];
  if (touch.stable == pressed) return;
  const uint32_t now = millis();
  touch.stable = pressed;
  // Stop before serial reporting, including a scan waiting for vision.
  if (pressed && !motionBypassAvoidance) abortMotion(F("TOUCH"));
  Serial.print(F("EVENT "));
  Serial.print(source);
  Serial.print(F(" TOUCH "));
  Serial.print(reinterpret_cast<const __FlashStringHelper *>(TOUCH_NAMES[index]));
  Serial.print(pressed ? F(" PRESS ") : F(" RELEASE "));
  Serial.print(now);
  if (pressed) {
    touch.pressedAt = now;
    Serial.println();
    if (!motionBypassAvoidance) abortMotion(F("TOUCH"));
  } else {
    Serial.print(' ');
    Serial.println(now - touch.pressedAt);
    pumpSerialIn();
  }
}

void tickTouches(uint32_t now) {
  for (uint8_t i = 0; i < 4; ++i) {
    TouchState &touch = touches[i];
    if (!physicalIo[TOUCH_TOP + i] || touch.overrideValue >= 0) continue;
    const bool raw = digitalRead(TOUCH_PINS[i]) == LOW;
    if (raw != touch.physicalRaw) {
      touch.physicalRaw = raw;
      touch.changedAt = now;
    } else if (raw != touch.stable && now - touch.changedAt >= TOUCH_DEBOUNCE_MS) {
      setTouch(i, raw, F("PHYSICAL"));
    }
  }
}

void tickSonar(uint32_t nowMs) {
  const uint32_t nowUs = micros();
  if (sonarPhase == SONAR_IDLE) {
    if (nowMs - lastSonarStartMs < SONAR_PERIOD_MS) return;
    lastSonarStartMs = nowMs;
    activeSonar = 1 - activeSonar;
    if (!physicalIo[SONAR_LEFT_TRIG + activeSonar]) return;
    const uint8_t trig = SONAR_TRIG_PINS[activeSonar];
    digitalWrite(trig, LOW);
    delayMicroseconds(2);
    digitalWrite(trig, HIGH);
    delayMicroseconds(10);
    digitalWrite(trig, LOW);
    if (!physicalIo[SONAR_LEFT_ECHO + activeSonar]) { physicalDistanceValid[activeSonar] = false; return; }
    sonarAtUs = micros();
    sonarPhase = SONAR_WAIT_RISE;
  } else if (sonarPhase == SONAR_WAIT_RISE) {
    if (digitalRead(SONAR_ECHO_PINS[activeSonar]) == HIGH) {
      echoRiseUs = nowUs;
      sonarPhase = SONAR_WAIT_FALL;
    } else if (nowUs - sonarAtUs >= SONAR_TIMEOUT_US) {
      physicalDistanceValid[activeSonar] = false;
      sonarPhase = SONAR_IDLE;
    }
  } else if (digitalRead(SONAR_ECHO_PINS[activeSonar]) == LOW) {
    physicalDistanceCm[activeSonar] = static_cast<float>(nowUs - echoRiseUs) / 58.0f;
    physicalDistanceValid[activeSonar] = physicalDistanceCm[activeSonar] >= 2.0f && physicalDistanceCm[activeSonar] <= 400.0f;
    lastDistanceMs[activeSonar] = nowMs;
    sonarPhase = SONAR_IDLE;
  } else if (nowUs - echoRiseUs >= SONAR_TIMEOUT_US) {
    physicalDistanceValid[activeSonar] = false;
    sonarPhase = SONAR_IDLE;
  }
}

bool currentSideDistance(uint8_t side, float &distance, uint32_t now) {
  if (distanceOverride[side] == DISTANCE_INVALID) return false;
  if (distanceOverride[side] == DISTANCE_VALUE) {
    if (now - injectedDistanceAtMs[side] > SONAR_FRESH_MS) return false;
    distance = injectedDistanceCm[side];
    return true;
  }
  if (!physicalIo[SONAR_LEFT_TRIG + side] || !physicalIo[SONAR_LEFT_ECHO + side]) return false;
  if (!physicalDistanceValid[side] || now - lastDistanceMs[side] > SONAR_FRESH_MS) return false;
  distance = physicalDistanceCm[side];
  return true;
}

bool currentDistance(float &distance, uint32_t now) {
  if (cameraFrontState == CAMERA_INVALID || now - lastCameraFrontMs > CAMERA_FRESH_MS) return false;
  distance = cameraFrontState == CAMERA_CLEAR     ? 400.0f
             : cameraFrontState == CAMERA_BLOCKED ? 0.0f
                                                  : cameraFrontDistanceCm;
  return true;
}

// Camera/front decisions belong to ESP. Uno checks its local side sonars here.
const __FlashStringHelper *motionSafetyReason(int16_t left, int16_t right, uint32_t now, bool report) {
  if (scan.active() && (scan.flags&1) && left && left==-right) return nullptr;
  float distance;
  if (left < right) {
    if (!currentSideDistance(0, distance, now) || distance >= safeDistanceCm[0]) return nullptr;
    if (report) {
      Serial.print(F("EVENT OBSTACLE_NEAR LEFT "));
      if (currentSideDistance(0, distance, now)) Serial.println(distance, 1);
      else Serial.println(F("INVALID"));
      pumpSerialIn();
    }
    return F("SIDE_LEFT_OBSTACLE");
  }
  if (left > right) {
    if (!currentSideDistance(1, distance, now) || distance >= safeDistanceCm[1]) return nullptr;
    if (report) {
      Serial.print(F("EVENT OBSTACLE_NEAR RIGHT "));
      if (currentSideDistance(1, distance, now)) Serial.println(distance, 1);
      else Serial.println(F("INVALID"));
      pumpSerialIn();
    }
    return F("SIDE_RIGHT_OBSTACLE");
  }
  return nullptr;
}

void tickDryRun(uint32_t now) {
  const uint32_t elapsed = now - dryRunAtMs;
  dryRunAtMs = now;
  if (!boardDryRun || motion.kind == MOTION_NONE || elapsed > 250) return;
  // A nominal pulse source for desk use only; it does not claim real motor speed.
  for (uint8_t i = 0; i < 2; ++i) {
    const int16_t command = i == 0 ? motion.left : motion.right;
    if (command == 0 || physicalIo[ENCODER_LEFT + i] || physicalIo[MOTOR_LEFT + i]) continue;
    if (motion.kind == MOTION_PULSES) {
      uint32_t leftCount, rightCount;
      getPulses(leftCount, rightCount);
      if ((i == 0 ? leftCount : rightCount) - motion.basePulses[i] >= motion.targetPulses) continue;
    }
    dryRunFraction[i] += (200.0f * abs(command) / 255.0f) * SLOTS_PER_WHEEL_REV * elapsed / 60000.0f;
    const uint16_t pulses = static_cast<uint16_t>(dryRunFraction[i]);
    if (pulses) {
      noInterrupts();
      wheelPulses[i] += pulses;
      interrupts();
      dryRunFraction[i] -= pulses;
    }
  }
}

void tickMotion(uint32_t now) {
  if (motion.kind == MOTION_NONE) return;
  const __FlashStringHelper *safetyReason = motionBypassAvoidance ? nullptr : motionSafetyReason(motion.left, motion.right, now, true);
  if (safetyReason) {
    abortMotion(safetyReason);
    return;
  }
  uint32_t leftCount, rightCount;
  getPulses(leftCount, rightCount);
  const uint32_t counts[2] = { leftCount, rightCount };
  // Compact movement feedback only, independent of slow sensor-card polling.
  if (now - motionFeedbackAtMs >= 200 && sonarPhase == SONAR_IDLE && Serial.availableForWrite() >= 44) {
    char line[34];
    snprintf_P(line, sizeof(line), PSTR("MP %u %lu %lu"), motion.id,
      leftCount - motion.basePulses[0], rightCount - motion.basePulses[1]);
    printProtectedLine(line);
    motionFeedbackAtMs = now;
  }
  for (uint8_t i = 0; i < 2; ++i) {
    const int16_t command = i == 0 ? motion.left : motion.right;
    if (command == 0) continue;
    if (motion.kind == MOTION_PULSES && counts[i] - motion.basePulses[i] >= motion.targetPulses) continue;
    if (counts[i] != motion.lastPulse[i]) {
      motion.lastPulse[i] = counts[i];
      motion.lastProgressMs[i] = now;
    } else if ((physicalIo[ENCODER_LEFT + i] || !physicalIo[MOTOR_LEFT + i]) && now - motion.lastProgressMs[i] > ENCODER_STALL_MS) {
      abortMotion(F("ENCODER_STALL"));
      return;
    }
  }
  if (motion.kind == MOTION_PULSES) {
    const bool leftDone = leftCount - motion.basePulses[0] >= motion.targetPulses;
    const bool rightDone = rightCount - motion.basePulses[1] >= motion.targetPulses;
    applyMotors(leftDone ? 0 : motion.left, rightDone ? 0 : motion.right);
    if (leftDone && rightDone) {
      finishMotion();
      return;
    }
  }
  if (motion.kind != MOTION_CONTINUOUS && deadlineReached(now, motion.deadlineMs)) {
    if (motion.kind == MOTION_PULSES) abortMotion(F("MOTION_TIMEOUT"));
    else finishMotion();
  }
}

void tickRpm(uint32_t now) {
  const uint32_t elapsed = now - rpmAtMs;
  if (elapsed < 500) return;
  uint32_t left, right;
  getPulses(left, right);
  rpm[0] = (left - rpmPulses[0]) * 60000.0f / (SLOTS_PER_WHEEL_REV * elapsed);
  rpm[1] = (right - rpmPulses[1]) * 60000.0f / (SLOTS_PER_WHEEL_REV * elapsed);
  rpmPulses[0] = left;
  rpmPulses[1] = right;
  rpmAtMs = now;
}

void tickOutputs(uint32_t now) {
  diagStage(70);
  if (ledFlash && deadlineReached(now, ledFlashUntilMs)) {
    ledFlash = false;
    ledOn = false;
  }
  const uint8_t requestedMode = !physicalIo[LED_OUT] ? 0 : ledFlash ? 2 : ledOn ? 1 : 0;
  if (requestedMode != remoteLedMode || (requestedMode == 2 && (remoteLedDeadline != ledFlashUntilMs || remoteLedInterval != ledFlashIntervalMs))) {
    diagStage(71);
    remoteLedMode = requestedMode; remoteLedDeadline = ledFlashUntilMs; remoteLedInterval = ledFlashIntervalMs;
    char eventLine[64];
    if (requestedMode == 2) snprintf_P(eventLine, sizeof(eventLine), PSTR("EVENT HOST_LED FLASH %lu %lu"), (unsigned long)ledFlashIntervalMs, (unsigned long)(ledFlashUntilMs-now));
    else snprintf_P(eventLine, sizeof(eventLine), PSTR("EVENT HOST_LED %s"), requestedMode == 1 ? "ON" : "OFF");
    printProtectedLine(eventLine);
    diagStage(72);
  }
  diagStage(73);
  digitalWrite(PIN_BUZZER, physicalIo[BUZZER_OUT] && buzzerUntilMs != 0 && !deadlineReached(now, buzzerUntilMs));
  diagStage(74);
}

bool parseLong(const char *text, long minValue, long maxValue, long &value) {
  if (!text || !*text) return false;
  char *end;
  const long parsed = strtol(text, &end, 10);
  if (*end || parsed < minValue || parsed > maxValue) return false;
  value = parsed;
  return true;
}
bool parseFloat(const char *text, float minValue, float maxValue, float &value) {
  if (!text || !*text) return false;
  char *end;
  const double parsed = strtod(text, &end);
  if (*end || !isfinite(parsed) || parsed < minValue || parsed > maxValue) return false;
  value = static_cast<float>(parsed);
  return true;
}

const int32_t scanLimits[3][10] PROGMEM = {
  {50,3000,50,3000,500,10000,1000,600000,100,10000},
  {1,2147483647L,-64,64,1,255,1,1000,0,1},
  {1,65535,1,1000,1,2147483647L,0,2,0,2147483647L}
};
__attribute__((noinline)) bool parseScanFields(char **words,long *v,uint8_t profile){
  for(uint8_t i=0;i<5;i++)if(!parseLong(words[i+3],pgm_read_dword(&scanLimits[profile][i*2]),pgm_read_dword(&scanLimits[profile][i*2+1]),v[i]))return false;
  return true;
}

void startMotion(uint16_t id, MotionKind kind, int16_t left, int16_t right,
                 uint32_t durationMs, uint32_t targetPulses, bool estimated,
                 const __FlashStringHelper *name, bool gentleWiggle = false, bool bypassAvoidance = false) {
  if (scan.active() && id!=scan.id) { error(id,F("BUSY"));return; }
  if (sessionStopped) {
    error(id, F("SESSION_STOPPED"));
    return;
  }
  if (motion.kind != MOTION_NONE) {
    error(id, F("BUSY"));
    return;
  }
  if (anyTouchPressed() && !gentleWiggle && !bypassAvoidance) {
    error(id, F("TOUCH_ACTIVE"));
    return;
  }
  const __FlashStringHelper *safetyReason = bypassAvoidance ? nullptr : motionSafetyReason(left, right, millis(), false);
  if (safetyReason) {
    error(id, safetyReason);
    return;
  }
  if (kind == MOTION_PULSES && ((left && physicalIo[MOTOR_LEFT] && !physicalIo[ENCODER_LEFT]) || (right && physicalIo[MOTOR_RIGHT] && !physicalIo[ENCODER_RIGHT]))) {
    error(id, F("PHYSICAL_ENCODER_REQUIRED")); return;
  }
  const uint32_t now = millis();
  motion.kind = kind;
  motionBypassAvoidance = bypassAvoidance;
  motion.id = id;
  motion.left = left;
  motion.right = right;
  motion.startedAtMs = now;
  motion.deadlineMs = now + durationMs;
  motion.targetPulses = targetPulses;
  motion.estimated = estimated;
  getPulses(motion.basePulses[0], motion.basePulses[1]);
  for (uint8_t i = 0; i < 2; ++i) {
    motion.lastPulse[i] = motion.basePulses[i];
    motion.lastProgressMs[i] = now;
  }
  applyMotors(left, right);
  ack(id);
  char line[34];
  snprintf_P(line, sizeof(line), PSTR("MS %u %lu %lu"), id,
    motion.basePulses[0], motion.basePulses[1]);
  printProtectedLine(line);
}

void tickScan(uint32_t now) {
  uint32_t l,r;getPulses(l,r);
  const ScanMachine::Event event=scan.tick(now,l,r);
  if(event==ScanMachine::START_STEP){
    const int16_t p=scan.increment>0?scan.pwm:-scan.pwm;
    const uint16_t count=scan.stepPulses();
    scan.motionStarted(l,r);
    startMotion(scan.id,MOTION_PULSES,p,-p,10000,count,true,F("SCAN_ROTATE"));
    if(motion.kind==MOTION_NONE)scan.end(ScanMachine::INVALID);
  }else if(event==ScanMachine::REQUEST){
    char line[46];snprintf_P(line,sizeof(line),PSTR("VQ %u %u %lu %u"),scan.id,scan.step,(unsigned long)scan.generation,scan.used);printProtectedLine(line);
  }else if(event==ScanMachine::COMPLETE){
    if(motion.kind!=MOTION_NONE)abortMotion(F("SCAN_ENDED"));
    stopOutputs();scan.flags=0;
    char line[46];snprintf_P(line,sizeof(line),PSTR("SD %u %lu %u"),scan.id,(unsigned long)scan.generation,scan.result);printProtectedLine(line);
  }
}

int8_t touchIndex(const char *name) {
  for (uint8_t i = 0; i < 4; ++i)
    if (!strcmp_P(name, TOUCH_NAMES[i])) return i;
  return -1;
}

void clearDebugInputs() {
  distanceOverride[0] = distanceOverride[1] = DISTANCE_AUTO;
  for (uint8_t i = 0; i < 4; ++i) {
    touches[i].overrideValue = -1;
    touches[i].physicalRaw = physicalIo[TOUCH_TOP + i] && digitalRead(TOUCH_PINS[i]) == LOW;
    touches[i].changedAt = millis();
    if (!physicalIo[TOUCH_TOP + i]) setTouch(i, false, F("DRY_RUN_DEFAULT"));
    else setTouch(i, touches[i].physicalRaw, F("PHYSICAL"));
  }
}

void setBoardDryRun(bool enabled) {
  detachInterrupt(digitalPinToInterrupt(PIN_ENC_LEFT)); detachInterrupt(digitalPinToInterrupt(PIN_ENC_RIGHT));
  ledOn = ledFlash = false; buzzerUntilMs = 0;
  boardDryRun = enabled;
  for (uint8_t i = 0; i < IO_COUNT; ++i) physicalIo[i] = !enabled;
  if (!enabled) {
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_LEFT), onLeftPulse, FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_RIGHT), onRightPulse, FALLING);
  }
  noInterrupts(); wheelPulses[0] = wheelPulses[1] = 0; interrupts();
  sonarPhase = SONAR_IDLE;
  physicalDistanceValid[0] = physicalDistanceValid[1] = false;
  cameraFrontState = CAMERA_INVALID;
  lastSonarStartMs = millis();
  digitalWrite(SONAR_TRIG_PINS[0], LOW);
  digitalWrite(SONAR_TRIG_PINS[1], LOW);
  dryRunAtMs = millis();
  dryRunFraction[0] = dryRunFraction[1] = 0;
  getPulses(rpmPulses[0], rpmPulses[1]);
  rpm[0] = rpm[1] = 0;
  rpmAtMs = millis();
  clearDebugInputs();
  stopOutputs();
  tickOutputs(millis());
}

void printIo(uint16_t id) {
  for (uint8_t i = 0; i < IO_COUNT; ++i) {
    Serial.print(F("DATA ")); Serial.print(id); Serial.print(F(" IO "));
    Serial.print(reinterpret_cast<const __FlashStringHelper *>(IO_NAMES[i])); Serial.println(physicalIo[i] ? F(" ON") : F(" OFF")); pumpSerialIn();
  }
}
// 50-byte coherent snapshot. Each encoded frame fits the AVR TX ring.
struct GuardedSnapshot {
  uint16_t before=0xA55A;
  uint8_t data[50];
  uint16_t after=0x5AA5;
  bool valid() const { return before==0xA55A && after==0x5AA5; }
} snapshotBuffer;
#define telemetrySnapshot snapshotBuffer.data
void snapshotPut16(uint8_t o, uint16_t n) { telemetrySnapshot[o]=n; telemetrySnapshot[o+1]=n>>8; }
void snapshotPut32(uint8_t o, uint32_t n) { snapshotPut16(o,n); snapshotPut16(o+2,n>>16); }
int16_t snapshotDistance(float n) { return n < 0 ? -10 : lroundf(n*10); }
void captureSnapshot(uint32_t now) {
  memset(telemetrySnapshot,0,sizeof(telemetrySnapshot));
  telemetrySnapshot[0]=1; snapshotPut32(1,now);
  telemetrySnapshot[5]=sessionStopped | ((motion.kind!=MOTION_NONE)<<1) | (debugEnabled<<2) | (boardDryRun<<3);
  uint16_t mask=0; for(uint8_t i=0;i<IO_COUNT;i++) if(physicalIo[i])mask|=uint16_t(1)<<i;
  snapshotPut16(6,mask);
  uint32_t counts[2]; getPulses(counts[0],counts[1]);
  for(uint8_t i=0;i<2;i++) {
    snapshotPut16(8+i*2,motion.kind==MOTION_NONE?0:i==0?motion.left:motion.right);
    snapshotPut16(12+i*2,appliedMotor[i]);
    telemetrySnapshot[16]|=digitalRead(i==0?PIN_L_IN1:PIN_R_IN1)<<(i*2);
    telemetrySnapshot[16]|=digitalRead(i==0?PIN_L_IN2:PIN_R_IN2)<<(i*2+1);
    if(physicalIo[ENCODER_LEFT+i])telemetrySnapshot[17]|=digitalRead(i==0?PIN_ENC_LEFT:PIN_ENC_RIGHT)<<i;
    snapshotPut32(18+i*4,counts[i]);
    static_assert(sizeof(float)==4,"snapshot requires float32");
    memcpy(telemetrySnapshot+26+i*4,&rpm[i],4);
    const bool valid=physicalIo[SONAR_LEFT_TRIG+i]&&physicalIo[SONAR_LEFT_ECHO+i]&&physicalDistanceValid[i]&&now-lastDistanceMs[i]<=SONAR_FRESH_MS;
    snapshotPut16(34+i*4,snapshotDistance(valid?physicalDistanceCm[i]:-1));
    float d; snapshotPut16(36+i*4,snapshotDistance(currentSideDistance(i,d,now)?d:-1));
    if(physicalIo[SONAR_LEFT_ECHO+i])telemetrySnapshot[42]|=digitalRead(SONAR_ECHO_PINS[i])<<i;
    telemetrySnapshot[43]|=(distanceOverride[i]!=DISTANCE_AUTO)<<i;
  }
  for(uint8_t i=0;i<4;i++) {
    if(physicalIo[TOUCH_TOP+i])telemetrySnapshot[44]|=digitalRead(TOUCH_PINS[i])<<i;
    telemetrySnapshot[45]|=touches[i].stable<<i;
    telemetrySnapshot[46]|=(touches[i].overrideValue>=0)<<i;
  }
  const bool led=ledFlash?((now-ledFlashStartedMs)/ledFlashIntervalMs)%2==0:ledOn;
  telemetrySnapshot[47]=led | ((buzzerUntilMs&&!deadlineReached(now,buzzerUntilMs))<<1) | (digitalRead(PIN_BUZZER)<<2);
  float d; snapshotPut16(48,snapshotDistance(currentDistance(d,now)?d:-1));
}
void tickHardwareTelemetry(uint32_t now) {
  if(!telemetryId||sonarPhase!=SONAR_IDLE||Serial.availableForWrite()<60)return;
  diagCommand(telemetryId); diagStage(30+telemetryStage);
  if(telemetryStage==3){char terminal[24];snprintf_P(terminal,sizeof(terminal),PSTR("DONE %u TELEMETRY"),telemetryId);telemetryId=0;printProtectedLine(terminal);return;}
  if(!telemetryStage)captureSnapshot(now);
  char line[46];
  uint8_t at=snprintf_P(line,sizeof(line),PSTR("TB %u %u "),telemetryId,telemetryStage);
  const char digits[]="0123456789ABCDEF";
  uint8_t length=telemetryStage==2?16:17;
  for(uint8_t i=0;i<length;i++){uint8_t n=telemetrySnapshot[telemetryStage*17+i];line[at++]=digits[n>>4];line[at++]=digits[n&15];}
  line[at]=0; printProtectedLine(line); ++telemetryStage;
}

void diagBadChecksum(const char *line, uint8_t calculated, uint8_t received) {
  if (!diagMode) return;
  char detail[64];
  uint8_t length = snprintf_P(detail, sizeof(detail), PSTR("EVENT DIAG RX %u %02X %02X "),
                              strlen(line), calculated, received);
  static const char digits[] PROGMEM = "0123456789ABCDEF";
  // At most 12 raw bytes; inspect before strtok modifies the command.
  for (uint8_t i = 0; line[i] && i < 12; ++i) {
    const uint8_t b = static_cast<uint8_t>(line[i]);
    detail[length++] = pgm_read_byte(&digits[b >> 4]); detail[length++] = pgm_read_byte(&digits[b & 15]);
  }
  detail[length] = '\0';
  diagWrite(detail); // May be skipped when TX is busy; ERROR is still emitted.
}

void processCommand(char *line) {
  // Web commands carry CRC-8. Unprotected manual commands remain available.
  char *star = strchr(line, '*');
  if (!strncmp_P(line, PSTR("C "), 2) && !star) { diagBadChecksum(line, serialCrc8(line), 0); error(0, F("BAD_CHECKSUM")); return; }
  if (star) {
    if (strlen(star + 1) != 2 || !isxdigit(star[1]) || !isxdigit(star[2])) {
      diagBadChecksum(line, 0, 0);
      error(0, F("BAD_CHECKSUM")); return;
    }
    const uint8_t received = static_cast<uint8_t>(strtoul(star + 1, nullptr, 16));
    *star = '\0';
    const uint8_t calculated = serialCrc8(line);
    if (calculated != received) { *star = '*'; diagBadChecksum(line, calculated, received); error(0, F("BAD_CHECKSUM")); return; }
  }
  char *words[8];
  uint8_t count = 0;
  char *save;
  for (char *token = strtok_r(line, " \t", &save); token; token = strtok_r(nullptr, " \t", &save)) {
    if (count == 8) {
      error(0, F("TOO_MANY_ARGUMENTS"));
      return;
    }
    words[count++] = token;
  }
  long rawId;
  if (count < 3 || (strcmp_P(words[0], PSTR("CMD")) && strcmp_P(words[0], PSTR("C"))) || !parseLong(words[1], 1, 65535, rawId)) {
    error(0, F("BAD_ENVELOPE"));
    return;
  }
  const uint16_t id = static_cast<uint16_t>(rawId);
  diagCommand(id);
  diagTrace(20);
  const char *verb = words[2];
  const uint8_t n = count - 3;
  const char *a = n > 0 ? words[3] : nullptr;
  const char *b = n > 1 ? words[4] : nullptr;
  const char *c = n > 2 ? words[5] : nullptr;
  if (!strcmp_P(verb, PSTR("CF"))) verb = "CAMERA_FRONT";
  else if (!strcmp_P(verb, PSTR("GT"))) verb = "GET_TELEMETRY";
  else if (!strcmp_P(verb, PSTR("DI"))) {
    verb = "DEBUG_INJECT";
    if (a && !strcmp_P(a, PSTR("S"))) a = "SONAR";
    if (b && !strcmp_P(b, PSTR("L"))) b = "LEFT";
    else if (b && !strcmp_P(b, PSTR("R"))) b = "RIGHT";
  }
  long x, y;
  float f, g;

  if((!strcmp_P(verb,PSTR("SC"))||!strcmp_P(verb,PSTR("SCAN_CONFIG")))&&n==5){
    long v[5];if(!star||!parseScanFields(words,v,0)){error(id,F("INVALID_SCAN_FRAME"));return;}
    if(scan.active()||motion.kind!=MOTION_NONE){error(id,F("BUSY"));return;}
    if(v[4]<=v[0]||v[4]<=v[1]){error(id,F("INVALID_ARGUMENT"));return;}
    scan.cfg.minimum=v[0];scan.cfg.quiet=v[1];scan.cfg.vision=v[2];scan.cfg.total=v[3];scan.cfg.settle=v[4];ack(id);done(id,F("SCAN_CONFIGURED"));return;
  }
  if((!strcmp_P(verb,PSTR("SR"))||!strcmp_P(verb,PSTR("SCAN_ROTATE")))&&n==5){
    long v[5];if(!star||!parseScanFields(words,v,1)){error(id,F("INVALID_SCAN_FRAME"));return;}
    if(sessionStopped||anyTouchPressed()){error(id,F("SESSION_OR_TOUCH_LOCK"));return;}
    if(motion.kind!=MOTION_NONE&&!(scan.id==id&&scan.generation==uint32_t(v[0]))){error(id,F("BUSY"));return;}
    if(v[4]){float dl,dr;if(!currentSideDistance(0,dl,millis())||!currentSideDistance(1,dr,millis())||dl>=safeDistanceCm[0]||dr>=safeDistanceCm[1]){error(id,F("BOTH_SIDES_REQUIRED"));return;}}
    if(!scan.begin(id,v[0],v[1],v[2],v[3],v[4],millis())){error(id,F("INVALID_SCAN"));return;}
    ack(id);if(scan.phase==ScanMachine::TERMINAL)scan.terminalSent=false;return;
  }
  if((!strcmp_P(verb,PSTR("VR"))||!strcmp_P(verb,PSTR("VISION_RESULT")))&&n==5){
    long v[5];if(!star||!parseScanFields(words,v,2)){error(id,F("INVALID_SCAN_FRAME"));return;}
    if(!scan.reply(v[0],v[1],v[2],v[3],v[4],millis())){error(id,F("STALE_VISION"));return;}
    ack(id);done(id,F("VISION_ACCEPTED"));return;
  }

  if (!strcmp_P(verb, PSTR("DEBUG_DIAG")) && n == 1 && parseLong(a, 0, 2, x)) {
    diagMode = static_cast<uint8_t>(x);
    ack(id); done(id, F("DIAG_SET")); return;
  }

  if (!strcmp_P(verb, PSTR("PING")) && n == 0) {
    ack(id);
    done(id, F("PONG"));
    Serial.println(F("EVENT CAPABILITIES SCAN_V1"));
    return;
  }
  if ((!strcmp_P(verb, PSTR("GET_TELEMETRY")) || !strcmp_P(verb, PSTR("GET_DISTANCE")) || !strcmp_P(verb, PSTR("GET_ENCODER")) || !strcmp_P(verb, PSTR("GET_STATUS"))) && n == 0) {
    if (telemetryId) { error(id, F("TELEMETRY_BUSY")); return; }
    telemetryId = id; telemetryStage = 0; ack(id); return;
  }
  if (!strcmp_P(verb, PSTR("GET_IO")) && n == 0) { ack(id); printIo(id); done(id); return; }
  if (!strcmp_P(verb, PSTR("GET_CONFIG")) && n == 0) {
    ack(id);
    Serial.print(F("DATA "));
    Serial.print(id);
    Serial.print(F(" CONFIG WHEEL_MM="));
    Serial.print(wheelDiameterMm, 1);
    Serial.print(F(" TRACK_MM="));
    Serial.print(trackWidthMm, 1);
    Serial.print(F(" SLOTS_PER_REV="));
    Serial.print(SLOTS_PER_WHEEL_REV);
    Serial.print(F(" SAFE_LEFT_CM="));
    Serial.print(safeDistanceCm[0], 1);
    Serial.print(F(" SAFE_RIGHT_CM="));
    Serial.println(safeDistanceCm[1], 1);
    done(id);
    return;
  }
  if (!strcmp_P(verb, PSTR("CAMERA_FRONT")) && n == 1) {
    if (!strcmp_P(a, PSTR("CLEAR"))) cameraFrontState = CAMERA_CLEAR;
    else if (!strcmp_P(a, PSTR("BLOCKED"))) cameraFrontState = CAMERA_BLOCKED;
    else if (!strcmp_P(a, PSTR("INVALID"))) cameraFrontState = CAMERA_INVALID;
    else if (parseFloat(a, 0, 400, f)) {
      cameraFrontDistanceCm = f;
      cameraFrontState = CAMERA_DISTANCE;
    } else {
      error(id, F("INVALID_ARGUMENT"));
      return;
    }
    lastCameraFrontMs = millis();
    ack(id);
    done(id, F("CAMERA_FRONT_SET"));
    return;
  }
  if (!strcmp_P(verb, PSTR("SET_GEOMETRY")) && n == 2 && parseFloat(a, 20, 150, f) && parseFloat(b, 50, 300, g)) {
    if (motion.kind != MOTION_NONE) {
      error(id, F("BUSY"));
      return;
    }
    wheelDiameterMm = f;
    trackWidthMm = g;
    ack(id);
    done(id, F("GEOMETRY_SET"));
    return;
  }
  if (!strcmp_P(verb, PSTR("SET_SAFE_DISTANCE")) && n == 2 && (!strcmp_P(a, PSTR("LEFT")) || !strcmp_P(a, PSTR("RIGHT")))) {
    if (!parseFloat(b, MIN_SAFE_CM, MAX_SAFE_CM, f)) {
      error(id, F("INVALID_ARGUMENT"));
      return;
    }
    if (motion.kind != MOTION_NONE) {
      error(id, F("BUSY"));
      return;
    }
    safeDistanceCm[!strcmp_P(a, PSTR("LEFT")) ? 0 : 1] = f;
    ack(id);
    done(id, F("SAFE_DISTANCE_SET"));
    return;
  }
  if (!strcmp_P(verb, PSTR("SESSION_START")) && n == 0) {
    sessionStopped = false;
    ack(id);
    done(id, F("SESSION_STARTED"));
    return;
  }
  if (!strcmp_P(verb, PSTR("SESSION_STOP")) && n == 0) {
    ack(id);
    sessionStopped = true;
    abortMotion(F("SESSION_STOP"));
    ledFlash = false;
    ledOn = false;
    buzzerUntilMs = 0;
    tickOutputs(millis());
    clearDebugInputs();
    cameraFrontState = CAMERA_INVALID;
    done(id, F("SESSION_STOPPED"));
    return;
  }
  if (!strcmp_P(verb, PSTR("STOP")) && n == 0) {
    ack(id);
    abortMotion(F("STOP_COMMAND"));
    done(id, F("MOTOR_STOPPED"));
    return;
  }
  if (!strcmp_P(verb, PSTR("MOVE")) && n == 3 && (!strcmp_P(a, PSTR("FORWARD")) || !strcmp_P(a, PSTR("BACKWARD"))) && parseLong(b, 1, 255, x) && parseLong(c, 1, 10000, y)) {
    const int16_t speed = !strcmp_P(a, PSTR("FORWARD")) ? x : -x;
    startMotion(id, MOTION_TIMED, speed, speed, y, 0, false,
                speed > 0 ? F("MOVE_FORWARD") : F("MOVE_BACKWARD"));
    return;
  }
  if (!strcmp_P(verb, PSTR("TURN")) && n == 3 && (!strcmp_P(a, PSTR("LEFT")) || !strcmp_P(a, PSTR("RIGHT"))) && parseLong(b, 1, 255, x) && parseLong(c, 1, 10000, y)) {
    const int16_t speed = !strcmp_P(a, PSTR("RIGHT")) ? x : -x;
    startMotion(id, MOTION_TIMED, speed, -speed, y, 0, false,
                speed > 0 ? F("TURN_RIGHT") : F("TURN_LEFT"),
                x <= 90 && y <= 400);
    return;
  }
  if (!strcmp_P(verb, PSTR("MOTOR")) && n == 2 && parseLong(a, -255, 255, x) && parseLong(b, -255, 255, y)) {
    if (x == 0 && y == 0) {
      ack(id);
      abortMotion(F("MOTOR_ZERO"));
      done(id, F("MOTOR_STOPPED"));
      return;
    }
    if (motion.kind == MOTION_CONTINUOUS) abortMotion(F("REPLACED"));
    startMotion(id, MOTION_CONTINUOUS, x, y, 0, 0, false, F("MOTOR"));
    return;
  }
  if (!strcmp_P(verb, PSTR("DRIVE")) && n == 2 && parseFloat(a, -100, 100, f) && f != 0 && parseLong(b, 1, 255, x)) {
    const float circumference = PI * wheelDiameterMm;
    const uint32_t pulses = max(1L, lroundf(fabs(f) * 10.0f / circumference * SLOTS_PER_WHEEL_REV));
    const int16_t speed = f > 0 ? x : -x;
    startMotion(id, MOTION_PULSES, speed, speed, 10000, pulses, true, F("DRIVE"));
    return;
  }
  if (!strcmp_P(verb, PSTR("ROTATE")) && n == 2 && parseFloat(a, -360, 360, f) && f != 0 && parseLong(b, 1, 255, x)) {
    if (trackWidthMm == 0) {
      error(id, F("TRACK_NOT_SET"));
      return;
    }
    const float wheelTravelMm = PI * trackWidthMm * fabs(f) / 360.0f;
    const uint32_t pulses = max(1L, lroundf(wheelTravelMm / (PI * wheelDiameterMm) * SLOTS_PER_WHEEL_REV));
    const int16_t speed = f > 0 ? x : -x;
    startMotion(id, MOTION_PULSES, speed, -speed, 10000, pulses, true, F("ROTATE"));
    return;
  }
  if (!strcmp_P(verb, PSTR("LED")) && n == 1 && (!strcmp_P(a, PSTR("ON")) || !strcmp_P(a, PSTR("OFF")))) {
    if (sessionStopped && strcmp_P(a, PSTR("OFF"))) {
      error(id, F("SESSION_STOPPED"));
      return;
    }
    ledFlash = false;
    ledOn = !strcmp_P(a, PSTR("ON"));
    tickOutputs(millis());
    ack(id);
    done(id, ledOn ? F("LED_ON") : F("LED_OFF"));
    return;
  }
  if (!strcmp_P(verb, PSTR("LED")) && n == 3 && !strcmp_P(a, PSTR("FLASH")) && parseLong(b, 20, 5000, x) && parseLong(c, 20, 10000, y)) {
    if (sessionStopped) {
      error(id, F("SESSION_STOPPED"));
      return;
    }
    ledFlash = true;
    ledFlashIntervalMs = x;
    ledFlashStartedMs = millis();
    ledFlashUntilMs = ledFlashStartedMs + y;
    ack(id);
    done(id, F("LED_FLASH_STARTED"));
    return;
  }
  if (!strcmp_P(verb, PSTR("BEEP")) && n == 1 && parseLong(a, 0, 5000, x)) {
    if (sessionStopped && x) {
      error(id, F("SESSION_STOPPED"));
      return;
    }
    buzzerUntilMs = millis() + x;
    ack(id);
    done(id, F("BEEP_STARTED"));
    return;
  }

#if UNO_ENABLE_DEBUG_COMMANDS
  if (!strcmp_P(verb, PSTR("TEST_MOTOR")) && n == 4) {
    long duration;
    if (!parseLong(a, -255, 255, x) || !parseLong(b, -255, 255, y) || (x == 0 && y == 0)
        || !parseLong(c, 50, 2000, duration)
        || (strcmp_P(words[6], PSTR("SAFE")) && strcmp_P(words[6], PSTR("BYPASS")))) {
      error(id, F("INVALID_ARGUMENT")); return;
    }
    startMotion(id, MOTION_TIMED, x, y, duration, 0, false, F("TEST_MOTOR"), false,
                !strcmp_P(words[6], PSTR("BYPASS")));
    return;
  }
  if ((!strcmp_P(verb, PSTR("DEBUG_IO")) && n == 2) || (!strcmp_P(verb, PSTR("DEBUG_OUTPUTS")) && n == 1 && !strcmp_P(a, PSTR("OFF")))) {
    if (!debugEnabled) { error(id, F("DEBUG_DISABLED")); return; }
    int8_t channel = -1;
    if (!strcmp_P(verb, PSTR("DEBUG_IO"))) {
      for (uint8_t i = 0; i < IO_COUNT; ++i) if (!strcmp_P(a, IO_NAMES[i])) channel = i;
      if (channel < 0 || (strcmp_P(b, PSTR("ON")) && strcmp_P(b, PSTR("OFF")))) { error(id, F("INVALID_ARGUMENT")); return; }
    }
    abortMotion(F("IO_CHANGE")); ledOn = ledFlash = false; buzzerUntilMs = 0;
    if (channel < 0) { for (uint8_t i = 0; i <= SONAR_RIGHT_TRIG; ++i) physicalIo[i] = false; }
    else {
      const bool changed = physicalIo[channel] != !strcmp_P(b, PSTR("ON"));
      physicalIo[channel] = !strcmp_P(b, PSTR("ON"));
      if (changed && (channel == ENCODER_LEFT || channel == ENCODER_RIGHT || channel == MOTOR_LEFT || channel == MOTOR_RIGHT)) {
        const uint8_t side = channel == ENCODER_RIGHT || channel == MOTOR_RIGHT ? 1 : 0;
        noInterrupts(); wheelPulses[side] = 0; lastEdgeUs[side] = 0; interrupts();
        rpmPulses[side] = 0; rpm[side] = 0; dryRunFraction[side] = 0;
      }
    }
    detachInterrupt(digitalPinToInterrupt(PIN_ENC_LEFT)); detachInterrupt(digitalPinToInterrupt(PIN_ENC_RIGHT));
    if (physicalIo[ENCODER_LEFT]) attachInterrupt(digitalPinToInterrupt(PIN_ENC_LEFT), onLeftPulse, FALLING);
    if (physicalIo[ENCODER_RIGHT]) attachInterrupt(digitalPinToInterrupt(PIN_ENC_RIGHT), onRightPulse, FALLING);
    sonarPhase = SONAR_IDLE; physicalDistanceValid[0] = physicalDistanceValid[1] = false;
    digitalWrite(SONAR_TRIG_PINS[0], LOW); digitalWrite(SONAR_TRIG_PINS[1], LOW);
    clearDebugInputs(); stopOutputs(); tickOutputs(millis());
    ack(id); printIo(id); done(id, F("IO_SET")); return;
  }
  if (!strcmp_P(verb, PSTR("DEBUG_MODE")) && n == 1 && (!strcmp_P(a, PSTR("ON")) || !strcmp_P(a, PSTR("OFF")))) {
    abortMotion(F("DEBUG_MODE_CHANGE"));
    debugEnabled = !strcmp_P(a, PSTR("ON"));
    if (!debugEnabled) {
      boardDryRun = false;
      clearDebugInputs();
    }
    ack(id);
    done(id, debugEnabled ? F("DEBUG_ON") : F("DEBUG_OFF"));
    return;
  }
  if (!strcmp_P(verb, PSTR("DEBUG_MOTOR_MODE")) && n == 1 && (!strcmp_P(a, PSTR("DRY_RUN")) || !strcmp_P(a, PSTR("LIVE")))) {
    if (!debugEnabled) {
      error(id, F("DEBUG_DISABLED"));
      return;
    }
    abortMotion(F("DEBUG_MODE_CHANGE"));
    setBoardDryRun(!strcmp_P(a, PSTR("DRY_RUN")));
    ack(id);
    done(id, boardDryRun ? F("DRY_RUN") : F("LIVE"));
    return;
  }
  if (!strcmp_P(verb, PSTR("DEBUG_INJECT"))) {
    if (!debugEnabled) {
      error(id, F("DEBUG_DISABLED"));
      return;
    }
    if (n == 3 && !strcmp_P(a, PSTR("TOUCH")) && touchIndex(b) >= 0 && (!strcmp_P(c, PSTR("PRESS")) || !strcmp_P(c, PSTR("RELEASE")) || !strcmp_P(c, PSTR("AUTO")))) {
      if (physicalIo[TOUCH_TOP + touchIndex(b)] && strcmp_P(c, PSTR("AUTO"))) {
        error(id, F("DRY_RUN_REQUIRED"));
        return;
      }
      const uint8_t index = touchIndex(b);
      ack(id);
      if (!strcmp_P(c, PSTR("AUTO"))) {
        touches[index].overrideValue = -1;
        touches[index].physicalRaw = physicalIo[TOUCH_TOP + index] && digitalRead(TOUCH_PINS[index]) == LOW;
        touches[index].changedAt = millis();
        if (!physicalIo[TOUCH_TOP + index]) setTouch(index, false, F("DRY_RUN_DEFAULT"));
        else setTouch(index, touches[index].physicalRaw, F("PHYSICAL"));
      } else {
        const bool pressed = !strcmp_P(c, PSTR("PRESS"));
        touches[index].overrideValue = pressed ? 1 : 0;
        setTouch(index, pressed, F("DEBUG_INJECT"));
      }
      done(id);
      return;
    }
    if (n == 3 && !strcmp_P(a, PSTR("SONAR")) && (!strcmp_P(b, PSTR("LEFT")) || !strcmp_P(b, PSTR("RIGHT")))) {
      if ((!boardDryRun || (physicalIo[SONAR_LEFT_TRIG + (!strcmp_P(b, PSTR("LEFT")) ? 0 : 1)] && physicalIo[SONAR_LEFT_ECHO + (!strcmp_P(b, PSTR("LEFT")) ? 0 : 1)])) && strcmp_P(c, PSTR("AUTO"))) {
        error(id, F("DRY_RUN_REQUIRED"));
        return;
      }
      const uint8_t side = !strcmp_P(b, PSTR("LEFT")) ? 0 : 1;
      if (!strcmp_P(c, PSTR("AUTO"))) distanceOverride[side] = DISTANCE_AUTO;
      else if (!strcmp_P(c, PSTR("INVALID"))) distanceOverride[side] = DISTANCE_INVALID;
      else if (parseFloat(c, 0, 400, f)) {
        distanceOverride[side] = DISTANCE_VALUE;
        injectedDistanceCm[side] = f;
        injectedDistanceAtMs[side] = millis();
      } else {
        error(id, F("INVALID_ARGUMENT"));
        return;
      }
      ack(id);
      done(id);
      return;
    }
    if (n == 3 && !strcmp_P(a, PSTR("ENCODER")) && parseLong(b, 0, 10000, x) && parseLong(c, 0, 10000, y)) {
      if (!boardDryRun || physicalIo[ENCODER_LEFT] || physicalIo[ENCODER_RIGHT] || physicalIo[MOTOR_LEFT] || physicalIo[MOTOR_RIGHT]) {
        error(id, F("DRY_RUN_REQUIRED"));
        return;
      }
      noInterrupts();
      wheelPulses[0] += x;
      wheelPulses[1] += y;
      interrupts();
      ack(id);
      Serial.print(F("EVENT DEBUG_INJECT ENCODER +"));
      Serial.print(x);
      Serial.print(F(" +"));
      Serial.println(y);
      done(id);
      return;
    }
    error(id, F("INVALID_ARGUMENT"));
    return;
  }
#else
  if (!strncmp_P(verb, PSTR("DEBUG_"), 6)) {
    error(id, F("DEBUG_UNAVAILABLE"));
    return;
  }
#endif
  error(id, F("UNKNOWN_OR_INVALID_COMMAND"));
}

// ===== V5.1 serial RX hardening =====
// Hosts that write several command lines back-to-back can overrun the 64-byte hardware
// RX ring while the sketch blocks inside Serial.print waiting for TX headroom (the RX
// interrupt only buffers 64 bytes). The emit primitives (ack/done/error/events) call
// pumpSerialIn() so bytes keep being assembled during those waits. A complete line that
// arrives while another command is being dispatched is held in pumpedLine (depth 1; a
// second concurrent line is dropped and the host retries on its ACK timeout) and run by
// loop() right after the current command returns.
#define pumpedLine pendingBuffer.data
bool pumpedReady = false;
bool dispatchingCommand = false;

int8_t assembleInputChar(const char ch, char *buf, uint8_t &len, bool &overflow) {
  if (ch == '\r') return 0;
  if (ch == '\n') {
    const int8_t result = overflow ? -1 : (len ? 1 : 0);
    if (result == 1) buf[len] = '\0';
    len = 0;
    overflow = false;
    return result;
  }
  if (!overflow) {
    if (len < 127) buf[len++] = ch;
    else overflow = true;
  }
  return 0;
}

void dispatchCommandLine(char *line) {
  dispatchingCommand = true;
  // ACK/DONE may pump a new line. Its tokens must never alias active arguments.
  strncpy(executingBuffer.data, line, sizeof(executingBuffer.data) - 1);
  executingBuffer.data[sizeof(executingBuffer.data) - 1] = '\0';
  diagStage(20);
  processCommand(executingBuffer.data);
  diagTrace(24);
  dispatchingCommand = false;
}

void processInputByte(const char ch, const uint8_t source) {
  const int8_t result = assembleInputChar(ch, inputBuffers[source].data, inputLength[source], inputOverflow[source]);
  if (result == -1) {
    ++diagRxOverflow;
    if (!dispatchingCommand) error(0, F("LINE_TOO_LONG"));
    return;
  }
  if (result != 1) return;
  if (!dispatchingCommand) {
    dispatchCommandLine(inputBuffers[source].data);
    return;
  }
  if (pumpedReady) { ++diagDropped; return; }
  strncpy(pumpedLine, inputBuffers[source].data, sizeof(pumpedLine) - 1);
  pumpedLine[sizeof(pumpedLine) - 1] = '\0';
  pumpedReady = true;
}

void pumpSerialIn() {
  // Re-entrancy guard: bytes completed during an error()/ack() pump stay in the hardware
  // ring until the outer pump finishes, bounding stack depth on pathological line floods.
  static bool pumping = false;
  if (pumping) return;
  pumping = true;
  while (hostRx.available() > 0) {
    if (hostRx.overflow()) { ++diagRxOverflow; inputLength[0] = 0; inputOverflow[0] = true; }
    processInputByte((char)hostRx.read(), 0);
  }
  while (Serial.available() > 0) processInputByte((char)Serial.read(), 1);
  if (hostRx.overflow()) {
    ++diagRxOverflow;
    // Never execute the tail of a truncated software-UART command.
    inputLength[0] = 0; inputOverflow[0] = true;
  }
  pumping = false;
}

void readSerial() {
  pumpSerialIn();
  while (pumpedReady) {
    pumpedReady = false;
    dispatchCommandLine(pumpedLine);
    pumpSerialIn();
  }
}

extern char __heap_start;
extern void *__brkval;
uint16_t diagMinFree = UINT16_MAX;
uint8_t diagMemoryFlags = 0;
bool diagMemoryLatched = false;
int16_t diagBadLeft = 0, diagBadRight = 0;
uint32_t diagMemoryAt = 500;

void checkMemoryState() {
  char stack;
  const int freeBytes = &stack - (__brkval ? static_cast<char *>(__brkval) : &__heap_start);
  diagMinFree = min(diagMinFree, static_cast<uint16_t>(max(0, freeBytes)));
  uint8_t flags = 0;
  if (!inputBuffers[0].valid() || !inputBuffers[1].valid() ||
      !pendingBuffer.valid() || !executingBuffer.valid() || !snapshotBuffer.valid()) flags |= 1;
  if (appliedMotor[0] < -255 || appliedMotor[0] > 255 ||
      appliedMotor[1] < -255 || appliedMotor[1] > 255) flags |= 2;
  if (freeBytes < 128) flags |= 4;
  diagMemoryFlags |= flags;
  if ((diagMemoryFlags & 3) && !diagMemoryLatched) {
    diagBadLeft = appliedMotor[0]; diagBadRight = appliedMotor[1];
    diagMemoryLatched = true;
    motion.kind = MOTION_NONE;
    stopOutputs();
  }
}

void tickMemoryDiagnostics(uint32_t now) {
  // This is sampled stack/heap headroom, not a historical stack high-water mark.
  if ((!diagMode && !diagMemoryLatched) || static_cast<int32_t>(now - diagMemoryAt) < 1000) return;
  char stack, line[64];
  const int freeBytes = &stack - (__brkval ? static_cast<char *>(__brkval) : &__heap_start);
  diagMinFree = min(diagMinFree, static_cast<uint16_t>(max(0, freeBytes)));
  snprintf_P(line, sizeof(line), PSTR("EVENT DIAG M %d %u %u %d %d %u"), freeBytes,
             diagMinFree, diagMemoryFlags, diagMemoryLatched ? diagBadLeft : appliedMotor[0],
             diagMemoryLatched ? diagBadRight : appliedMotor[1], diagMemoryLatched);
  const size_t length = strlen(line);
  if (length + 5 > static_cast<size_t>(Serial.availableForWrite())) return;
  // Protected diagnostic snapshots distinguish invalid stored state from a damaged frame.
  const uint8_t crc = serialCrc8(line);
  Serial.print(line); Serial.print('*');
  if (crc < 16) Serial.print('0');
  Serial.println(crc, HEX);
  diagMemoryAt = now;
}

void tickDiagnostics(uint32_t now) {
  if (!diagMode || now - diagLastAt < 1000) return;
  const uint8_t wait = (sonarPhase != SONAR_IDLE ? 1 : 0) |
                       (Serial.availableForWrite() < 60 ? 2 : 0);
  char line[64];
  // Hex keeps worst-case heartbeat short enough for the 63-byte TX ring.
  snprintf_P(line, sizeof(line), PSTR("EVENT DIAG H %lx %x %x %x %x %x %x %x"),
             static_cast<unsigned long>(now), diagLoops, diagMaxLoopMs,
             telemetryId, telemetryStage, wait, diagRxOverflow, diagDropped);
  if (strlen(line) + 2 > static_cast<size_t>(Serial.availableForWrite())) return;
  diagWrite(line);
  diagLastAt = now; diagLoops = 0; diagMaxLoopMs = 0;
}

void setup() {
  pinMode(PIN_L_IN1, OUTPUT);
  pinMode(PIN_L_IN2, OUTPUT);
  pinMode(PIN_R_IN1, OUTPUT);
  pinMode(PIN_R_IN2, OUTPUT);
  pinMode(PIN_L_PWM, OUTPUT);
  pinMode(PIN_R_PWM, OUTPUT);
  stopOutputs();
  for (uint8_t side = 0; side < 2; ++side) {
    pinMode(SONAR_TRIG_PINS[side], OUTPUT);
    digitalWrite(SONAR_TRIG_PINS[side], LOW);
    pinMode(SONAR_ECHO_PINS[side], INPUT);
  }
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  for (uint8_t i = 0; i < 4; ++i) {
    pinMode(TOUCH_PINS[i], INPUT_PULLUP);
    touches[i] = { digitalRead(TOUCH_PINS[i]) == LOW, false, millis(), 0, -1 };
  }
  pinMode(PIN_ENC_LEFT, INPUT_PULLUP);
  pinMode(PIN_ENC_RIGHT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_LEFT), onLeftPulse, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_RIGHT), onRightPulse, FALLING);
  hostRx.begin(57600);
  Serial.begin(57600);
  rpmAtMs = millis();
  dryRunAtMs = millis();
  Serial.println(F("READY UNO_CAT_V6.6"));
  Serial.println(F("EVENT CAPABILITIES SCAN_V1"));
  if (diagBreadcrumb.magic == 0xD159 &&
      static_cast<uint8_t>(~diagBreadcrumb.stage) == diagBreadcrumb.stageInverse &&
      static_cast<uint16_t>(~diagBreadcrumb.command) == diagBreadcrumb.commandInverse) {
    Serial.print(F("EVENT DIAG_BOOT PREV_STAGE=")); Serial.print(diagBreadcrumb.stage);
    Serial.print(F(" CMD=")); Serial.println(diagBreadcrumb.command);
  } else Serial.println(F("EVENT DIAG_BOOT NO_VALID_BREADCRUMB"));
  diagBreadcrumb.magic = 0xD159; diagCommand(0); diagStage(0);
  Serial.println(F("EVENT HOST_LED OFF"));
  Serial.println(F("EVENT UART_SOFT_RX D12 BAUD=57600 TX=D1"));
  // V5.1: keep assembling host commands while Serial.print waits for TX headroom, so
  // command bursts (sensor injections + ROTATE/DRIVE) can no longer overrun the RX ring.
}

void loop() {
  const uint32_t loopStart = micros();
  checkMemoryState();
  if (diagMemoryLatched) {
    stopOutputs(); // Keep motor outputs disabled until reset; do not parse corrupt buffers.
    tickMemoryDiagnostics(millis());
    return;
  }
  diagStage(1);
  readSerial();
  const uint32_t now = millis();
  diagStage(2);
  tickTouches(now);
  diagStage(3);
  tickSonar(now);
  diagStage(4);
  tickDryRun(now);
  diagStage(5);
  tickMotion(now);
  tickScan(now);
  diagStage(6);
  tickRpm(now);
  diagStage(7);
  tickOutputs(now);
  diagStage(8);
  checkMemoryState();
  if (diagMemoryLatched) { tickMemoryDiagnostics(now); return; }
  tickHardwareTelemetry(now);
  if (diagLoops != UINT16_MAX) ++diagLoops;
  const uint32_t elapsedMs = (micros() - loopStart) / 1000;
  if (elapsedMs > diagMaxLoopMs) diagMaxLoopMs = min(elapsedMs, static_cast<uint32_t>(UINT16_MAX));
  diagStage(9);
  tickMemoryDiagnostics(millis());
  tickDiagnostics(millis());
}
