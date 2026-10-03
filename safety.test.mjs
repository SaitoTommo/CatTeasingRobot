import test from "node:test";
import assert from "node:assert/strict";
import { cameraObservation, cameraTargetObservation, initialWorld, MockUno } from "./core.mjs";

test("invalid and expired sonar permit motion; only a fresh reading below 1 cm stops it",()=>{
  for(const side of ['LEFT','RIGHT']) {
    const lines=[],uno=new MockUno(initialWorld(),line=>lines.push(line));let id=1;
    const send=(command,now=0)=>{const n=id++;uno.receiveWire(`CMD ${n} ${command}`,now);return n;};
    const motor=side==='LEFT'?'MOTOR -80 80':'MOTOR 80 -80';
    send(`DEBUG_INJECT SONAR ${side} INVALID`);send(motor);
    assert.ok(uno.active,'no echo must not reject starting');
    uno.step(600);assert.ok(uno.active,'expired no echo must not stop movement');
    send('STOP',601);
    send(`DEBUG_INJECT SONAR ${side} 0.5`,601);
    send(motor,1102);assert.ok(uno.active,'an expired close reading must not be used to stop');
    uno.step(1103);assert.ok(uno.active);
    send(`DEBUG_INJECT SONAR ${side} 0.5`,1104);
    uno.step(1105);assert.equal(uno.active,null,'a fresh close reading must stop active motion');
    assert.ok(lines.some(line=>line.includes(`SIDE_${side}_OBSTACLE`)));
    send(`DEBUG_INJECT SONAR ${side} 1.0`,1106);send(motor,1106);
    assert.ok(uno.active,'exactly the threshold is allowed');
  }
});

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

test("Uno leaves forward camera gating to the host and each sonar protects its own turn", () => {
  const lines = [];
  const uno = new MockUno(initialWorld(), line => lines.push(line));
  let id = 1;
  const send = (command, now = 0) => {
    const current = id++;
    uno.receiveWire(`CMD ${current} ${command}`, now);
    return current;
  };
  send("CAMERA_FRONT 100");
  send("DEBUG_INJECT SONAR LEFT 0.4");
  send("DEBUG_INJECT SONAR RIGHT 100");
  const leftTurn = send("MOTOR -80 80");
  assert.ok(lines.includes(`ERROR ${leftTurn} SIDE_LEFT_OBSTACLE`));
  const forward = send("MOTOR 80 80");
  assert.ok(lines.includes(`EVENT MOTION_STARTED ${forward} MOTOR`));
  send("STOP");
  const rightTurn = send("MOTOR 80 -80");
  assert.ok(lines.includes(`EVENT MOTION_STARTED ${rightTurn} MOTOR`));
  send("STOP");
  send("CAMERA_FRONT INVALID");
  const blockedForward = send("MOTOR 80 80");
  assert.ok(lines.includes(`EVENT MOTION_STARTED ${blockedForward} MOTOR`));
  send("CAMERA_FRONT 100");
  const staleForward = send("MOTOR 80 80", 501);
  assert.ok(lines.includes(`EVENT MOTION_STARTED ${staleForward} MOTOR`));
  send("CAMERA_FRONT BLOCKED", 502);
  const cameraBlocked = send("MOTOR 80 80", 502);
  assert.ok(lines.includes(`EVENT MOTION_STARTED ${cameraBlocked} MOTOR`));
  send("CAMERA_FRONT CLEAR", 503);
  const cameraClear = send("MOTOR 80 80", 503);
  assert.ok(lines.includes(`EVENT MOTION_STARTED ${cameraClear} MOTOR`));
});

