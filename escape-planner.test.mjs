import test from "node:test";
import assert from "node:assert/strict";
import { chooseEscapeRoute, planSimulatedEscapeRoute } from "./escape-planner.mjs";
import { initialWorld } from "./core.mjs";

test("corner escape uses an open side when behind is blocked", () => {
  const route = chooseEscapeRoute([
    { angle: 180, frontClearanceCm: 10, turnAllowed: true, priority: 0 },
    { angle: -90, frontClearanceCm: 60, turnAllowed: true, priority: 1 },
    { angle: 90, frontClearanceCm: 50, turnAllowed: true, priority: 2 },
  ], 26);
  assert.equal(route.angle, -90);
  assert.equal(route.distanceCm, 26);
});

test("escape shortens the move or stops when clearance is insufficient", () => {
  assert.equal(chooseEscapeRoute([
    { angle: 180, frontClearanceCm: 27, turnAllowed: true, priority: 0 },
  ], 26).distanceCm, 12);
  assert.equal(chooseEscapeRoute([
    { angle: -90, frontClearanceCm: 60, turnAllowed: false, priority: 0 },
    { angle: 180, frontClearanceCm: 19, turnAllowed: true, priority: 1 },
  ], 26), null);
});

test("simulated corner chooses the open right corridor", () => {
  const world = initialWorld();
  world.robot = { x: 40, y: 40, heading: 0 };
  world.cat = { x: 83, y: 40 };
  world.obstacle = { x: 900, y: 500, radius: 20 };
  const route = planSimulatedEscapeRoute(world, 26);
  assert.equal(route.angle, 90);
  assert.equal(route.distanceCm, 26);
});

test("escape aims away from the cat before any camera search", () => {
  const world = initialWorld();
  world.robot = { x: 500, y: 315, heading: 0 };
  world.cat = { x: 430, y: 315 }; // cat behind the robot
  world.obstacle = { x: 900, y: 500, radius: 20 };
  const route = planSimulatedEscapeRoute(world, 26);
  assert.equal(route.angle, 0, "turning 180 degrees here would drive into the cat");
  assert.equal(route.distanceCm, 26);
});
