import test from "node:test";
import assert from "node:assert/strict";
import {
  chooseCatInitiatedFollowUp, chooseEndingOutcome, chooseRobotInitiatedAction, decideScanProgress,
  Esp32DecisionSystem, touchResponsePlan, validateResponsePolicy,
} from "./decision-system.mjs";

test("search stops at one full turn", () => {
  assert.equal(decideScanProgress({ detected: false, sweptDeg: 359.9,
    limitDeg: 360, timedOut: false }), "CONTINUE");
  assert.equal(decideScanProgress({ detected: false, sweptDeg: 360,
    limitDeg: 360, timedOut: false }), "FULL_SWEEP");
});

test("cat touch has its own three response probabilities", () => {
  assert.equal(touchResponsePlan("TOP", () => .1).response, "STAY_SIGNAL");
  assert.equal(touchResponsePlan("LEFT", () => .3).response, "SLOW_STRAIGHT_ESCAPE");
  assert.equal(touchResponsePlan("RIGHT", () => .9).response, "FAST_STRAIGHT_ESCAPE");
  assert.equal(touchResponsePlan("LEFT", () => .3).direction, -1);
  assert.equal(touchResponsePlan("LEFT", () => .3).sweepLimitDeg, 360);
});

test("cat-initiated follow-up is independent from robot search probabilities", () => {
  const policy = validateResponsePolicy({
    catFollowProbePercent: 0, catFollowObservePercent: 100, catFollowPatrolPercent: 0,
    robotFarProbePercent: 100, robotFarApproachPercent: 0,
  });
  assert.equal(chooseCatInitiatedFollowUp(() => .1, policy).action, "OBSERVE");
  assert.equal(chooseRobotInitiatedAction({ camera: { detected: true, distanceCm: 55 },
    random: () => .1, policy }).action, "PROBE_APPROACH");
  assert.equal(chooseCatInitiatedFollowUp(() => .9, validateResponsePolicy({
    catFollowProbePercent: 0, catFollowObservePercent: 0, catFollowPatrolPercent: 100,
  })).action, "END_SESSION");
});

test("cat follow-up runs once and inactivity returns to patrol", () => {
  const system = new Esp32DecisionSystem(() => .01);
  system.onTouchPress("TOP", 1000);
  const base = { phase: "INTERACTION", autoBusy: false, nextDecisionAt: 0,
    obstacleBlockedUntil: 0, sessionElapsedMs: 1000,
    camera: { detected: true, distanceCm: 55 } };
  assert.equal(system.tick({ ...base, now: 2000 }).action, "PROBE_APPROACH");
  assert.equal(system.tick({ ...base, now: 5000 }), null);
  assert.equal(system.tick({ ...base, now: 13000 }).action, "END_SESSION");
  system.onTouchPress("TOP", 14000);
  assert.equal(system.tick({ ...base, now: 15000 }).action, "OBSERVE",
    "cat probe cooldown suppresses a second probe");
});

test("robot-initiated far and near probabilities and cooldown remain separate", () => {
  const system = new Esp32DecisionSystem(() => .1);
  const camera = { detected: true, distanceCm: 55 };
  system.onMoodAlert("CAMERA_NEGATIVE", { phase: "PATROL", camera, now: 1000 });
  const base = { phase: "INTERACTION", autoBusy: false, nextDecisionAt: 0,
    obstacleBlockedUntil: 0, sessionElapsedMs: 1000, camera };
  assert.equal(system.tick({ ...base, now: 2000 }).action, "SLOW_APPROACH");
  assert.equal(system.tick({ ...base, now: 16000 }).action, "PROBE_APPROACH");
  system.setResponsePolicy({ robotFarProbePercent: 0, robotFarApproachPercent: 100 });
  assert.equal(system.tick({ ...base, now: 17000 }).action, "SLOW_APPROACH");
  assert.equal(system.tick({ ...base, now: 18000,
    camera: { detected: true, distanceCm: 20 } }).action, "STAY_SIGNAL");
});

test("old browser settings migrate and each probability group sums to 100", () => {
  const legacy = validateResponsePolicy({ signalPercent: 20, slowEscapePercent: 75,
    probePercent: 10, nearObservePercent: 50 });
  assert.equal(legacy.touchSignalPercent, 20);
  assert.equal(legacy.touchSlowEscapePercent, 60);
  assert.equal(legacy.touchFastEscapePercent, 20);
  assert.equal(legacy.robotFarProbePercent, 10);
  assert.equal(legacy.robotNearObservePercent, 40);
  assert.equal(legacy.initialLookSeconds, 5);
  assert.equal(legacy.touchDebounceMs, 1000);
  assert.equal(legacy.postEscapeWaitSeconds, 10);
  assert.equal(validateResponsePolicy({ initialLookSeconds: 8,
    postEscapeWaitSeconds: 20 }).postEscapeWaitSeconds, 20);
  assert.throws(() => validateResponsePolicy({ catFollowProbePercent: 20 }), /sum_/);
  assert.throws(() => validateResponsePolicy({ farCatThresholdCm: 69, catLeaveDistanceCm: 50 }),
    /distance_order/);
});

test("ending outcome has its own three probabilities", () => {
  const policy = validateResponsePolicy({ endingPatrolPercent: 40,
    endingSilentPercent: 30, endingSignalPercent: 30 });
  assert.equal(chooseEndingOutcome(() => .1, policy).outcome, "CONTINUE_PATROL");
  assert.equal(chooseEndingOutcome(() => .5, policy).outcome, "PREY_STILL");
  assert.equal(chooseEndingOutcome(() => .9, policy).outcome, "PREY_SIGNAL");
  assert.throws(() => validateResponsePolicy({ endingSignalPercent: 40 }), /sum_endingPatrolPercent/);
  assert.throws(() => validateResponsePolicy({ endingAfterRounds: 0,
    endingInactivitySeconds: 0 }), /ending_trigger/);
});

test("ending triggers by rounds or inactivity without requiring an escape", () => {
  const base = { phase: "INTERACTION", autoBusy: false, nextDecisionAt: 0,
    obstacleBlockedUntil: 0, sessionElapsedMs: 1000,
    camera: { detected: true, distanceCm: 55 } };
  const rounds = new Esp32DecisionSystem(() => .9, validateResponsePolicy({
    endingAfterRounds: 2, endingInactivitySeconds: 0,
  }));
  rounds.onTouchPress("TOP", 1000);
  rounds.onTouchPress("TOP", 2000);
  const roundEnding = rounds.tick({ ...base, now: 2500 });
  assert.equal(roundEnding.action, "END_SESSION");
  assert.equal(roundEnding.endingOutcome, "PREY_SIGNAL");
  assert.match(roundEnding.reason, /trigger=rounds/);
  rounds.finishInteraction();
  assert.equal(rounds.catInteractionRounds, 0);

  const inactivity = new Esp32DecisionSystem(() => .5, validateResponsePolicy({
    endingAfterRounds: 0, endingInactivitySeconds: 4,
  }));
  inactivity.onTouchPress("TOP", 1000);
  assert.equal(inactivity.tick({ ...base, nextDecisionAt: 6000, now: 4999 }), null);
  const idleEnding = inactivity.tick({ ...base, nextDecisionAt: 6000, now: 5000 });
  assert.equal(idleEnding.endingOutcome, "PREY_STILL");
  assert.match(idleEnding.reason, /trigger=inactivity/);
});
