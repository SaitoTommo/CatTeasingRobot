import test from "node:test";
import assert from "node:assert/strict";
import { HardwareTelemetry, TelemetryPoller, confirmSceneMotion, encodeSerialCommand,
  decodeSerialFrame, serialCrc8, encoderWheelTravel } from "./hardware-telemetry.mjs";
import { MockUno, initialWorld } from "./core.mjs";

test("motor telemetry separates received command from disabled physical output", () => {
  const model = new HardwareTelemetry();
  model.accept("DATA 1 HW ML 80 0 0 0 0 0", 100);
  const row = model.display("ML", 200);
  assert.match(row.text, /输入指令 80/);
  assert.match(row.text, /引脚驱动 0 · PWM 0\/255/);
  assert.match(row.text, /输出关闭/);
});

test("encoder count, raw input, RPM and physical or virtual source remain distinct", () => {
  const model = new HardwareTelemetry();
  model.accept("DATA 2 HW EL 1 0 4294967295 120.0 P", 100);
  assert.match(model.display("EL", 200).text, /4294967295 脉冲 · 120.0 RPM/);
  assert.match(model.display("EL", 200).text, /LOW · 物理输入/);
  model.accept("DATA 2 HW ER 0 -1 22 80.0 V", 100);
  assert.match(model.display("ER", 200).text, /未采样 · 虚拟脉冲/);
});

test("disabled and invalid physical sonar never becomes a plausible measured distance", () => {
  const model = new HardwareTelemetry();
  model.accept("DATA 3 HW SL 0 0 -1 -1.0 60.0 I", 100);
  const row = model.display("SL", 200);
  assert.match(row.text, /物理测距 无效/);
  assert.match(row.text, /控制读数 60.0 cm · 测试注入/);
  assert.match(row.text, /未采样/);
  model.accept("DATA 3 HW SR 1 1 0 27.0 27.0 P", 100);
  assert.match(model.display("SR", 200).text, /物理测距 27.0 cm/);
});

test("a partial or malformed frame cannot refresh an older sensor row", () => {
  const model = new HardwareTelemetry();
  model.accept("DATA 4 HW EL 1 1 10 0.0 P", 0);
  model.accept("DATA 5 HW STATE 4000 IDLE 1 1 0", 4000);
  assert.equal(model.display("EL", 4000).state, "stale");
  assert.equal(model.display("STATE", 4000).state, "fresh");
  assert.equal(model.accept("DATA 5 HW EL 1 1 broken 0.0 P", 4000), null);
  assert.equal(model.accept("DATA 5 HW EL 1 1 11", 4000), null);
  assert.equal(model.display("EL", 4000).state, "stale");
  model.reset();
  assert.equal(model.display("EL", 4000).state, "waiting");
});

test("touch and auxiliary output rows preserve logical and physical states", () => {
  const model = new HardwareTelemetry();
  model.accept("DATA 6 HW T TOP 1 0 1 P", 100);
  assert.match(model.display("T_TOP", 200).text, /LOW · 按下/);
  model.accept("DATA 6 HW OUT 0 1 0 1 1 1", 100);
  assert.match(model.display("OUT", 200).text, /LED 指令 亮 → LOW（关闭）/);
  assert.match(model.display("OUT", 200).text, /蜂鸣器指令 响 → HIGH（开启）/);
});

test("polling never stacks requests and missing responses trigger backoff", () => {
  const poller = new TelemetryPoller();
  assert.equal(poller.due(0, false), false);
  assert.equal(poller.due(0, true), true);
  poller.begin(7, 0);
  assert.equal(poller.due(750, true), false);
  poller.settle(8, 750);
  assert.equal(poller.due(1000, true), false);
  poller.settle(7, 1000);
  assert.equal(poller.due(1050, true), true);
  poller.begin(9, 1050);
  assert.equal(poller.due(6050, true), false);
  assert.equal(poller.due(8049, true), false);
  assert.equal(poller.due(8050, true), true);
  poller.reset();
  assert.equal(poller.pending, null);
});

test("lost motion start is recovered from the exported log's terminal reply", () => {
  const pose = { x: 410, y: 315, heading: 0 };
  const pending = new Map([[1226, { left: 105 / 255, right: 105 / 255,
    kind: "DRIVE", target: 12, startPose: { ...pose }, manualRobotRevision: 3 }]]);
  // The actual export has a damaged ACK / MOTION_STARTED but an intact DONE.
  const line = "EVENT MOTION_DONE 1226 LEFT_BASE=1500 RIGHT_BASE=1444 ESTIMATED LEFT_PULSES=11 RIGHT_PULSES=11";
  const recovered = confirmSceneMotion(pending, null, 1226, { x: 999, y: 999 }, 4, line);
  assert.deepEqual(recovered.startPose, pose);
  assert.deepEqual(recovered.encoderBase, [1500, 1444]);
  assert.equal(recovered.manualRobotRevision, 3);
  assert.equal(recovered.kind, "DRIVE");
  assert.equal(pending.size, 0);
});

