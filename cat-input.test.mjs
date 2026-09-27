import test from "node:test";
import assert from "node:assert/strict";
import { initialWorld } from "./core.mjs";
import { CatMotionInput } from "./cat-input.mjs";

test("attack input stays in the same contact until the cat physically leaves", () => {
  const world = initialWorld();
  world.cat = { x: world.robot.x, y: world.robot.y };
  const input = new CatMotionInput(world);
  input.configure({ behavior: "ATTACK", attackArea: "TOP", speedCmS: 1,
    holdMs: 100, intervalMs: 500 });
  input.start();
  input.attackCount = 1;
  input.activeTouch = "TOP";
  input.phase = "HOLD";
  input.phaseUntil = 100;
  input.step(110, .05);
  input.step(1000, .05);
  assert.equal(input.attackCount, 1);
  assert.equal(input.touchOverride(), "TOP");
  assert.equal(input.phase, "RETREAT");
  world.cat.x = world.robot.x - 150;
  assert.deepEqual(input.step(1050, .05), [{ type: "ATTACK_RELEASE", area: "TOP" }]);
  assert.equal(input.touchOverride(), null);
  input.step(1100, .05);
  assert.equal(input.phase, "APPROACH");
});
