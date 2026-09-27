import test from "node:test";
import assert from "node:assert/strict";
import { escapeTurn, observedTravelCm } from "./escape-planner.mjs";
test("escape uses visible bearing or measured contact side", () => {
  assert.equal(escapeTurn({ detected: true, relativeDeg: 20 }, "LEFT"), -160);
  assert.equal(escapeTurn({ detected: false, relativeDeg: null }, "REAR"), 0);
  assert.equal(escapeTurn({ detected: false, relativeDeg: 120 }, "LEFT"), 90);
});
test("travel requires a current forward measurement after the turn", () => {
  assert.equal(observedTravelCm(null, 26), 0);
  assert.equal(observedTravelCm(19, 26), 4);
  assert.equal(observedTravelCm(27, 26), 12);
  assert.equal(observedTravelCm(90, 26), 26);
});
