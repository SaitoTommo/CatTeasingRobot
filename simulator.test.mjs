import test from "node:test";
import assert from "node:assert/strict";
import { touchResponsePlan } from "./decision-system.mjs";

test("every touch area uses one search then response plan", () => {
  const directions = { TOP: 1, LEFT: -1, RIGHT: 1, REAR: 1 };
  for (const [area, direction] of Object.entries(directions)) {
    const signal = touchResponsePlan(area, () => .1);
    const escape = touchResponsePlan(area, () => .9);
    assert.equal(signal.direction, direction);
    assert.equal(escape.direction, direction);
    assert.equal(signal.sweepLimitDeg, 360);
    assert.equal(escape.sweepLimitDeg, 360);
    assert.equal(signal.response, "STAY_SIGNAL");
    assert.equal(escape.response, "FAST_STRAIGHT_ESCAPE");
    assert.equal(touchResponsePlan(area, () => .1).response, "STAY_SIGNAL");
    assert.equal(touchResponsePlan(area, () => .9).response, "FAST_STRAIGHT_ESCAPE");
  }
});

test("patrol continues until touch, then top signals after facing and left escapes before refacing", async () => {
  const elements = new Map();
  const archivedMessages = [];
  const drawing = new Proxy({}, { get: () => () => {} });
  class Element {
    constructor(id = "") {
      this.id = id;
      this.value = ({
        catScenarioBehavior: "STILL", attackArea: "TOP",
        catSpeedCmS: "20", attackHoldMs: "700",
        attackIntervalMs: "20000",
        tagStopCm: "20", wheelDiameterMm: "65", trackWidthMm: "120",
      })[id] ?? "";
      this.checked = false;
      this.children = [];
      this.listeners = new Map();
      this.classList = { toggle() {} };
      this.hidden = false;
      this.textContent = "";
      this.attributes = new Map();
    }
    get selectedOptions() { return [{ textContent: this.value }]; }
    get firstChild() { return this.children[0]; }
    get scrollHeight() { return this.children.length; }
    getContext() { return drawing; }
    addEventListener(type, listener) {
      const listeners = this.listeners.get(type) ?? [];
      listeners.push(listener);
      this.listeners.set(type, listeners);
    }
    dispatch(type, event = {}) {
      for (const listener of this.listeners.get(type) ?? []) listener({ target: this, preventDefault() {}, ...event });
    }
    click() { this.dispatch("click"); }
    setAttribute(name, value) { this.attributes.set(name, String(value)); }
    getAttribute(name) { return this.attributes.get(name) ?? null; }
    append(...children) {
      for (const child of children) { child.parent = this; this.children.push(child); }
    }
    remove() {
      if (this.parent?.id === "log") archivedMessages.push(this.children[2]?.textContent);
      const index = this.parent?.children.indexOf(this) ?? -1;
      if (index >= 0) this.parent.children.splice(index, 1);
    }
    replaceChildren() { this.children.length = 0; }
    getBoundingClientRect() { return { left: 0, top: 0, width: 1000, height: 600 }; }
    setPointerCapture() {}
  }
  const topTouchButton = new Element("topTouch");
  topTouchButton.dataset = { touch: "TOP" };
  globalThis.document = {
    visibilityState: "visible",
    getElementById(id) {
      if (!elements.has(id)) elements.set(id, new Element(id));
      return elements.get(id);
    },
    createElement() { return new Element(); },
    querySelectorAll(selector) {
      if (selector === "[data-touch]") return [topTouchButton];
      return [];
    },
    addEventListener() {},
  };
  let nextFrame = null;
  globalThis.requestAnimationFrame = callback => { nextFrame = callback; };
  let randomValue = .1;
  const previousRandom = Math.random;
  Math.random = () => randomValue;
  await import("./app.mjs");
  const messages = () => [...archivedMessages, ...elements.get("log").children.map(row => row.children[2].textContent)];
  elements.get("touchSignalPercent").value = "0";
  elements.get("touchSlowEscapePercent").value = "100";
  elements.get("touchFastEscapePercent").value = "0";
  elements.get("catFollowProbePercent").value = "0";
  elements.get("catFollowObservePercent").value = "30";
  elements.get("catFollowPatrolPercent").value = "70";
  elements.get("endingInactivitySeconds").value = "8";
  elements.get("applyEndingPolicy").click();
  elements.get("applyResponsePolicy").click();
  assert.match(elements.get("responsePolicyStatus").textContent, /触碰后声光／慢逃／快逃 0\/100\/0%/);
  assert.match(elements.get("endingPolicyStatus").textContent, /8 秒无新触碰后结束/);
  assert.ok(messages().some(message => message.includes("RESPONSE_POLICY_SET group=cat")));
  elements.get("touchSignalPercent").value = "20";
  elements.get("touchSlowEscapePercent").value = "40";
  elements.get("touchFastEscapePercent").value = "40";
  elements.get("catFollowProbePercent").value = "5";
  elements.get("catFollowObservePercent").value = "35";
  elements.get("catFollowPatrolPercent").value = "60";
  elements.get("endingInactivitySeconds").value = "12";
  elements.get("endingAfterRounds").value = "2";
  elements.get("endingPatrolPercent").value = "0";
  elements.get("endingSilentPercent").value = "0";
  elements.get("endingSignalPercent").value = "100";
  elements.get("captureWaitMaxSeconds").value = "8";
  elements.get("applyEndingPolicy").click();
  elements.get("applyResponsePolicy").click();
  elements.get("startCatMotion").click();
  assert.ok(!messages().some(message => message.includes("DECISION_START") || message.includes("BEHAVIOR PATROL")),
    "cat movement input must not start patrol or choose a robot behavior");
  elements.get("startPatrol").click();
  assert.ok(messages().some(message => message.includes("BEHAVIOR PATROL source=ESP32_AUTO")),
    "the patrol button should launch patrol immediately, without waiting for an animation frame");
  assert.equal(messages().filter(message => /^CMD \d+ SESSION_START$/.test(message)).length, 1,
    "starting patrol should open exactly one Uno session");
  const startedAt = performance.now();
  let switchIndex = -1;
  while (performance.now() - startedAt < 25000 &&
      !messages().some(message => message.includes("ATTACK_RESPONSE stationary=true light=FLASH sound=BEEP"))) {
    if (switchIndex < 0 && performance.now() - startedAt > 2500) {
      switchIndex = messages().length;
      elements.get("catScenarioBehavior").value = "ATTACK";
      elements.get("catScenarioBehavior").dispatch("change");
    }
    const frame = nextFrame;
    assert.ok(frame, "the simulator should schedule another frame");
    nextFrame = null;
    frame(performance.now());
    await new Promise(resolve => setTimeout(resolve, 16));
  }
  const log = messages();
  const attack = log.findIndex(message => message.includes("SIM_CAT ATTACK_BEGIN area=TOP"));
  const response = log.findIndex(message => message.includes("TOP_LEVEL_DECISION action=ATTACK_RESPONSE"));
  const search = log.findIndex(message => message.includes("SEARCH_CAT_START camera=SIM_CAMERA sweep_limit_deg=360"));
  const motor = log.findIndex((message, index) => index > search && /^CMD \d+ MOTOR /.test(message));
  const face = log.findIndex(message => message.includes("FACE_CAT_RESULT action=ATTACK_RESPONSE status=READY"));
  const signal = log.findIndex(message => message.includes("ATTACK_RESPONSE stationary=true light=FLASH sound=BEEP"));
  assert.ok(switchIndex > 0, "the attack scenario should be selected during patrol");
  assert.ok(log.slice(switchIndex, attack).some(message => /PATROL_|^CMD \d+ (DRIVE|MOTOR) /.test(message)),
    "patrol should keep moving after the attack scenario is selected and before contact");
  assert.ok(log.some(message => message.includes("PATROL_APPROACH")), `patrol should advance while the cat approaches: ${log.slice(-30).join(" | ")}`);
  assert.ok(log.some(message => message.includes("PATROL_SCAN_START tag=B sweep_limit_deg=360")),
    `reaching A should start a camera-guided scan for B: ${log.filter(message => /PATROL_|OBSTACLE_NEAR|BEHAVIOR_ABORTED|SIM_CAT ATTACK_BEGIN|TOP_LEVEL_DECISION/.test(message)).slice(0, 45).join(" | ")}`);
  assert.ok(log.some(message => message.includes("PATROL_SCAN_STOP tag=B reason=TAG_DETECTED")),
    "the scan should stop as soon as B appears");
  assert.ok(attack > 0, "the cat should reach the top touch sensor");
  assert.ok(response > attack, "the top touch should select the search behavior");
  assert.ok(log.some(message => message.includes("ATTACK_SEARCH_START area=TOP direction=RIGHT sweep_limit_deg=360")));
  assert.ok(search > response, "search should begin after the touch releases");
  assert.ok(motor > search, "the robot should actually rotate to search");
  assert.ok(face > motor, `the search should finish with a face-cat result: ${log.filter(message => /SEARCH_CAT|SCAN_WAIT|FACE_CAT|ALIGN_CAT|OBSTACLE_NEAR|BEHAVIOR_ABORTED|TOP_LEVEL_DECISION/.test(message)).slice(-55).join(" | ")}`);
  assert.ok(signal > face, "the sound and light response should start only after the cat is found and faced");
  assert.ok(!log.some(message => /^CMD \d+ ROTATE 90 /.test(message)), "patrol should never use the old fixed turn");

  const firstSignalEnd = performance.now() + 3000;
  while (performance.now() < firstSignalEnd && !messages().some(message => /^CMD \d+ LED OFF$/.test(message))) {
    const frame = nextFrame;
    assert.ok(frame);
    nextFrame = null;
    frame(performance.now());
    await new Promise(resolve => setTimeout(resolve, 16));
  }
  elements.get("attackArea").value = "LEFT";
  elements.get("attackArea").dispatch("change");
  randomValue = .9;
  const leftStart = messages().length;
  const leftDeadline = performance.now() + 35000;
  while (performance.now() < leftDeadline &&
      messages().slice(leftStart).filter(message => message.includes("FACE_CAT_RESULT action=ATTACK_RESPONSE status=READY")).length < 2 &&
      !messages().slice(leftStart).some(message => message.includes("BEHAVIOR_ABORTED ATTACK_RESPONSE"))) {
    const frame = nextFrame;
    assert.ok(frame);
    nextFrame = null;
    frame(performance.now());
    await new Promise(resolve => setTimeout(resolve, 16));
  }
  const leftLog = messages().slice(leftStart);
  const leftTouch = leftLog.findIndex(message => message.includes("UNO_TO_ESP32 TOUCH area=LEFT phase=PRESS"));
  const leftSearch = leftLog.findIndex(message => message.includes("ATTACK_SEARCH_START area=LEFT direction=LEFT sweep_limit_deg=360"));
  const leftFace = leftLog.findIndex((message, index) => index > leftSearch && message.includes("FACE_CAT_RESULT action=ATTACK_RESPONSE status=READY"));
  const leftMotor = leftLog.findIndex((message, index) => index > leftSearch && index < leftFace && /^CMD \d+ MOTOR -\d+ \d+$/.test(message));
  const escapeTurn = leftLog.findIndex(message => /^CMD \d+ ROTATE 180 110$/.test(message));
  const escapeDrive = leftLog.findIndex(message => /^CMD \d+ DRIVE 26 210$/.test(message));
  const reface = leftLog.findIndex(message => message.includes("ATTACK_RESPONSE_PHASE reface_after_escape"));
  const facedAgain = leftLog.findIndex((message, index) => index > reface && message.includes("FACE_CAT_RESULT action=ATTACK_RESPONSE status=READY"));
  assert.ok(leftTouch >= 0 && leftSearch > leftTouch, `left touch should start a leftward cat search: ${leftLog.filter(message => /ATTACK_|SEARCH_CAT|FACE_CAT|BEHAVIOR_ABORTED|MOTOR -75/.test(message)).slice(-45).join(" | ")}`);
  assert.ok(leftMotor > leftSearch, "the left touch should physically command a left turn");
  assert.ok(leftFace > leftMotor, `the cat should be faced before escape: ${leftLog.filter(message => /ATTACK_|SEARCH_CAT|FACE_CAT|BEHAVIOR_ABORTED|MOTOR -75|ROTATE 180|DRIVE 26/.test(message)).slice(-55).join(" | ")}`);
  assert.ok(escapeTurn > leftFace && escapeDrive > escapeTurn, "escape should turn away from the faced cat then drive");
  assert.ok(reface > escapeDrive, "escape should finish by searching for the cat again");
  assert.ok(facedAgain > reface, `the robot should face the cat again after escape: ${leftLog.filter(message => /ATTACK_|SEARCH_CAT|FACE_CAT|BEHAVIOR_ABORTED|MOTOR |ROTATE 180|DRIVE 26/.test(message)).slice(-45).join(" | ")}`);

  let interruptedTarget = "A";
  for (const message of log.slice(0, response)) {
    const reached = /PATROL_TAG_REACHED tag=[A-D] .* next_tag=([A-D])/.exec(message);
    if (reached) interruptedTarget = reached[1];
  }
  elements.get("catScenarioBehavior").value = "RETREAT";
  elements.get("catScenarioBehavior").dispatch("change");
  const endingStart = messages().length;
  const endingDeadline = performance.now() + 20000;
  while (performance.now() < endingDeadline &&
      !messages().slice(endingStart).some(message => message.includes("PATROL_RESUME target_tag="))) {
    const frame = nextFrame;
    assert.ok(frame);
    nextFrame = null;
    frame(performance.now());
    await new Promise(resolve => setTimeout(resolve, 16));
  }
  const resumedLog = messages().slice(endingStart);
  const endingLog = messages().slice(leftStart);
  const outcomeStart = endingLog.findIndex(message => message.includes("ENDING_OUTCOME_START outcome=PREY_SIGNAL stationary=true"));
  const outcomeEnd = endingLog.findIndex((message, index) => index > outcomeStart &&
    message.includes("ENDING_OUTCOME_DONE outcome=PREY_SIGNAL"));
  assert.ok(outcomeStart >= 0 && outcomeEnd > outcomeStart,
    `two touch rounds should trigger a stationary ending cue: ${endingLog.filter(message => /ESCAPE_|ENDING_OUTCOME|SESSION_/.test(message)).slice(-30).join(" | ")}`);
  assert.ok(!endingLog.slice(outcomeStart, outcomeEnd).some(message =>
    /SEARCH_CAT_START|ALIGN_CAT_START|^CMD \d+ (DRIVE|ROTATE|MOTOR) /.test(message)),
    "the ending cue must not search, turn, or drive");
  const stopConfirmation = resumedLog.findIndex(message => /DONE \d+ SESSION_STOPPED/.test(message));
  const resume = resumedLog.findIndex(message => message.includes("PATROL_RESUME target_tag="));
  assert.ok(stopConfirmation >= 0 && resume > stopConfirmation,
    `patrol should resume only after Uno confirms SESSION_STOPPED: ${resumedLog.slice(-45).join(" | ")}`);
  assert.ok(resumedLog[resume].includes(`target_tag=${interruptedTarget}`),
    `patrol must retain its interrupted target ${interruptedTarget}: ${resumedLog[resume]}`);
  assert.ok(resumedLog.slice(resume).some(message => message.includes("BEHAVIOR PATROL source=ESP32_AUTO")),
    "automatic patrol should restart after the interaction ends");

  elements.get("stopPatrol").click();
  elements.get("stopCatMotion").click();
  elements.get("resetScene").click();
  elements.get("endingAfterRounds").value = "3";
  elements.get("endingPatrolPercent").value = "100";
  elements.get("endingSilentPercent").value = "0";
  elements.get("endingSignalPercent").value = "0";
  elements.get("captureWaitMaxSeconds").value = "15";
  elements.get("applyEndingPolicy").click();
  const arena = elements.get("arena");
  arena.dispatch("pointerdown", { clientX: 675, clientY: 255, pointerId: 1 });
  arena.dispatch("pointermove", { clientX: 982, clientY: 582, pointerId: 1 });
  arena.dispatch("pointerup", { pointerId: 1 });
  const standaloneStart = messages().length;
  randomValue = .1;
  topTouchButton.click();
  const sweepDeadline = performance.now() + 13000;
  while (performance.now() < sweepDeadline &&
      !messages().slice(standaloneStart).some(message => message.includes("SEARCH_CAT_STOP reason=CAT_DETECTED"))) {
    const frame = nextFrame;
    assert.ok(frame);
    nextFrame = null;
    frame(performance.now());
    await new Promise(resolve => setTimeout(resolve, 16));
  }
  await new Promise(resolve => setTimeout(resolve, 100));
  const sweepLog = messages().slice(standaloneStart);
  assert.equal(sweepLog.filter(message => message.includes("SEARCH_CAT_START camera=")).length, 1,
    "a standalone search must not start a second scan during behavior cleanup");
  assert.ok(sweepLog.some(message => message.includes("SEARCH_CAT_STOP reason=CAT_DETECTED")),
    `the distant cat should be found within the simulated field of view: ${sweepLog.filter(message => /SEARCH_CAT|BEHAVIOR_ABORTED|OBSTACLE_NEAR/.test(message)).slice(-25).join(" | ")}`);

  elements.get("resetScene").click();
  elements.get("catScenarioBehavior").value = "STILL";
  elements.get("catScenarioBehavior").dispatch("change");
  arena.dispatch("pointerdown", { clientX: 410, clientY: 315, pointerId: 2 });
  arena.dispatch("pointermove", { clientX: 410, clientY: 565, pointerId: 2 });
  arena.dispatch("pointerup", { pointerId: 2 });
  const blockedRightStart = messages().length;
  elements.get("startPatrol").click();
  const reverseDeadline = performance.now() + 3000;
  while (performance.now() < reverseDeadline &&
      !messages().slice(blockedRightStart).some(message => /^CMD \d+ MOTOR -75 75$/.test(message))) {
    const frame = nextFrame;
    assert.ok(frame);
    nextFrame = null;
    frame(performance.now());
    await new Promise(resolve => setTimeout(resolve, 16));
  }
  const blockedRightLog = messages().slice(blockedRightStart);
  assert.ok(blockedRightLog.some(message => message.includes("SCAN_REVERSE reason=SIDE_RIGHT_OBSTACLE_OR_SENSOR_INVALID direction=LEFT")),
    `patrol should choose the clear left side: ${blockedRightLog.slice(-30).join(" | ")}`);
  assert.ok(blockedRightLog.some(message => /^CMD \d+ MOTOR -75 75$/.test(message)),
    "patrol should issue a left turn after rejecting the blocked right side");
  assert.ok(!blockedRightLog.some(message => /^CMD \d+ MOTOR 75 -75$/.test(message)),
    "patrol should not repeatedly request the blocked right turn");
  elements.get("stopPatrol").click();

  elements.get("resetScene").click();
  randomValue = .1;
  const injectedStart = messages().length;
  topTouchButton.click();
  const injectedDeadline = performance.now() + 8000;
  while (performance.now() < injectedDeadline &&
      !messages().slice(injectedStart).some(message => message.includes("ATTACK_RESPONSE stationary=true light=FLASH sound=BEEP"))) {
    const frame = nextFrame;
    assert.ok(frame);
    nextFrame = null;
    frame(performance.now());
    await new Promise(resolve => setTimeout(resolve, 16));
  }
  const injectedLog = messages().slice(injectedStart);
  assert.ok(injectedLog.some(message => message.includes("UNO_TO_ESP32 TOUCH area=TOP phase=PRESS")));
  assert.ok(injectedLog.some(message => message.includes("UNO_TO_ESP32 TOUCH area=TOP phase=RELEASE")));
  assert.ok(injectedLog.some(message => message.includes("TOP_LEVEL_DECISION action=ATTACK_RESPONSE")),
    "touch injection must enter the same decision path without starting a cat scene");
  assert.ok(injectedLog.some(message => message.includes("ATTACK_RESPONSE stationary=true light=FLASH sound=BEEP")),
    `touch injection should produce a response: ${injectedLog.filter(message => /ATTACK_|FACE_CAT|SEARCH_CAT|BEHAVIOR_ABORTED/.test(message)).slice(-35).join(" | ")}`);

  const sessionEndDeadline = performance.now() + 8000;
  while (performance.now() < sessionEndDeadline &&
      !messages().slice(injectedStart).some(message => message.includes("SESSION_ENDED duration_s="))) {
    const frame = nextFrame;
    assert.ok(frame);
    nextFrame = null;
    frame(performance.now());
    await new Promise(resolve => setTimeout(resolve, 16));
  }
  assert.ok(messages().slice(injectedStart).some(message => message.includes("SESSION_ENDED duration_s=")),
    "a standalone touch response should complete the interaction");
  elements.get("resetScene").click();
  elements.get("endingInactivitySeconds").value = "5";
  elements.get("applyEndingPolicy").click();
  elements.get("startPatrol").click();
  const patrolTouchStart = messages().length;
  topTouchButton.click();
  const patrolSignalDeadline = performance.now() + 8000;
  while (performance.now() < patrolSignalDeadline &&
      !messages().slice(patrolTouchStart).some(message => message.includes("ATTACK_RESPONSE stationary=true light=FLASH sound=BEEP"))) {
    const frame = nextFrame;
    assert.ok(frame);
    nextFrame = null;
    frame(performance.now());
    await new Promise(resolve => setTimeout(resolve, 16));
  }
  arena.dispatch("pointerdown", { clientX: 675, clientY: 255, pointerId: 3 });
  arena.dispatch("pointermove", { clientX: 982, clientY: 582, pointerId: 3 });
  arena.dispatch("pointerup", { pointerId: 3 });
  const patrolResumeDeadline = performance.now() + 10000;
  while (performance.now() < patrolResumeDeadline &&
      !messages().slice(patrolTouchStart).some(message => message.includes("PATROL_RESUME target_tag=A"))) {
    const frame = nextFrame;
    assert.ok(frame);
    nextFrame = null;
    frame(performance.now());
    await new Promise(resolve => setTimeout(resolve, 16));
  }
  const patrolTouchLog = messages().slice(patrolTouchStart);
  assert.ok(patrolTouchLog.some(message => message.includes("PATROL_RESUME target_tag=A")),
    `sensor injection should resume interrupted patrol after the cat leaves: ${patrolTouchLog.filter(message => /ATTACK_|SESSION_|PATROL_RESUME|BEHAVIOR_ABORTED/.test(message)).slice(-30).join(" | ")}`);
  elements.get("stopMotion").click();

  elements.get("resetScene").click();
  arena.dispatch("pointerdown", { clientX: 675, clientY: 255, pointerId: 4 });
  arena.dispatch("pointermove", { clientX: 500, clientY: 315, pointerId: 4 });
  arena.dispatch("pointerup", { pointerId: 4 });
  const detourStart = messages().length;
  elements.get("startPatrol").click();
  const detourDeadline = performance.now() + 20000;
  while (performance.now() < detourDeadline &&
      !messages().slice(detourStart).some(message => message.includes("PATROL_DETOUR_DONE tag=A"))) {
    const frame = nextFrame;
    assert.ok(frame);
    nextFrame = null;
    frame(performance.now());
    await new Promise(resolve => setTimeout(resolve, 16));
  }
  const detourLog = messages().slice(detourStart);
  assert.ok(detourLog.some(message => message.includes("PATROL_DETOUR_START tag=A around=CAT")),
    `a stationary cat should trigger a detour: ${detourLog.slice(-30).join(" | ")}`);
  assert.ok(detourLog.some(message => message.includes("PATROL_DETOUR_DONE tag=A")),
    `patrol should go around the stationary cat: ${detourLog.filter(message => /PATROL_DETOUR|BEHAVIOR_ABORTED|OBSTACLE_NEAR/.test(message)).slice(-35).join(" | ")}`);
  const reachedDeadline = performance.now() + 8000;
  while (performance.now() < reachedDeadline &&
      !messages().slice(detourStart).some(message => message.includes("PATROL_TAG_REACHED tag=A"))) {
    const frame = nextFrame;
    assert.ok(frame);
    nextFrame = null;
    frame(performance.now());
    await new Promise(resolve => setTimeout(resolve, 16));
  }
  assert.ok(messages().slice(detourStart).some(message => message.includes("PATROL_TAG_REACHED tag=A")),
    `detour should resume the same tag target: ${messages().slice(detourStart).filter(message => /PATROL_|BEHAVIOR_ABORTED|OBSTACLE_NEAR/.test(message)).slice(-35).join(" | ")}`);
  elements.get("stopPatrol").click();

  elements.get("resetScene").click();
  elements.get("touchSignalPercent").value = "0";
  elements.get("touchSlowEscapePercent").value = "100";
  elements.get("touchFastEscapePercent").value = "0";
  elements.get("applyResponsePolicy").click();
  const slowEscapeStart = messages().length;
  topTouchButton.click();
  const slowEscapeDeadline = performance.now() + 12000;
  while (performance.now() < slowEscapeDeadline &&
      !messages().slice(slowEscapeStart).some(message => message.includes("ESCAPE_PHASE pace=SLOW"))) {
    const frame = nextFrame;
    assert.ok(frame);
    nextFrame = null;
    frame(performance.now());
    await new Promise(resolve => setTimeout(resolve, 16));
  }
  assert.ok(messages().slice(slowEscapeStart).some(message => message.includes("ESCAPE_PHASE pace=SLOW straight_distance_cm=26 pwm=95")),
    `the slow setting should change the executed escape speed: ${messages().slice(slowEscapeStart).filter(message => /ATTACK_|ESCAPE_PHASE|BEHAVIOR_ABORTED/.test(message)).slice(-25).join(" | ")}`);
  elements.get("stopMotion").click();

  elements.get("resetScene").click();
  arena.dispatch("pointerdown", { clientX: 675, clientY: 255, pointerId: 5 });
  arena.dispatch("pointermove", { clientX: 100, clientY: 315, pointerId: 5 });
  arena.dispatch("pointerup", { pointerId: 5 });
  elements.get("searchTimeoutMs").value = "1000";
  elements.get("applyAdvancedPolicy").click();
  elements.get("startPatrol").click();
  const unseenTouchStart = messages().length;
  topTouchButton.click();
  const unseenTouchDeadline = performance.now() + 8000;
  while (performance.now() < unseenTouchDeadline &&
      !messages().slice(unseenTouchStart).some(message => message.includes("PATROL_RESUME target_tag=A"))) {
    const frame = nextFrame;
    assert.ok(frame);
    nextFrame = null;
    frame(performance.now());
    await new Promise(resolve => setTimeout(resolve, 16));
  }
  const unseenTouchLog = messages().slice(unseenTouchStart);
  const fallback = unseenTouchLog.findIndex(message => message.includes("CAT_TOUCH_FALLBACK"));
  const stopped = unseenTouchLog.findIndex((message, index) => index > fallback && /DONE \d+ SESSION_STOPPED/.test(message));
  const resumed = unseenTouchLog.findIndex((message, index) => index > stopped && message.includes("PATROL_RESUME target_tag=A"));
  const reopened = unseenTouchLog.findIndex((message, index) => index > resumed && /DONE \d+ SESSION_STARTED/.test(message));
  assert.ok(unseenTouchLog.some(message => message.includes("SEARCH_CAT_STOP reason=TIMEOUT")),
    `a human touch with no visible cat should exhaust the bounded search: ${unseenTouchLog.filter(message => /SEARCH_CAT|FACE_CAT|CAT_TOUCH_FALLBACK|SESSION_/.test(message)).slice(-35).join(" | ")}`);
  assert.ok(fallback >= 0 && stopped > fallback && resumed > stopped && reopened > resumed,
    `search failure should stop the interaction and reopen Uno motion before patrol: ${unseenTouchLog.filter(message => /CAT_TOUCH_FALLBACK|SESSION_|PATROL_RESUME/.test(message)).slice(-35).join(" | ")}`);
  assert.ok(!unseenTouchLog.some(message => /ENDING_OUTCOME_START outcome=PREY_/.test(message)),
    "a failed search must not draw a prey-like ending");
  elements.get("stopPatrol").click();
  Math.random = previousRandom;
});
