import test from "node:test";
import assert from "node:assert/strict";
import { SceneSensorPublisher } from "./sensor-stream.mjs";

test("empty board sends no sensor traffic while idle and refreshes only the forward input during travel", () => {
  const publisher = new SceneSensorPublisher();
  const scene = { cameraValue: "80.0", sides: { LEFT: "50.0", RIGHT: "60.0" } };
  assert.deepEqual(publisher.plan({ now: 0, ...scene }), []);
  assert.deepEqual(publisher.plan({ now: 1000, ...scene }), []);
  assert.deepEqual(publisher.plan({ now: 1001, ...scene, beforeMotion: true }), [
    "CAMERA_FRONT 80.0", "DEBUG_INJECT SONAR LEFT 50.0", "DEBUG_INJECT SONAR RIGHT 60.0",
  ]);
  assert.deepEqual(publisher.plan({ now: 1250, ...scene, moving: true }), []);
  assert.deepEqual(publisher.plan({ now: 1352, ...scene, moving: true }), ["CAMERA_FRONT 80.0"]);
  assert.deepEqual(publisher.plan({ now: 1703, ...scene, moving: true }), ["CAMERA_FRONT 80.0"]);
});

test("turns avoid camera traffic but side safety changes are sent promptly", () => {
  const publisher = new SceneSensorPublisher();
  const scene = { cameraValue: "80.0", sides: { LEFT: "50.0", RIGHT: "60.0" } };
  assert.deepEqual(publisher.plan({ now: 0, ...scene, beforeMotion: true, frontRequired: false }), [
    "DEBUG_INJECT SONAR LEFT 50.0", "DEBUG_INJECT SONAR RIGHT 60.0",
  ]);
  assert.deepEqual(publisher.plan({ now: 500, ...scene, moving: true, frontRequired: false }), []);
  assert.deepEqual(publisher.plan({ now: 510, ...scene, moving: true, frontRequired: false,
    sides: { LEFT: "4.0", RIGHT: "60.0" } }), ["DEBUG_INJECT SONAR LEFT 4.0"]);
  assert.deepEqual(publisher.plan({ now: 520, ...scene, moving: true, frontRequired: false,
    sides: { LEFT: "50.0", RIGHT: "60.0" } }), ["DEBUG_INJECT SONAR LEFT 50.0"]);
  assert.deepEqual(publisher.plan({ now: 530, ...scene, moving: true, frontRequired: false,
    sides: { LEFT: "53.5", RIGHT: "60.0" } }), []);
  assert.deepEqual(publisher.plan({ now: 771, ...scene, moving: true, frontRequired: false,
    sides: { LEFT: "53.5", RIGHT: "60.0" } }), ["DEBUG_INJECT SONAR LEFT 53.5"]);
});

test("new motion after SESSION_STOP restores cleared sensor inputs", () => {
  const publisher = new SceneSensorPublisher();
  const scene = { cameraValue: "80.0", sides: { LEFT: "50.0", RIGHT: "60.0" }, beforeMotion: true };
  publisher.plan({ now: 0, ...scene });
  publisher.reset();
  assert.deepEqual(publisher.plan({ now: 100, ...scene }), [
    "CAMERA_FRONT 80.0", "DEBUG_INJECT SONAR LEFT 50.0", "DEBUG_INJECT SONAR RIGHT 60.0",
  ]);
});