test("both side sonars default to 1 cm and have separate adjustable limits", () => {
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
    line.includes("SAFE_LEFT_CM=1.0 SAFE_RIGHT_CM=1.0")));
  send("DEBUG_INJECT SONAR LEFT 0.9");
  const blockedLeft = send("TURN LEFT 80 100");
  assert.ok(lines.includes(`ERROR ${blockedLeft} SIDE_LEFT_OBSTACLE`));
  send("DEBUG_INJECT SONAR LEFT 1");
  const allowedLeft = send("TURN LEFT 80 100");
  assert.ok(lines.includes(`EVENT MOTION_STARTED ${allowedLeft} TURN_LEFT`));
  send("STOP");
  send("DEBUG_INJECT SONAR RIGHT 0.9");
  const blockedRight = send("TURN RIGHT 80 100");
  assert.ok(lines.includes(`ERROR ${blockedRight} SIDE_RIGHT_OBSTACLE`));
  send("DEBUG_INJECT SONAR RIGHT 1");
  const allowedRight = send("TURN RIGHT 80 100");
  assert.ok(lines.includes(`EVENT MOTION_STARTED ${allowedRight} TURN_RIGHT`));
  send("STOP");
  const setLeft = send("SET_SAFE_DISTANCE LEFT 10");
  assert.ok(lines.includes(`DONE ${setLeft} SAFE_DISTANCE_SET`));
  const blockedAtNine = send("TURN LEFT 80 100");
  assert.ok(lines.includes(`ERROR ${blockedAtNine} SIDE_LEFT_OBSTACLE`));
  const rejectFront = send("SET_SAFE_DISTANCE FRONT 5");
  assert.ok(lines.includes(`ERROR ${rejectFront} UNKNOWN_OR_INVALID_COMMAND`));
});

test("signed rotation can turn left and still obey left sonar", () => {
  const lines = [];
  const uno = new MockUno(initialWorld(), line => lines.push(line));
  uno.receiveWire("CMD 1 SET_GEOMETRY 65 120", 0);
  uno.receiveWire("CMD 2 DEBUG_INJECT SONAR LEFT 0.4", 0);
  uno.receiveWire("CMD 3 ROTATE -90 110", 0);
  assert.ok(lines.includes("ERROR 3 SIDE_LEFT_OBSTACLE"));
  uno.receiveWire("CMD 4 DEBUG_INJECT SONAR LEFT 100", 0);
  uno.receiveWire("CMD 5 ROTATE -90 110", 0);
  assert.ok(lines.includes("EVENT MOTION_STARTED 5 ROTATE ESTIMATED"));
});

test("one continuous contact in the same sensor region emits one press until release", () => {
  const world = initialWorld();
  world.cat = { x: world.robot.x, y: world.robot.y + 30 };
  const lines = [];
  const uno = new MockUno(world, line => lines.push(line));
  uno.step(0);
  uno.step(50);
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

test("moving from a side sensor to an uninstrumented rear region releases contact without waiting for body separation", () => {
  const world = initialWorld();
  const lines = [];
  const uno = new MockUno(world, line => lines.push(line));
  world.cat = { x: world.robot.x, y: world.robot.y + 30 };
  uno.simulateRearTouch = false; // Explicit missing-sensor fixture, not the actual robot configuration.
  uno.step(0); uno.step(50);
  assert.equal(uno.touch.RIGHT, true);
  world.cat = { x: world.robot.x - 30, y: world.robot.y };
  uno.step(100); uno.step(150);
  assert.equal(uno.touch.RIGHT, false);
  assert.equal(uno.touch.REAR, false);
  const events = lines.filter(line => line.includes("SIM_CONTACT TOUCH"));
  world.cat.x -= 150;
  uno.step(200); uno.step(250);
  assert.deepEqual(lines.filter(line => line.includes("SIM_CONTACT TOUCH")), events,
    "leaving the hidden rear region adds no signal");
});

test("the installed rear touch sensor is enabled by default and can be explicitly disabled for a fixture", () => {
  const world = initialWorld();
  world.cat = { x: world.robot.x - 30, y: world.robot.y };
  const uno = new MockUno(world, () => {});
  uno.step(0); uno.step(50);
  assert.equal(uno.touch.REAR, true);
  uno.simulateRearTouch = false;
  uno.step(100); uno.step(150);
  assert.equal(uno.touch.REAR, false);
});

test("rear touch release is observable but further hidden movement produces no additional signal", () => {
  const world = initialWorld();
  world.cat = { x: world.robot.x - 30, y: world.robot.y };
  const lines = [];
  const uno = new MockUno(world, line => lines.push(line));
  uno.step(0); uno.step(50);
  assert.equal(uno.touch.REAR, true);
  world.cat.x -= 40;
  uno.step(100); uno.step(150);
  assert.equal(uno.touch.REAR, false);
  const events = lines.filter(line => line.includes("SIM_CONTACT TOUCH"));
  world.cat.x -= 100;
  uno.step(200); uno.step(250);
  assert.deepEqual(lines.filter(line => line.includes("SIM_CONTACT TOUCH")), events);
});