test("ACK recovery and later start preserve one starting pose; stale replies cannot replace motion", () => {
  const pose = { x: 10, y: 20, heading: 0 };
  const pending = new Map([[10, { left: 1, right: 1 }], [9, { left: -1, right: -1 }]]);
  const acked = confirmSceneMotion(pending, null, 10, pose, 1);
  const started = confirmSceneMotion(pending, acked, 10, { x: 50, y: 20 }, 1,
    "EVENT MOTION_STARTED 10 LEFT_BASE=40 RIGHT_BASE=50");
  assert.equal(started, acked);
  assert.deepEqual(started.startPose, pose);
  assert.deepEqual(started.encoderBase, [40, 50]);
  assert.equal(confirmSceneMotion(pending, started, 9, pose, 1), started);
  assert.equal(confirmSceneMotion(pending, started, 100, pose, 1), started);
});

test("short checked commands reduce bytes while preserving complete arguments", () => {
  assert.equal(serialCrc8("123456789"), "F4"); // CRC-8/SMBUS check vector.
  for (const [input, output] of [
    ["CMD 1224 DEBUG_INJECT SONAR LEFT 72.3", "C 1224 DI S L 72.3"],
    ["CMD 1231 CAMERA_FRONT 57.2", "C 1231 CF 57.2"],
    ["CMD 1227 GET_TELEMETRY", "C 1227 GT"],
  ]) {
    const wire = encodeSerialCommand(input);
    assert.equal(decodeSerialFrame(wire), output);
    assert.ok(wire.length < input.length);
  }
  assert.equal(decodeSerialFrame(encodeSerialCommand("CMD 1235 DRIVE 12.0 105")), "C 1235 DRIVE 12.0 105");
});

test("critical short replies expand only when intact; missing digits and checksum are rejected", () => {
  const checked = text => `${text}*${serialCrc8(text)}`;
  assert.equal(decodeSerialFrame(checked("MS 1235 1500 1444")),
    "EVENT MOTION_STARTED 1235 LEFT_BASE=1500 RIGHT_BASE=1444");
  assert.equal(decodeSerialFrame(checked("MD 1235 11 11")),
    "EVENT MOTION_DONE 1235 LEFT_PULSES=11 RIGHT_PULSES=11");
  assert.equal(decodeSerialFrame(checked("MA 1235 STOP_COMMAND 7 7")),
    "EVENT MOTION_ABORTED 1235 STOP_COMMAND LEFT_PULSES=7 RIGHT_PULSES=7");
  assert.equal(decodeSerialFrame(checked("AK 1235")), "ACK 1235");
  assert.equal(decodeSerialFrame(checked("MD 1235 11 11").replace("1235", "123")), null);
  assert.equal(decodeSerialFrame("MS 1235 1500 1444"), null);
  assert.equal(decodeSerialFrame(checked("AK 1235").slice(0, -1)), null);
});

test("card telemetry is throttled to 250 ms without extra motion status polling", () => {
  const poller = new TelemetryPoller();
  poller.begin(1, 0); poller.settle(1, 50);
  assert.equal(poller.due(249, true), false);
  assert.equal(poller.due(250, true), true);
  assert.equal(poller.due(250, false), false);
});

test("movement feedback is checked independently of the slow card telemetry", () => {
  const payload = "MP 42 20 21";
  assert.equal(decodeSerialFrame(`${payload}*${serialCrc8(payload)}`),
    "EVENT MOTION_PROGRESS 42 LEFT_PULSES=20 RIGHT_PULSES=21");
  assert.equal(decodeSerialFrame(`${payload}*${serialCrc8(payload)}`.replace("21", "2")), null);
});

test("staggered encoder edges cannot flip straight heading or rewind a turn", () => {
  const forward = { left: 1, right: 1 };
  const a = encoderWheelTravel(forward, [10, 9], 67.5);
  const b = encoderWheelTravel(forward, [10, 10], 67.5);
  assert.equal(a.leftCm, a.rightCm);
  assert.equal(b.leftCm, b.rightCm);
  assert.ok(b.travelCm > a.travelCm);
  const turn = { left: 1, right: -1, lastPulses: [10, 10] };
  assert.equal(encoderWheelTravel(turn, [9, 10], 67.5), null);
  const c = encoderWheelTravel(turn, [11, 10], 67.5);
  assert.equal(c.leftCm, -c.rightCm);
  assert.ok(c.travelCm > b.travelCm);
});

test("same motor command renews without a stop, restart or encoder baseline reset", () => {
  const lines = [];
  const mock = new MockUno(initialWorld(), line => lines.push(line));
  mock.receiveWire("CMD 1 SESSION_START", 0);
  mock.receiveWire("CMD 5 CAMERA_FRONT 100", 0);
  mock.receiveWire("CMD 2 MOTOR 100 100", 0);
  const original = mock.active;
  mock.step(200);
  mock.receiveWire("CMD 6 CAMERA_FRONT 100", 250);
  mock.receiveWire("CMD 3 MOTOR 100 100", 250);
  assert.equal(mock.active, original);
  assert.equal(mock.active.id, 2);
  mock.step(600);
  assert.equal(mock.active, original);
  assert.equal(lines.filter(line => line.includes("MOTION_STARTED")).length, 1);
  assert.equal(lines.some(line => line.includes("REPLACED")), false);
  mock.receiveWire("CMD 4 STOP", 610);
  assert.equal(mock.active, null);
});

test("renewal ACK cannot replace the current pose and pulse origin", () => {
  const active = { id: 2, startPose: { x: 10, y: 20 }, lastPulses: [20, 20] };
  const pending = new Map([[3, { renewalOf: 2, left: 1, right: 1 }]]);
  assert.equal(confirmSceneMotion(pending, active, 3, { x: 50, y: 20 }, 0), active);
  assert.equal(pending.size, 0);
});
