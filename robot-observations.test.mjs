import test from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { initialWorld, cameraObservation } from "./core.mjs";
import { sideObservation, frontObservation, cameraObservationInput, CapturedCatWait } from "./robot-observations.mjs";

test("moving an unseen rear cat cannot confirm departure or change captured-wait decisions", () => {
  const a = initialWorld(), b = initialWorld();
  a.robot = b.robot = { x: 300, y: 300, heading: 0 };
  a.cat = { x: 100, y: 300 };
  const options = { startedAt: 0, maxWaitMs: 1000, farCm: 70, farHoldMs: 200 };
  const stationary = new CapturedCatWait(options), moved = new CapturedCatWait(options);
  for (let now = 0; now <= 1000; now += 100) {
    b.cat = { x: 120 - now / 20, y: 330 };
    const cameraA = cameraObservation(a), cameraB = cameraObservation(b);
    assert.deepEqual(cameraA, cameraB);
    const resultA = stationary.update({ camera: cameraA, touchActive: false, now });
    const resultB = moved.update({ camera: cameraB, touchActive: false, now });
    assert.deepEqual(resultA, resultB);
    assert.equal(resultB.confirmedLeft, false);
    if (now === 1000) assert.equal(resultB.reason, "MAX_WAIT_UNCONFIRMED");
  }
});

test("touch release, loss of sight and invalid vision never count as departure", () => {
  const wait = new CapturedCatWait({ startedAt: 0, maxWaitMs: 10000, farCm: 70, farHoldMs: 500 });
  const unknown = { detected: false, distanceCm: null };
  wait.update({ camera: unknown, touchActive: true, now: 0 });
  assert.equal(wait.update({ camera: unknown, touchActive: false, now: 500 }).reason, "CAT_POSITION_UNKNOWN");
  const far = { valid: true, detected: true, distanceCm: 100 };
  wait.update({ camera: far, touchActive: false, now: 1000 });
  assert.equal(wait.update({ camera: unknown, touchActive: false, now: 1500 }).confirmedLeft, false);
  assert.equal(wait.update({ camera: { ...far, valid: false }, touchActive: false, now: 2000 }).confirmedLeft, false);
  wait.update({ camera: far, touchActive: false, now: 3000 });
  assert.equal(wait.update({ camera: far, touchActive: false, now: 3500 }).reason, "CAT_LEFT_OBSERVED");
});

test("enabled physical sonar uses actual measured range, never the simulated clearance", () => {
  const physicalIo = new Map([["SONAR_LEFT_TRIG", true], ["SONAR_LEFT_ECHO", true]]);
  const rows = new Map([["SL", { at: 0, values: ["1", "1", "0", "3.0", "3.0", "P"] }]]);
  const args = { side: "LEFT", mode: "serial", physicalIo, rows, simulatedAllowed: true,
    readSimulated: () => { throw new Error("hidden simulated fallback"); } };
  assert.deepEqual(sideObservation({ ...args, now: 100 }), { valid: true, distanceCm: 3, source: "PHYSICAL" });
  assert.equal(sideObservation({ ...args, now: 501 }).distanceCm, null);
  rows.clear();
  assert.equal(sideObservation({ ...args, now: 100 }).distanceCm, null);
});

test("unknown gates, partial physical sonar and injected rows cannot masquerade as physical input", () => {
  const args = { side: "RIGHT", mode: "serial", rows: new Map(), now: 0,
    simulatedAllowed: true, readSimulated: () => { throw new Error("forbidden fallback"); } };
  assert.equal(sideObservation({ ...args, physicalIo: new Map() }).valid, false);
  assert.equal(sideObservation({ ...args, physicalIo: new Map([["SONAR_RIGHT_TRIG", false], ["SONAR_RIGHT_ECHO", true]]) }).valid, false);
  args.rows.set("SR", { at: 0, values: ["1", "1", "0", "90", "90", "I"] });
  assert.equal(sideObservation({ ...args, physicalIo: new Map([["SONAR_RIGHT_TRIG", true], ["SONAR_RIGHT_ECHO", true]]) }).valid, false);
});

test("explicit virtual input uses the simulated sensor; disabled input remains unknown", () => {
  const args = { side: "LEFT", mode: "serial", physicalIo: new Map([["SONAR_LEFT_TRIG", false], ["SONAR_LEFT_ECHO", false]]),
    rows: new Map(), now: 0, readSimulated: () => 42 };
  assert.equal(sideObservation({ ...args, simulatedAllowed: true }).distanceCm, 42);
  assert.equal(sideObservation({ ...args, simulatedAllowed: false }).distanceCm, null);
});

test("real front input is current host data; no camera input stays unknown", () => {
  const rows = new Map([["FRONT", { at: 100, values: ["12", "HOST"] }]]);
  const args = { mode: "serial", simulatedAllowed: false, rows,
    readSimulated: () => { throw new Error("forbidden geometry"); } };
  assert.equal(frontObservation({ ...args, now: 200 }).distanceCm, 12);
  assert.equal(frontObservation({ ...args, now: 601 }).distanceCm, null);
  rows.get("FRONT").values[1] = "TEST";
  assert.equal(frontObservation({ ...args, now: 200 }).distanceCm, null);
  const camera = cameraObservationInput({ simulatedAllowed: false, readSimulated: args.readSimulated });
  assert.equal(camera.valid, false);
  assert.equal(camera.distanceCm, null);
});

test("source selection and captured-wait logic never import scene geometry or cat scripts", async () => {
  const source = await readFile(new URL("robot-observations.mjs", import.meta.url), "utf8");
  assert.doesNotMatch(source, /\bworld\b|catInput|cameraFrontDistanceCm|sideSonarDistanceCm|\.\/core\.mjs/);
});
