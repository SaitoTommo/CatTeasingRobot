import test from "node:test";
import assert from "node:assert/strict";
import {
  chooseCatInitiatedFollowUp, chooseEndingOutcome, chooseRobotInitiatedAction, decideScanProgress,
  Esp32DecisionSystem, EXPLORATION_SOUND_LIGHT_PATTERNS, touchResponsePlan, validateResponsePolicy,
} from "./decision-system.mjs";

test("search stops at one full turn", () => {
  assert.equal(decideScanProgress({ detected: false, sweptDeg: 359.9,
    limitDeg: 360, timedOut: false }), "CONTINUE");
  assert.equal(decideScanProgress({ detected: false, sweptDeg: 360,
    limitDeg: 360, timedOut: false }), "FULL_SWEEP");
});

test("hunting branch draws only prey, slow escape, or fast escape", () => {
  assert.equal(touchResponsePlan("TOP", () => .1).response, "PREY_STILL");
  assert.equal(touchResponsePlan("LEFT", () => .5).response, "SLOW_STRAIGHT_ESCAPE");
  assert.equal(touchResponsePlan("RIGHT", () => .9).response, "FAST_STRAIGHT_ESCAPE");
  assert.equal(touchResponsePlan("LEFT", () => .5).direction, -1);
  assert.equal(touchResponsePlan("LEFT", () => .3).sweepLimitDeg, 360);
  assert.equal(touchResponsePlan("TOP", () => .1).escapeDraw, null);
  let draws = [.9, .1];
  const preyWithoutFeedback = touchResponsePlan("TOP", () => draws.shift());
  assert.equal(preyWithoutFeedback.response, "PREY_STILL");
  assert.equal(preyWithoutFeedback.contactFeedbackDuringTouch, false);
  draws = [.1, .9];
  const fastWithFeedback = touchResponsePlan("TOP", () => draws.shift());
  assert.equal(fastWithFeedback.response, "FAST_STRAIGHT_ESCAPE");
  assert.equal(fastWithFeedback.contactFeedbackDuringTouch, true);
});

test("a touch counted during an active hunting response does not draw again", () => {
  let draws = 0;
  const system = new Esp32DecisionSystem(() => { draws += 1; return .9; });
  system.chooseTouchBranch();
  system.onTouchPress("TOP", 1000, true);
  assert.equal(draws, 3);
  const counted = system.onTouchPress("LEFT", 2000, true, false);
  assert.equal(counted.plan, null);
  assert.match(counted.reason, /response=COUNT_ONLY/);
  assert.equal(draws, 3);
  assert.equal(system.attackCount, 2);
});

