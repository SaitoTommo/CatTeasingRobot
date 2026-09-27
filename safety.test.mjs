import test from "node:test";
import assert from "node:assert/strict";
import { cameraObservation, cameraTargetObservation, initialWorld, MockUno } from "./core.mjs";

test("simulated camera detects distant cat and route tag within its viewing angle", () => {
  const world = initialWorld();
  world.robot = { x: 40, y: 300, heading: 0 };
  world.cat = { x: 960, y: 300 };
  assert.equal(cameraObservation(world).detected, true);
  assert.ok(cameraObservation(world).distanceCm > 130);
  assert.equal(cameraTargetObservation(world, { x: 850, y: 300 }).detected, true);
  world.cat.y = 590;
  assert.equal(cameraObservation(world).detected, true);
  world.cat.y = 0;
  assert.equal(cameraObservation(world).detected, true);
  world.robot.heading = Math.PI;
  assert.equal(cameraObservation(world).detected, false);
});

test("camera protects forward travel and each sonar protects its own turn", () => {
  const lines = [];
  const uno = new MockUno(initialWorld(), line => lines.push(line));
  let id = 1;
  const send = (command, now = 0) => {
    const current = id++;
    uno.receiveWire(`CMD ${current} ${command}`, now);
    return current;
  };
  send("CAMERA_FRONT 100");
  send("DEBUG_INJECT SONAR LEFT 4");
  send("DEBUG_INJECT SONAR RIGHT 100");
  const leftTurn = send("MOTOR -80 80");
  assert.ok(lines.includes(`ERROR ${leftTurn} SIDE_LEFT_OBSTACLE_OR_SENSOR_INVALID`));
  const forward = send("MOTOR 80 80");
  assert.ok(lines.includes(`EVENT MOTION_STARTED ${forward} MOTOR`));
  send("STOP");
  const rightTurn = send("MOTOR 80 -80");
  assert.ok(lines.includes(`EVENT MOTION_STARTED ${rightTurn} MOTOR`));
  send("STOP");
  send("CAMERA_FRONT INVALID");
  const blockedForward = send("MOTOR 80 80");
  assert.ok(lines.includes(`ERROR ${blockedForward} FRONT_OBSTACLE_OR_CAMERA_INVALID`));
  send("CAMERA_FRONT 100");
  const staleForward = send("MOTOR 80 80", 501);
  assert.ok(lines.includes(`ERROR ${staleForward} FRONT_OBSTACLE_OR_CAMERA_INVALID`));
  send("CAMERA_FRONT BLOCKED", 502);
  const cameraBlocked = send("MOTOR 80 80", 502);
  assert.ok(lines.includes(`ERROR ${cameraBlocked} FRONT_OBSTACLE_OR_CAMERA_INVALID`));
  send("CAMERA_FRONT CLEAR", 503);
  const cameraClear = send("MOTOR 80 80", 503);
  assert.ok(lines.includes(`EVENT MOTION_STARTED ${cameraClear} MOTOR`));
});

test("both side sonars default to 5 cm and have separate adjustable limits", () => {
  const lines = [];
  const uno = new MockUno(initialWorld(), line => lines.push(line));
  let id = 1;
  const send = command => {
    const current = id++;
    uno.receiveWire(`CMD ${current} ${command}`, 0);
    return current;
  };
  const config = send("GET_CONFIG");
  assert.ok(lines.some(line => line.startsWith(`DATA ${config} CONFIG `) &&
    line.includes("SAFE_LEFT_CM=5.0 SAFE_RIGHT_CM=5.0")));
  send("DEBUG_INJECT SONAR LEFT 4.9");
  const blockedLeft = send("TURN LEFT 80 100");
  assert.ok(lines.includes(`ERROR ${blockedLeft} SIDE_LEFT_OBSTACLE_OR_SENSOR_INVALID`));
  send("DEBUG_INJECT SONAR LEFT 5");
  const allowedLeft = send("TURN LEFT 80 100");
  assert.ok(lines.includes(`EVENT MOTION_STARTED ${allowedLeft} TURN_LEFT`));
  send("STOP");
  send("DEBUG_INJECT SONAR RIGHT 4.9");
  const blockedRight = send("TURN RIGHT 80 100");
  assert.ok(lines.includes(`ERROR ${blockedRight} SIDE_RIGHT_OBSTACLE_OR_SENSOR_INVALID`));
  send("DEBUG_INJECT SONAR RIGHT 5");
  const allowedRight = send("TURN RIGHT 80 100");
  assert.ok(lines.includes(`EVENT MOTION_STARTED ${allowedRight} TURN_RIGHT`));
  send("STOP");
  const setLeft = send("SET_SAFE_DISTANCE LEFT 10");
  assert.ok(lines.includes(`DONE ${setLeft} SAFE_DISTANCE_SET`));
  const blockedAtNine = send("TURN LEFT 80 100");
  assert.ok(lines.includes(`ERROR ${blockedAtNine} SIDE_LEFT_OBSTACLE_OR_SENSOR_INVALID`));
  const rejectFront = send("SET_SAFE_DISTANCE FRONT 5");
  assert.ok(lines.includes(`ERROR ${rejectFront} UNKNOWN_OR_INVALID_COMMAND`));
});

test("signed rotation can turn left and still obey left sonar", () => {
  const lines = [];
  const uno = new MockUno(initialWorld(), line => lines.push(line));
  uno.receiveWire("CMD 1 SET_GEOMETRY 65 120", 0);
  uno.receiveWire("CMD 2 DEBUG_INJECT SONAR LEFT 4", 0);
  uno.receiveWire("CMD 3 ROTATE -90 110", 0);
  assert.ok(lines.includes("ERROR 3 SIDE_LEFT_OBSTACLE_OR_SENSOR_INVALID"));
  uno.receiveWire("CMD 4 DEBUG_INJECT SONAR LEFT 100", 0);
  uno.receiveWire("CMD 5 ROTATE -90 110", 0);
  assert.ok(lines.includes("EVENT MOTION_STARTED 5 ROTATE ESTIMATED"));
});

test("one continuous overlap emits one press until the cat separates and returns", () => {
  const world = initialWorld();
  world.cat = { x: world.robot.x, y: world.robot.y + 30 };
  const lines = [];
  const uno = new MockUno(world, line => lines.push(line));
  uno.step(0);
  uno.step(50);
  world.robot.heading = Math.PI;
  uno.step(500);
  uno.step(5000);
  assert.equal(lines.filter(line => /EVENT SIM_CONTACT TOUCH .* PRESS/.test(line)).length, 1);
  assert.equal(lines.filter(line => /EVENT SIM_CONTACT TOUCH .* RELEASE/.test(line)).length, 0);
  world.cat.y = world.robot.y + 150;
  uno.step(5050);
  uno.step(5100);
  assert.equal(lines.filter(line => /EVENT SIM_CONTACT TOUCH .* RELEASE/.test(line)).length, 1);
  world.cat.y = world.robot.y + 30;
  uno.step(5150);
  uno.step(5200);
  assert.equal(lines.filter(line => /EVENT SIM_CONTACT TOUCH .* PRESS/.test(line)).length, 2);
});
