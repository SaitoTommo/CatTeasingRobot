import test from "node:test";
import assert from "node:assert/strict";
import { decideLocalAvoidance } from "./route-planner.mjs";
test("patrol moves in bounded steps from current range", () => {
  assert.deepEqual(decideLocalAvoidance({ frontCm: 80, requestedCm: 90 }), { action: "DRIVE", distanceCm: 12 });
  assert.deepEqual(decideLocalAvoidance({ frontCm: 22, requestedCm: 90 }), { action: "DRIVE", distanceCm: 7 });
});
test("obstruction requests real scanning, invalid range stops", () => {
  assert.deepEqual(decideLocalAvoidance({ frontCm: 10, requestedCm: 90 }), { action: "SCAN" });
  assert.deepEqual(decideLocalAvoidance({ frontCm: null, requestedCm: 90 }), { action: "STOP" });
});
