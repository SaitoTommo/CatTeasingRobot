#include <Arduino.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Arduino Uno R3. D0/D1 are reserved for the 57600-baud command channel.
constexpr uint8_t PIN_ENC_LEFT = 2;
constexpr uint8_t PIN_ENC_RIGHT = 3;
constexpr uint8_t PIN_L_IN1 = 4;
constexpr uint8_t PIN_L_PWM = 5;
constexpr uint8_t PIN_R_PWM = 6;
constexpr uint8_t PIN_L_IN2 = 7;
constexpr uint8_t PIN_R_IN1 = 8;
constexpr uint8_t PIN_R_IN2 = 9;
constexpr uint8_t SONAR_TRIG_PINS[2] = { 10, A4 };  // LEFT, RIGHT
constexpr uint8_t SONAR_ECHO_PINS[2] = { 11, A5 };
constexpr uint8_t PIN_BUZZER = 12;
constexpr uint8_t PIN_LED = 13;
constexpr uint8_t TOUCH_PINS[4] = { A0, A1, A2, A3 };
const char *const TOUCH_NAMES[4] = { "TOP", "LEFT", "RIGHT", "REAR" };

#ifndef UNO_ENABLE_DEBUG_COMMANDS
#define UNO_ENABLE_DEBUG_COMMANDS 1
#endif

constexpr uint8_t SLOTS_PER_WHEEL_REV = 20;         // One FALLING edge per slot, on the wheel axle.
constexpr float DEFAULT_WHEEL_DIAMETER_MM = 67.5f;  // Measured wheel diameter.
constexpr float DEFAULT_SAFE_CM = 5.0f;
constexpr float FRONT_CAMERA_SAFE_CM = 15.0f;
constexpr float MIN_SAFE_CM = 5.0f;
constexpr float MAX_SAFE_CM = 100.0f;
constexpr uint32_t TOUCH_DEBOUNCE_MS = 45;
constexpr uint32_t SONAR_PERIOD_MS = 60;
constexpr uint32_t SONAR_TIMEOUT_US = 24000;
constexpr uint32_t SONAR_FRESH_MS = 500;
constexpr uint32_t CAMERA_FRESH_MS = 500;
constexpr uint32_t MOTOR_LEASE_MS = 500;
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
const char *const IO_NAMES[IO_COUNT] = { "MOTOR_LEFT", "MOTOR_RIGHT", "LED", "BUZZER", "SONAR_LEFT_TRIG", "SONAR_RIGHT_TRIG", "SONAR_LEFT_ECHO", "SONAR_RIGHT_ECHO", "ENCODER_LEFT", "ENCODER_RIGHT", "TOUCH_TOP", "TOUCH_LEFT", "TOUCH_RIGHT", "TOUCH_REAR" };
bool physicalIo[IO_COUNT] = { true,true,true,true,true,true,true,true,true,true,true,true,true,true };
bool ledOn = false;
bool ledFlash = false;
uint32_t ledFlashUntilMs = 0;
uint32_t ledFlashIntervalMs = 0;
uint32_t ledFlashStartedMs = 0;
uint32_t buzzerUntilMs = 0;
float wheelDiameterMm = DEFAULT_WHEEL_DIAMETER_MM;
float trackWidthMm = 0;  // Must be measured before ROTATE is enabled.
float rpm[2] = { 0, 0 };
uint32_t rpmAtMs = 0;
uint32_t rpmPulses[2] = { 0, 0 };
float dryRunFraction[2] = { 0, 0 };
uint32_t dryRunAtMs = 0;

enum MotionKind : uint8_t { MOTION_NONE,
                            MOTION_TIMED,
                            MOTION_PULSES,
                            MOTION_LEASE };
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