test("interaction branch is sampled once and exploration cycles through distinct sound-light patterns", () => {
  const explore = new Esp32DecisionSystem(() => 0,
    { explorationBranchPercent: 100, endingAfterRounds: 0 });
  assert.equal(explore.chooseTouchBranch(), "EXPLORE");
  assert.equal(explore.chooseTouchBranch(), "EXPLORE");
  const firstCycle = Array.from({ length: EXPLORATION_SOUND_LIGHT_PATTERNS.length }, (_, index) =>
    explore.onExplorationTouch("TOP", index * 2000).pattern.name);
  assert.equal(new Set(firstCycle).size, EXPLORATION_SOUND_LIGHT_PATTERNS.length);
  assert.ok(EXPLORATION_SOUND_LIGHT_PATTERNS.every(pattern => pattern.command.startsWith("LED ") && pattern.beepMs > 0));
  const next = explore.onExplorationTouch("TOP", 10000).pattern.name;
  assert.notEqual(next, firstCycle.at(-1));
  const base = { phase: "INTERACTION", autoBusy: false, nextDecisionAt: 0,
    obstacleBlockedUntil: 0, sessionElapsedMs: 1000,
    camera: { detected: true, distanceCm: 55 } };
  assert.equal(explore.tick({ ...base, now: 11000, touchActive: true }), null);
  assert.equal(explore.tick({ ...base, now: 11000 }).action, "PROBE_APPROACH");
  assert.equal(explore.tick({ ...base, now: 23000 }).action, "END_SESSION");
  explore.finishInteraction();
  assert.equal(explore.touchBranch, null);
  const hunt = new Esp32DecisionSystem(() => .5, { explorationBranchPercent: 0 });
  assert.equal(hunt.chooseTouchBranch(), "HUNT");
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
  assert.equal(legacy.touchPreyPercent, 20);
  assert.equal(legacy.touchFlashPercent, 20);
  assert.equal(legacy.touchSlowEscapePercent, 60);
  assert.equal(legacy.touchFastEscapePercent, 20);
  assert.equal(legacy.robotFarProbePercent, 10);
  assert.equal(legacy.robotNearObservePercent, 40);
  assert.equal(legacy.initialLookSeconds, 5);
  assert.equal(legacy.touchDebounceMs, 1000);
  assert.equal(legacy.postEscapeWaitSeconds, 10);
  const saved = validateResponsePolicy({ touchSignalPercent: 30,
    touchSlowEscapePercent: 30, touchFastEscapePercent: 40 });
  assert.equal(saved.touchPreyPercent, 30);
  assert.equal(saved.touchFlashPercent, 30);
  assert.equal(saved.touchSlowEscapePercent, 30);
  const previousFourWay = validateResponsePolicy({ touchPreyPercent: 20,
    touchSoundLightPercent: 20, touchSlowEscapePercent: 30, touchFastEscapePercent: 30 });
  assert.equal(previousFourWay.touchSlowEscapePercent, 40);
  assert.equal(previousFourWay.touchFastEscapePercent, 40);
  const fresh = validateResponsePolicy({});
  assert.equal(fresh.explorationBranchPercent, 20);
  assert.equal(fresh.touchPreyPercent + fresh.touchSlowEscapePercent +
    fresh.touchFastEscapePercent, 100);
  assert.throws(() => validateResponsePolicy({ explorationBranchPercent: 101 }), /explorationBranchPercent/);
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

test("prey endings are chosen on a caught touch; idle endings only resume patrol", () => {
  const base = { phase: "INTERACTION", autoBusy: false, nextDecisionAt: 0,
    obstacleBlockedUntil: 0, sessionElapsedMs: 1000,
    camera: { detected: true, distanceCm: 55 } };
  const rounds = new Esp32DecisionSystem(() => .9, validateResponsePolicy({
    endingAfterRounds: 2, endingInactivitySeconds: 0,
  }));
  rounds.onTouchPress("TOP", 1000);
  rounds.onTouchPress("LEFT", 1100, false);
  assert.equal(rounds.catInteractionRounds, 1, "swipes in one episode count as one round");
  rounds.onTouchPress("TOP", 2000);
  const caughtEnding = rounds.chooseCatEnding("caught_touch");
  assert.equal(caughtEnding.endingOutcome, "PREY_SIGNAL");
  assert.match(caughtEnding.reason, /trigger=caught_touch/);
  const roundEnding = rounds.tick({ ...base, now: 2500 });
  assert.equal(roundEnding.action, "END_SESSION");
  assert.equal(roundEnding.endingOutcome, "CONTINUE_PATROL");
  assert.match(roundEnding.reason, /round_limit_after_release/);
  rounds.finishInteraction();
  assert.equal(rounds.catInteractionRounds, 0);

  const inactivity = new Esp32DecisionSystem(() => .5, validateResponsePolicy({
    endingAfterRounds: 0, endingInactivitySeconds: 4,
  }));
  inactivity.onTouchPress("TOP", 1000);
  assert.equal(inactivity.tick({ ...base, nextDecisionAt: 6000, now: 4999 }), null);
  const idleEnding = inactivity.tick({ ...base, nextDecisionAt: 6000, now: 5000 });
  assert.equal(idleEnding.endingOutcome, "CONTINUE_PATROL");
  assert.match(idleEnding.reason, /inactive_after_touch/);
  assert.equal(inactivity.tick({ ...base, autoBusy: true, sessionElapsedMs: 60000,
    now: 60000 }), null, "an active escape cannot be interrupted by the session limit");
});
