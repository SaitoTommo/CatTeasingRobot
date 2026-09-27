import test from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { initialWorld, cameraObservation, cameraTargetObservation, cameraFrontDistanceCm, sideSonarDistanceCm, contactArea, MockUno } from "./core.mjs";
import { Esp32DecisionSystem } from "./decision-system.mjs";
test("out-of-view and occluded objects do not expose bearing or distance", () => {
  const world = initialWorld();
  world.robot = { x: 300, y: 300, heading: 0 };
  world.cat = { x: 100, y: 300 };
  const behind = cameraObservation(world);
  assert.equal(behind.detected, false);
  assert.equal(behind.distanceCm, null);
  assert.equal(behind.relativeDeg, null);
  world.cat = { x: 150, y: 350 };
  assert.deepEqual(cameraObservation(world), behind, "hidden movement cannot change the delivered observation");
  world.cat = { x: 650, y: 300 };
  world.obstacle = { x: 450, y: 300, radius: 50 };
  assert.equal(cameraObservation(world).detected, false);
  assert.equal(cameraTargetObservation(world, { x: 700, y: 300 }).detected, false);
  world.obstacle.y = 500;
  assert.equal(cameraObservation(world).detected, true);
});
test("rays measure nearest surfaces and penetration measures zero", () => {
  const world = initialWorld();
  world.robot = { x: 300, y: 300, heading: 0 };
  world.cat = { x: 100, y: 500 };
  world.obstacle = { x: 426, y: 300, radius: 20 };
  assert.equal(cameraFrontDistanceCm(world), 20);
  world.obstacle.x = 326;
  assert.equal(cameraFrontDistanceCm(world), 0);
  world.obstacle = { x: 300, y: 226, radius: 20 };
  assert.equal(sideSonarDistanceCm(world, "LEFT"), 7);
  assert.equal(contactArea(world), null);
  world.cat = { x: 300, y: 260 };
  assert.equal(contactArea(world), "LEFT");
});
test("wheel observations track rotation instead of reading scene heading", () => {
  const world = initialWorld();
  const uno = new MockUno(world, () => {});
  uno.receiveWire("CMD 1 MOTOR 80 -80", 0);
  const heading = world.robot.heading;
  for (let now = 10; now <= 200; now += 10) uno.step(now);
  const radians = (uno.encoder.left + uno.encoder.right) * Math.PI * uno.wheelDiameterMm / 20 / 120;
  assert.ok(Math.abs(world.robot.heading - heading - radians) < 1e-8);
});
test("external alerts cannot initiate behavior without camera detection", () => {
  const system = new Esp32DecisionSystem();
  const context = { phase: "PATROL", camera: { detected: false }, now: 10 };
  assert.equal(system.onMoodAlert("HTTP_NEGATIVE", context), null);
  assert.equal(system.onMoodAlert("CAMERA_DETECTED", context), null);
});
test("decision and navigation code cannot access world geometry", async () => {
  for (const file of ["decision-system.mjs", "route-planner.mjs", "escape-planner.mjs"]) {
    const source = await readFile(new URL(file, import.meta.url), "utf8");
    assert.doesNotMatch(source, /\bworld\b|cameraFrontDistanceCm|cameraTargetObservation|\bMath\.atan2|\.\/core\.mjs/);
  }
  const app = await readFile(new URL("app.mjs", import.meta.url), "utf8");
  const controllers = app.slice(app.indexOf("async function scanUntilVisible"), app.indexOf("function startSession"));
  assert.doesNotMatch(controllers, /\bworld\b|planSceneDetour|planSimulatedEscapeRoute/);
});

test("page controls referenced by the controller exist in the actual HTML", async () => {
  const app = await readFile(new URL("app.mjs", import.meta.url), "utf8");
  const html = await readFile(new URL("index.html", import.meta.url), "utf8");
  for (const match of app.matchAll(/\$\("([^"\n]+)"\)/g))
    assert.ok(html.includes('id="' + match[1] + '"'), match[1]);
});
test("injected side measurements expire instead of persisting indefinitely", () => {
  const uno = new MockUno(initialWorld(), () => {});
  uno.receiveWire("CMD 1 DEBUG_INJECT SONAR LEFT 90", 0);
  assert.equal(uno.sideDistanceCm("LEFT"), 90);
  uno.step(501);
  assert.equal(uno.sideDistanceCm("LEFT"), null);
});
