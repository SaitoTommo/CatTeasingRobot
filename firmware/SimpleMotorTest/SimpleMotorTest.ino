// Arduino Uno + L298N: keep ENA/ENB jumpered to logic 5V.
// Do not connect D5/D6 while the enable jumpers are installed.
// Serial Monitor: 115200 baud. A/B/C/D run one motor; S stops both.
// Motors continue running until the next command. Lift the wheels for testing.

const byte LEFT_IN1 = 4;
const byte LEFT_IN2 = 7;
const byte RIGHT_IN1 = 8;  // L298N IN3
const byte RIGHT_IN2 = 9;  // L298N IN4

void stopMotors() {
  digitalWrite(LEFT_IN1, LOW);
  digitalWrite(LEFT_IN2, LOW);
  digitalWrite(RIGHT_IN1, LOW);
  digitalWrite(RIGHT_IN2, LOW);
}

void setup() {
  pinMode(LEFT_IN1, OUTPUT);
  pinMode(LEFT_IN2, OUTPUT);
  pinMode(RIGHT_IN1, OUTPUT);
  pinMode(RIGHT_IN2, OUTPUT);
  stopMotors();
  Serial.begin(115200);
  Serial.println("READY: A=left forward, B=left reverse, C=right forward, D=right reverse, S=stop");
}

void loop() {
  if (!Serial.available()) return;
  const char command = Serial.read();
  if (command == '\r' || command == '\n' || command == ' ') return;

  // Each command stops both motors before selecting just one direction.
  stopMotors();
  switch (command) {
    case 'A':
      digitalWrite(LEFT_IN1, HIGH);
      Serial.println("A: LEFT FORWARD");
      break;
    case 'B':
      digitalWrite(LEFT_IN2, HIGH);
      Serial.println("B: LEFT REVERSE");
      break;
    case 'C':
      digitalWrite(RIGHT_IN1, HIGH);
      Serial.println("C: RIGHT FORWARD");
      break;
    case 'D':
      digitalWrite(RIGHT_IN2, HIGH);
      Serial.println("D: RIGHT REVERSE");
      break;
    case 'S':
      Serial.println("S: STOP");
      break;
    default:
      Serial.println("UNKNOWN: use A, B, C, D or S. Motors stopped.");
      break;
  }
}