char inputLine[128];
uint8_t inputLength = 0;
bool inputOverflow = false;

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
  Serial.print(F("DONE "));
  Serial.print(id);
  if (suffix) {
    Serial.print(' ');
    Serial.print(suffix);
  }
  Serial.println();
  pumpSerialIn();
}
void error(uint16_t id, const __FlashStringHelper *reason) {
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
  if (motion.kind == MOTION_NONE) return;
  const uint16_t id = motion.id;
  uint32_t leftPulses, rightPulses;
  getPulses(leftPulses, rightPulses);
  leftPulses -= motion.basePulses[0];
  rightPulses -= motion.basePulses[1];
  motion.kind = MOTION_NONE;
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
  stopOutputs();
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
  Serial.print(F("EVENT "));
  Serial.print(source);
  Serial.print(F(" TOUCH "));
  Serial.print(TOUCH_NAMES[index]);
  Serial.print(pressed ? F(" PRESS ") : F(" RELEASE "));
  Serial.print(now);
  if (pressed) {
    touch.pressedAt = now;
    Serial.println();
    abortMotion(F("TOUCH"));
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

bool movesTowardFront(int16_t left, int16_t right);

const __FlashStringHelper *motionSafetyReason(int16_t left, int16_t right, uint32_t now, bool report) {
  float distance;
  if (movesTowardFront(left, right) && (!currentDistance(distance, now) || distance < FRONT_CAMERA_SAFE_CM)) {
    if (report) {
      Serial.print(F("EVENT OBSTACLE_NEAR FRONT "));
      if (currentDistance(distance, now)) Serial.println(distance, 1);
      else Serial.println(F("INVALID"));
      pumpSerialIn();
    }
    return F("FRONT_OBSTACLE_OR_CAMERA_INVALID");
  }
  if (left < right) {
    if (currentSideDistance(0, distance, now) && distance >= safeDistanceCm[0]) return nullptr;
    if (report) {
      Serial.print(F("EVENT OBSTACLE_NEAR LEFT "));
      if (currentSideDistance(0, distance, now)) Serial.println(distance, 1);
      else Serial.println(F("INVALID"));
      pumpSerialIn();
    }
    return F("SIDE_LEFT_OBSTACLE_OR_SENSOR_INVALID");
  }
  if (left > right) {
    if (currentSideDistance(1, distance, now) && distance >= safeDistanceCm[1]) return nullptr;
    if (report) {
      Serial.print(F("EVENT OBSTACLE_NEAR RIGHT "));
      if (currentSideDistance(1, distance, now)) Serial.println(distance, 1);
      else Serial.println(F("INVALID"));
      pumpSerialIn();
    }
    return F("SIDE_RIGHT_OBSTACLE_OR_SENSOR_INVALID");
  }
  return nullptr;
}

bool movesTowardFront(int16_t left, int16_t right) {
  return (left > 0 && right >= 0) || (right > 0 && left >= 0);
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
  const __FlashStringHelper *safetyReason = motionSafetyReason(motion.left, motion.right, now, true);
  if (safetyReason) {
    abortMotion(safetyReason);
    return;
  }
  uint32_t leftCount, rightCount;
  getPulses(leftCount, rightCount);
  const uint32_t counts[2] = { leftCount, rightCount };
  // Compact movement feedback only, independent of slow sensor-card polling.
  if (now - motionFeedbackAtMs >= 100 && sonarPhase == SONAR_IDLE && Serial.availableForWrite() >= 44) {
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
  if (deadlineReached(now, motion.deadlineMs)) {
    if (motion.kind == MOTION_LEASE) abortMotion(F("DEADMAN_TIMEOUT"));
    else if (motion.kind == MOTION_PULSES) abortMotion(F("MOTION_TIMEOUT"));
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
  if (ledFlash && deadlineReached(now, ledFlashUntilMs)) {
    ledFlash = false;
    ledOn = false;
  }
  digitalWrite(PIN_LED, physicalIo[LED_OUT] && (ledFlash ? ((now - ledFlashStartedMs) / ledFlashIntervalMs) % 2 == 0 : ledOn));
  digitalWrite(PIN_BUZZER, physicalIo[BUZZER_OUT] && buzzerUntilMs != 0 && !deadlineReached(now, buzzerUntilMs));
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

void startMotion(uint16_t id, MotionKind kind, int16_t left, int16_t right,
                 uint32_t durationMs, uint32_t targetPulses, bool estimated,
                 const __FlashStringHelper *name, bool gentleWiggle = false) {
  if (sessionStopped) {
    error(id, F("SESSION_STOPPED"));
    return;
  }
  if (motion.kind != MOTION_NONE) {
    error(id, F("BUSY"));
    return;
  }
  if (anyTouchPressed() && !gentleWiggle) {
    error(id, F("TOUCH_ACTIVE"));
    return;
  }
  const __FlashStringHelper *safetyReason = motionSafetyReason(left, right, millis(), false);
  if (safetyReason) {
    error(id, safetyReason);
    return;
  }
  if (kind == MOTION_PULSES && ((left && physicalIo[MOTOR_LEFT] && !physicalIo[ENCODER_LEFT]) || (right && physicalIo[MOTOR_RIGHT] && !physicalIo[ENCODER_RIGHT]))) {
    error(id, F("PHYSICAL_ENCODER_REQUIRED")); return;
  }
  const uint32_t now = millis();
  motion.kind = kind;
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

void printStatus(uint16_t id) {
  Serial.print(F("DATA "));
  Serial.print(id);
  Serial.print(F(" STATUS "));
  Serial.print(sessionStopped ? F("STOPPED") : motion.kind == MOTION_NONE ? F("IDLE")
                                                                          : F("MOVING"));
  Serial.print(F(" LED="));
  Serial.print(ledFlash ? F("FLASH") : ledOn ? F("ON")
                                             : F("OFF"));
  Serial.print(F(" BUZZER="));
  Serial.print(buzzerUntilMs && !deadlineReached(millis(), buzzerUntilMs) ? F("ON") : F("OFF"));
  Serial.print(F(" DEBUG="));
  Serial.print(debugEnabled ? F("ON") : F("OFF"));
  Serial.print(F(" INPUT_MODE=")); Serial.print(boardDryRun ? F("TEST") : F("LIVE"));
  Serial.print(F(" MOTOR_MODE="));
  Serial.println(!physicalIo[MOTOR_LEFT] && !physicalIo[MOTOR_RIGHT] ? F("OFF") : physicalIo[MOTOR_LEFT] && physicalIo[MOTOR_RIGHT] ? F("LIVE") : F("MIXED"));
}

int8_t touchIndex(const char *name) {
  for (uint8_t i = 0; i < 4; ++i)
    if (!strcmp(name, TOUCH_NAMES[i])) return i;
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
    Serial.print(IO_NAMES[i]); Serial.println(physicalIo[i] ? F(" ON") : F(" OFF")); pumpSerialIn();
  }
}
// Compact rows stay below 60 bytes including CRLF. Send only with enough TX room
// and between sonar measurements, so telemetry never waits on TX or loses an echo.
void tickHardwareTelemetry(uint32_t now) {
  if (!telemetryId || sonarPhase != SONAR_IDLE || Serial.availableForWrite() < 60) return;
  if (telemetryStage == 13) {
    const uint16_t id = telemetryId; telemetryId = 0; done(id, F("TELEMETRY")); return;
  }
  uint32_t leftCount, rightCount;
  getPulses(leftCount, rightCount);
  Serial.print(F("DATA ")); Serial.print(telemetryId); Serial.print(F(" HW "));
  if (telemetryStage == 0) {
    Serial.print(F("STATE ")); Serial.print(now); Serial.print(' ');
    Serial.print(sessionStopped ? F("STOPPED") : motion.kind == MOTION_NONE ? F("IDLE") : F("MOVING"));
    Serial.print(' '); Serial.print(debugEnabled); Serial.print(' '); Serial.print(boardDryRun);
    uint16_t mask = 0;
    for (uint8_t i = 0; i < IO_COUNT; ++i) if (physicalIo[i]) mask |= static_cast<uint16_t>(1) << i;
    Serial.print(' '); Serial.println(mask);
  } else if (telemetryStage <= 2) {
    const uint8_t side = telemetryStage - 1;
    Serial.print(side == 0 ? F("ML ") : F("MR "));
    Serial.print(motion.kind == MOTION_NONE ? 0 : side == 0 ? motion.left : motion.right);
    Serial.print(' '); Serial.print(appliedMotor[side]); Serial.print(' '); Serial.print(abs(appliedMotor[side]));
    Serial.print(' '); Serial.print(physicalIo[MOTOR_LEFT + side]);
    Serial.print(' '); Serial.print(digitalRead(side == 0 ? PIN_L_IN1 : PIN_R_IN1));
    Serial.print(' '); Serial.println(digitalRead(side == 0 ? PIN_L_IN2 : PIN_R_IN2));
  } else if (telemetryStage <= 4) {
    const uint8_t side = telemetryStage - 3;
    Serial.print(side == 0 ? F("EL ") : F("ER "));
    Serial.print(physicalIo[ENCODER_LEFT + side]); Serial.print(' ');
    Serial.print(physicalIo[ENCODER_LEFT + side] ? digitalRead(side == 0 ? PIN_ENC_LEFT : PIN_ENC_RIGHT) : -1);
    Serial.print(' '); Serial.print(side == 0 ? leftCount : rightCount);
    Serial.print(' '); Serial.print(rpm[side], 1); Serial.print(' ');
    Serial.println(physicalIo[ENCODER_LEFT + side] ? F("P") : boardDryRun && !physicalIo[MOTOR_LEFT + side] ? F("V") : F("O"));
  } else if (telemetryStage <= 6) {
    const uint8_t side = telemetryStage - 5;
    Serial.print(side == 0 ? F("SL ") : F("SR "));
    Serial.print(physicalIo[SONAR_LEFT_TRIG + side]); Serial.print(' ');
    Serial.print(physicalIo[SONAR_LEFT_ECHO + side]); Serial.print(' ');
    Serial.print(physicalIo[SONAR_LEFT_ECHO + side] ? digitalRead(SONAR_ECHO_PINS[side]) : -1); Serial.print(' ');
    const bool valid = physicalIo[SONAR_LEFT_TRIG + side] && physicalIo[SONAR_LEFT_ECHO + side] && physicalDistanceValid[side] && now - lastDistanceMs[side] <= SONAR_FRESH_MS;
    Serial.print(valid ? physicalDistanceCm[side] : -1.0f, 1); Serial.print(' ');
    float distance; Serial.print(currentSideDistance(side, distance, now) ? distance : -1.0f, 1); Serial.print(' ');
    Serial.println(distanceOverride[side] != DISTANCE_AUTO ? F("I") : physicalIo[SONAR_LEFT_TRIG + side] && physicalIo[SONAR_LEFT_ECHO + side] ? F("P") : F("O"));
  } else if (telemetryStage <= 10) {
    const uint8_t index = telemetryStage - 7;
    Serial.print(F("T ")); Serial.print(TOUCH_NAMES[index]); Serial.print(' ');
    Serial.print(physicalIo[TOUCH_TOP + index]); Serial.print(' ');
    Serial.print(physicalIo[TOUCH_TOP + index] ? digitalRead(TOUCH_PINS[index]) : -1); Serial.print(' ');
    Serial.print(touches[index].stable); Serial.print(' ');
    Serial.println(touches[index].overrideValue >= 0 ? F("I") : physicalIo[TOUCH_TOP + index] ? F("P") : F("O"));
  } else if (telemetryStage == 11) {
    const bool ledRequested = ledFlash ? ((now - ledFlashStartedMs) / ledFlashIntervalMs) % 2 == 0 : ledOn;
    Serial.print(F("OUT ")); Serial.print(physicalIo[LED_OUT]); Serial.print(' '); Serial.print(ledRequested);
    Serial.print(' '); Serial.print(digitalRead(PIN_LED)); Serial.print(' '); Serial.print(physicalIo[BUZZER_OUT]);
    Serial.print(' '); Serial.print(buzzerUntilMs && !deadlineReached(now, buzzerUntilMs));
    Serial.print(' '); Serial.println(digitalRead(PIN_BUZZER));
  } else {
    float distance; Serial.print(F("FRONT ")); Serial.print(currentDistance(distance, now) ? distance : -1.0f, 1);
    Serial.print(' '); Serial.println(boardDryRun ? F("TEST") : F("HOST"));
  }
  ++telemetryStage;
}

void processCommand(char *line) {
  // Web commands carry CRC-8. Unprotected manual commands remain available.
  char *star = strchr(line, '*');
  if (!strncmp(line, "C ", 2) && !star) { error(0, F("BAD_CHECKSUM")); return; }
  if (star) {
    if (strlen(star + 1) != 2 || !isxdigit(star[1]) || !isxdigit(star[2])) {
      error(0, F("BAD_CHECKSUM")); return;
    }
    const uint8_t received = static_cast<uint8_t>(strtoul(star + 1, nullptr, 16));
    *star = '\0';
    if (serialCrc8(line) != received) { error(0, F("BAD_CHECKSUM")); return; }
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
  if (count < 3 || (strcmp(words[0], "CMD") && strcmp(words[0], "C")) || !parseLong(words[1], 1, 65535, rawId)) {
    error(0, F("BAD_ENVELOPE"));
    return;
  }
  const uint16_t id = static_cast<uint16_t>(rawId);
  const char *verb = words[2];
  const uint8_t n = count - 3;
  const char *a = n > 0 ? words[3] : nullptr;
  const char *b = n > 1 ? words[4] : nullptr;
  const char *c = n > 2 ? words[5] : nullptr;
  if (!strcmp(verb, "CF")) verb = "CAMERA_FRONT";
  else if (!strcmp(verb, "GT")) verb = "GET_TELEMETRY";
  else if (!strcmp(verb, "DI")) {
    verb = "DEBUG_INJECT";
    if (a && !strcmp(a, "S")) a = "SONAR";
    if (b && !strcmp(b, "L")) b = "LEFT";
    else if (b && !strcmp(b, "R")) b = "RIGHT";
  }
  long x, y;
  float f, g;

  if (!strcmp(verb, "PING") && n == 0) {
    ack(id);
    done(id, F("PONG"));
    return;
  }
  if (!strcmp(verb, "GET_TELEMETRY") && n == 0) {
    if (telemetryId) { error(id, F("TELEMETRY_BUSY")); return; }
    telemetryId = id; telemetryStage = 0; ack(id); return;
  }
  if (!strcmp(verb, "GET_IO") && n == 0) { ack(id); printIo(id); done(id); return; }
  if (!strcmp(verb, "GET_STATUS") && n == 0) {
    ack(id);
    printStatus(id);
    done(id);
    return;
  }
  if (!strcmp(verb, "GET_DISTANCE") && n == 0) {
    ack(id);
    Serial.print(F("DATA "));
    Serial.print(id);
    Serial.print(F(" DISTANCE "));
    if (currentDistance(f, millis())) Serial.print(f, 1);
    else Serial.print(F("INVALID"));
    Serial.print(F(" SOURCE="));
    Serial.println(F("CAMERA_FRONT"));
    Serial.print(F("DATA "));
    Serial.print(id);
    Serial.print(F(" SIDE_LEFT "));
    if (currentSideDistance(0, f, millis())) Serial.print(f, 1);
    else Serial.print(F("INVALID"));
    Serial.print(F(" SOURCE="));
    Serial.println(distanceOverride[0] != DISTANCE_AUTO ? F("DEBUG_INJECT")
                   : !physicalIo[SONAR_LEFT_TRIG] || !physicalIo[SONAR_LEFT_ECHO] ? F("IO_DISABLED")
                                                        : F("SONAR_LEFT"));
    Serial.print(F("DATA "));
    Serial.print(id);
    Serial.print(F(" SIDE_RIGHT "));
    if (currentSideDistance(1, f, millis())) Serial.print(f, 1);
    else Serial.print(F("INVALID"));
    Serial.print(F(" SOURCE="));
    Serial.println(distanceOverride[1] != DISTANCE_AUTO ? F("DEBUG_INJECT")
                   : boardDryRun                        ? F("NO_SIM_MEASUREMENT")
                                                        : F("SONAR_RIGHT"));
    done(id);
    return;
  }
  if (!strcmp(verb, "GET_ENCODER") && n == 0) {
    uint32_t left, right;
    getPulses(left, right);
    ack(id);
    Serial.print(F("DATA "));
    Serial.print(id);
    Serial.print(F(" ENCODER "));
    Serial.print(left);
    Serial.print(' ');
    Serial.print(right);
    Serial.print(F(" LEFT_RPM="));
    Serial.print(rpm[0], 1);
    Serial.print(F(" RIGHT_RPM="));
    Serial.print(rpm[1], 1);
    Serial.print(F(" SOURCE="));
    Serial.print(F("MIXED LEFT_PHYSICAL=")); Serial.print(physicalIo[ENCODER_LEFT]);
    Serial.print(F(" RIGHT_PHYSICAL=")); Serial.println(physicalIo[ENCODER_RIGHT]);
    done(id);
    return;
  }
  if (!strcmp(verb, "GET_CONFIG") && n == 0) {
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
  if (!strcmp(verb, "CAMERA_FRONT") && n == 1) {
    if (!strcmp(a, "CLEAR")) cameraFrontState = CAMERA_CLEAR;
    else if (!strcmp(a, "BLOCKED")) cameraFrontState = CAMERA_BLOCKED;
    else if (!strcmp(a, "INVALID")) cameraFrontState = CAMERA_INVALID;
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
  if (!strcmp(verb, "SET_GEOMETRY") && n == 2 && parseFloat(a, 20, 150, f) && parseFloat(b, 50, 300, g)) {
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
  if (!strcmp(verb, "SET_SAFE_DISTANCE") && n == 2 && (!strcmp(a, "LEFT") || !strcmp(a, "RIGHT"))) {
    if (!parseFloat(b, MIN_SAFE_CM, MAX_SAFE_CM, f)) {
      error(id, F("INVALID_ARGUMENT"));
      return;
    }
    if (motion.kind != MOTION_NONE) {
      error(id, F("BUSY"));
      return;
    }
    safeDistanceCm[!strcmp(a, "LEFT") ? 0 : 1] = f;
    ack(id);
    done(id, F("SAFE_DISTANCE_SET"));
    return;
  }
  if (!strcmp(verb, "SESSION_START") && n == 0) {
    sessionStopped = false;
    ack(id);
    done(id, F("SESSION_STARTED"));
    return;
  }
  if (!strcmp(verb, "SESSION_STOP") && n == 0) {
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
  if (!strcmp(verb, "STOP") && n == 0) {
    ack(id);
    abortMotion(F("STOP_COMMAND"));
    done(id, F("MOTOR_STOPPED"));
    return;
  }
  if (!strcmp(verb, "KEEP") && n == 0) {
    if (motion.kind != MOTION_LEASE) { error(id, F("NO_ACTIVE_MOTOR")); return; }
    motion.deadlineMs = millis() + MOTOR_LEASE_MS;
    done(id, F("LEASE_RENEWED")); return;
  }
  if (!strcmp(verb, "MOVE") && n == 3 && (!strcmp(a, "FORWARD") || !strcmp(a, "BACKWARD")) && parseLong(b, 1, 255, x) && parseLong(c, 1, 10000, y)) {
    const int16_t speed = !strcmp(a, "FORWARD") ? x : -x;
    startMotion(id, MOTION_TIMED, speed, speed, y, 0, false,
                speed > 0 ? F("MOVE_FORWARD") : F("MOVE_BACKWARD"));
    return;
  }
  if (!strcmp(verb, "TURN") && n == 3 && (!strcmp(a, "LEFT") || !strcmp(a, "RIGHT")) && parseLong(b, 1, 255, x) && parseLong(c, 1, 10000, y)) {
    const int16_t speed = !strcmp(a, "RIGHT") ? x : -x;
    startMotion(id, MOTION_TIMED, speed, -speed, y, 0, false,
                speed > 0 ? F("TURN_RIGHT") : F("TURN_LEFT"),
                x <= 90 && y <= 400);
    return;
  }
  if (!strcmp(verb, "MOTOR") && n == 2 && parseLong(a, -255, 255, x) && parseLong(b, -255, 255, y)) {
    if (x == 0 && y == 0) {
      ack(id);
      abortMotion(F("MOTOR_ZERO"));
      done(id, F("MOTOR_STOPPED"));
      return;
    }
    if (motion.kind == MOTION_LEASE && motion.left == x && motion.right == y) {
      motion.deadlineMs = millis() + MOTOR_LEASE_MS;
      done(id, F("LEASE_RENEWED")); return;
    }
    if (motion.kind == MOTION_LEASE) abortMotion(F("REPLACED"));
    startMotion(id, MOTION_LEASE, x, y, MOTOR_LEASE_MS, 0, false, F("MOTOR"));
    return;
  }
  if (!strcmp(verb, "DRIVE") && n == 2 && parseFloat(a, -100, 100, f) && f != 0 && parseLong(b, 1, 255, x)) {
    const float circumference = PI * wheelDiameterMm;
    const uint32_t pulses = max(1L, lroundf(fabs(f) * 10.0f / circumference * SLOTS_PER_WHEEL_REV));
    const int16_t speed = f > 0 ? x : -x;
    startMotion(id, MOTION_PULSES, speed, speed, 10000, pulses, true, F("DRIVE"));
    return;
  }
  if (!strcmp(verb, "ROTATE") && n == 2 && parseFloat(a, -360, 360, f) && f != 0 && parseLong(b, 1, 255, x)) {
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
  if (!strcmp(verb, "LED") && n == 1 && (!strcmp(a, "ON") || !strcmp(a, "OFF"))) {
    if (sessionStopped && strcmp(a, "OFF")) {
      error(id, F("SESSION_STOPPED"));
      return;
    }
    ledFlash = false;
    ledOn = !strcmp(a, "ON");
    tickOutputs(millis());
    ack(id);
    done(id, ledOn ? F("LED_ON") : F("LED_OFF"));
    return;
  }
  if (!strcmp(verb, "LED") && n == 3 && !strcmp(a, "FLASH") && parseLong(b, 20, 5000, x) && parseLong(c, 20, 10000, y)) {
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
  if (!strcmp(verb, "BEEP") && n == 1 && parseLong(a, 1, 5000, x)) {
    if (sessionStopped) {
      error(id, F("SESSION_STOPPED"));
      return;
    }
    buzzerUntilMs = millis() + x;
    ack(id);
    done(id, F("BEEP_STARTED"));
    return;
  }

#if UNO_ENABLE_DEBUG_COMMANDS
  if ((!strcmp(verb, "DEBUG_IO") && n == 2) || (!strcmp(verb, "DEBUG_OUTPUTS") && n == 1 && !strcmp(a, "OFF"))) {
    if (!debugEnabled) { error(id, F("DEBUG_DISABLED")); return; }
    int8_t channel = -1;
    if (!strcmp(verb, "DEBUG_IO")) {
      for (uint8_t i = 0; i < IO_COUNT; ++i) if (!strcmp(a, IO_NAMES[i])) channel = i;
      if (channel < 0 || (strcmp(b, "ON") && strcmp(b, "OFF"))) { error(id, F("INVALID_ARGUMENT")); return; }
    }
    abortMotion(F("IO_CHANGE")); ledOn = ledFlash = false; buzzerUntilMs = 0;
    if (channel < 0) { for (uint8_t i = 0; i <= SONAR_RIGHT_TRIG; ++i) physicalIo[i] = false; }
    else {
      const bool changed = physicalIo[channel] != !strcmp(b, "ON");
      physicalIo[channel] = !strcmp(b, "ON");
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
  if (!strcmp(verb, "DEBUG_MODE") && n == 1 && (!strcmp(a, "ON") || !strcmp(a, "OFF"))) {
    abortMotion(F("DEBUG_MODE_CHANGE"));
    debugEnabled = !strcmp(a, "ON");
    if (!debugEnabled) {
      boardDryRun = false;
      clearDebugInputs();
    }
    ack(id);
    done(id, debugEnabled ? F("DEBUG_ON") : F("DEBUG_OFF"));
    return;
  }
  if (!strcmp(verb, "DEBUG_MOTOR_MODE") && n == 1 && (!strcmp(a, "DRY_RUN") || !strcmp(a, "LIVE"))) {
    if (!debugEnabled) {
      error(id, F("DEBUG_DISABLED"));
      return;
    }
    abortMotion(F("DEBUG_MODE_CHANGE"));
    setBoardDryRun(!strcmp(a, "DRY_RUN"));
    ack(id);
    done(id, boardDryRun ? F("DRY_RUN") : F("LIVE"));
    return;
  }
  if (!strcmp(verb, "DEBUG_INJECT")) {
    if (!debugEnabled) {
      error(id, F("DEBUG_DISABLED"));
      return;
    }
    if (n == 3 && !strcmp(a, "TOUCH") && touchIndex(b) >= 0 && (!strcmp(c, "PRESS") || !strcmp(c, "RELEASE") || !strcmp(c, "AUTO"))) {
      if (physicalIo[TOUCH_TOP + touchIndex(b)] && strcmp(c, "AUTO")) {
        error(id, F("DRY_RUN_REQUIRED"));
        return;
      }
      const uint8_t index = touchIndex(b);
      ack(id);
      if (!strcmp(c, "AUTO")) {
        touches[index].overrideValue = -1;
        touches[index].physicalRaw = physicalIo[TOUCH_TOP + index] && digitalRead(TOUCH_PINS[index]) == LOW;
        touches[index].changedAt = millis();
        if (!physicalIo[TOUCH_TOP + index]) setTouch(index, false, F("DRY_RUN_DEFAULT"));
        else setTouch(index, touches[index].physicalRaw, F("PHYSICAL"));
      } else {
        const bool pressed = !strcmp(c, "PRESS");
        touches[index].overrideValue = pressed ? 1 : 0;
        setTouch(index, pressed, F("DEBUG_INJECT"));
      }
      done(id);
      return;
    }
    if (n == 3 && !strcmp(a, "SONAR") && (!strcmp(b, "LEFT") || !strcmp(b, "RIGHT"))) {
      if ((!boardDryRun || (physicalIo[SONAR_LEFT_TRIG + (!strcmp(b, "LEFT") ? 0 : 1)] && physicalIo[SONAR_LEFT_ECHO + (!strcmp(b, "LEFT") ? 0 : 1)])) && strcmp(c, "AUTO")) {
        error(id, F("DRY_RUN_REQUIRED"));
        return;
      }
      const uint8_t side = !strcmp(b, "LEFT") ? 0 : 1;
      if (!strcmp(c, "AUTO")) distanceOverride[side] = DISTANCE_AUTO;
      else if (!strcmp(c, "INVALID")) distanceOverride[side] = DISTANCE_INVALID;
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
    if (n == 3 && !strcmp(a, "ENCODER") && parseLong(b, 0, 10000, x) && parseLong(c, 0, 10000, y)) {
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
  if (!strncmp(verb, "DEBUG_", 6)) {
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
char pumpedLine[128];
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
  processCommand(line);
  dispatchingCommand = false;
}

void processInputByte(const char ch) {
  const int8_t result = assembleInputChar(ch, inputLine, inputLength, inputOverflow);
  if (result == -1) {
    if (!dispatchingCommand) error(0, F("LINE_TOO_LONG"));
    return;
  }
  if (result != 1) return;
  if (!dispatchingCommand) {
    dispatchCommandLine(inputLine);
    return;
  }
  if (pumpedReady) return;
  strncpy(pumpedLine, inputLine, sizeof(pumpedLine) - 1);
  pumpedLine[sizeof(pumpedLine) - 1] = '\0';
  pumpedReady = true;
}

void pumpSerialIn() {
  // Re-entrancy guard: bytes completed during an error()/ack() pump stay in the hardware
  // ring until the outer pump finishes, bounding stack depth on pathological line floods.
  static bool pumping = false;
  if (pumping) return;
  pumping = true;
  while (Serial.available() > 0) processInputByte((char)Serial.read());
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
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  digitalWrite(PIN_BUZZER, LOW);
  for (uint8_t i = 0; i < 4; ++i) {
    pinMode(TOUCH_PINS[i], INPUT_PULLUP);
    touches[i] = { digitalRead(TOUCH_PINS[i]) == LOW, false, millis(), 0, -1 };
  }
  pinMode(PIN_ENC_LEFT, INPUT_PULLUP);
  pinMode(PIN_ENC_RIGHT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_LEFT), onLeftPulse, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_RIGHT), onRightPulse, FALLING);
  Serial.begin(57600);
  rpmAtMs = millis();
  dryRunAtMs = millis();
  Serial.println(F("READY UNO_CAT_V5.6"));
  // V5.1: keep assembling host commands while Serial.print waits for TX headroom, so
  // command bursts (sensor injections + ROTATE/DRIVE) can no longer overrun the RX ring.
}

void loop() {
  readSerial();
  const uint32_t now = millis();
  tickTouches(now);
  tickSonar(now);
  tickDryRun(now);
  tickMotion(now);
  tickRpm(now);
  tickOutputs(now);
  tickHardwareTelemetry(now);
}
