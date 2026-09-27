// ESP32 decision policy. This module accepts observations and emits behavior decisions;
// it has no DOM, canvas, serial, or cat-scene dependency.
export const DEFAULT_RESPONSE_POLICY = Object.freeze({
  touchFlashPercent: 20, touchPreyPercent: 20, touchSlowEscapePercent: 40, touchFastEscapePercent: 40,
  catFollowProbePercent: 5, catFollowObservePercent: 35, catFollowPatrolPercent: 60,
  endingAfterRounds: 3, endingInactivitySeconds: 12,
  endingPatrolPercent: 40, endingSilentPercent: 30, endingSignalPercent: 30,
  captureWaitMaxSeconds: 15, captureCueFlashMs: 2200, captureCueBeepMs: 360,
  robotFarApproachPercent: 85, robotFarProbePercent: 15,
  robotNearSignalPercent: 20, robotNearObservePercent: 40, robotNearStayPercent: 40,
  catProbeCooldownSeconds: 15, robotProbeCooldownSeconds: 15, farCatThresholdCm: 40,
  initialLookSeconds: 5, postEscapeWaitSeconds: 10,
  touchDebounceMs: 1000,
  catLeaveDistanceCm: 70, catLeaveSeconds: 5, sessionLimitSeconds: 60,
  decisionIntervalMs: 2500, searchTimeoutMs: 15000, finalSearchTimeoutMs: 8000,
  alignTimeoutMs: 5000, touchReleaseTimeoutMs: 4000, searchPwm: 75,
  centerTolerancePercent: 5.5, alignPwm: 52,
  slowApproachCm: 9, slowApproachPwm: 95,
  probeStep1Cm: 3, probeStep2Cm: 4, probeStep3Cm: 3,
  probePause1Ms: 700, probePause2Ms: 900, probePause3Ms: 600, probePwm: 90,
  signalFlashMs: 1400, signalBeepMs: 180,
  escapeDistanceCm: 26, slowEscapePwm: 95, fastEscapePwm: 210,
});

export const RESPONSE_POLICY_LIMITS = Object.freeze({
  touchFlashPercent: [0, 100], touchPreyPercent: [0, 100],
  touchSlowEscapePercent: [0, 100], touchFastEscapePercent: [0, 100],
  catFollowProbePercent: [0, 100], catFollowObservePercent: [0, 100], catFollowPatrolPercent: [0, 100],
  endingAfterRounds: [0, 20], endingInactivitySeconds: [0, 120],
  endingPatrolPercent: [0, 100], endingSilentPercent: [0, 100], endingSignalPercent: [0, 100],
  captureWaitMaxSeconds: [1, 60], captureCueFlashMs: [1000, 5000], captureCueBeepMs: [100, 1000],
  robotFarApproachPercent: [0, 100], robotFarProbePercent: [0, 100],
  robotNearSignalPercent: [0, 100], robotNearObservePercent: [0, 100], robotNearStayPercent: [0, 100],
  catProbeCooldownSeconds: [0, 60], robotProbeCooldownSeconds: [0, 60], farCatThresholdCm: [20, 69],
  initialLookSeconds: [0, 30], postEscapeWaitSeconds: [0, 60],
  touchDebounceMs: [0, 5000],
  catLeaveDistanceCm: [41, 150], catLeaveSeconds: [1, 30], sessionLimitSeconds: [10, 300],
  decisionIntervalMs: [0, 10000], searchTimeoutMs: [1000, 30000],
  finalSearchTimeoutMs: [1000, 30000], alignTimeoutMs: [1000, 15000],
  touchReleaseTimeoutMs: [500, 10000], searchPwm: [30, 150],
  centerTolerancePercent: [1, 20], alignPwm: [30, 150],
  slowApproachCm: [1, 30], slowApproachPwm: [50, 255],
  probeStep1Cm: [1, 20], probeStep2Cm: [1, 20], probeStep3Cm: [1, 20],
  probePause1Ms: [0, 3000], probePause2Ms: [0, 3000], probePause3Ms: [0, 3000],
  probePwm: [50, 255], signalFlashMs: [100, 5000], signalBeepMs: [0, 1000],
  escapeDistanceCm: [1, 60], slowEscapePwm: [50, 255], fastEscapePwm: [50, 255],
});

export function validateResponsePolicy(policy) {
  if (!policy || typeof policy !== "object" || Array.isArray(policy))
    throw new Error("INVALID_RESPONSE_POLICY");
  // Migrate the two-probability settings saved by earlier versions of the lab.
  const migrated = { ...policy };
  if (policy.signalPercent !== undefined && policy.touchSignalPercent === undefined && policy.touchPreyPercent === undefined) {
    migrated.touchPreyPercent = policy.signalPercent;
    if (policy.touchFlashPercent === undefined) migrated.touchFlashPercent = policy.signalPercent;
    migrated.touchSlowEscapePercent = Math.round((100 - policy.signalPercent) *
      (policy.slowEscapePercent ?? 50) / 100);
    migrated.touchFastEscapePercent = 100 - migrated.touchPreyPercent - migrated.touchSlowEscapePercent;
  }
  // The former post-release signal share becomes the caught/prey share. Keep
  // the same value for the independent contact-light draw on saved policies.
  if (policy.touchSignalPercent !== undefined) {
    if (policy.touchPreyPercent === undefined) migrated.touchPreyPercent = policy.touchSignalPercent;
    if (policy.touchFlashPercent === undefined) migrated.touchFlashPercent = policy.touchSignalPercent;
  }
  if (policy.probePercent !== undefined && policy.robotFarProbePercent === undefined) {
    migrated.robotFarProbePercent = policy.probePercent;
    migrated.robotFarApproachPercent = 100 - policy.probePercent;
  }
  if (policy.probeCooldownSeconds !== undefined && policy.robotProbeCooldownSeconds === undefined)
    migrated.robotProbeCooldownSeconds = policy.probeCooldownSeconds;
  if (policy.catInactivitySeconds !== undefined && policy.endingInactivitySeconds === undefined)
    migrated.endingInactivitySeconds = policy.catInactivitySeconds;
  if (policy.nearObservePercent !== undefined && policy.robotNearObservePercent === undefined) {
    const signal = policy.signalPercent ?? 20;
    migrated.robotNearSignalPercent = signal;
    migrated.robotNearObservePercent = Math.round((100 - signal) * policy.nearObservePercent / 100);
    migrated.robotNearStayPercent = 100 - signal - migrated.robotNearObservePercent;
  }
  const resolved = { ...DEFAULT_RESPONSE_POLICY, ...migrated };
  for (const [key, [min, max]] of Object.entries(RESPONSE_POLICY_LIMITS)) {
    const value = resolved[key];
    if (!Number.isFinite(value) || value < min || value > max ||
        (key !== "centerTolerancePercent" && !Number.isInteger(value)))
      throw new Error(`INVALID_RESPONSE_POLICY ${key}`);
  }
  if (resolved.farCatThresholdCm >= resolved.catLeaveDistanceCm)
    throw new Error("INVALID_RESPONSE_POLICY distance_order");
  for (const keys of [
    ["touchPreyPercent", "touchSlowEscapePercent", "touchFastEscapePercent"],
    ["catFollowProbePercent", "catFollowObservePercent", "catFollowPatrolPercent"],
    ["robotFarApproachPercent", "robotFarProbePercent"],
    ["robotNearSignalPercent", "robotNearObservePercent", "robotNearStayPercent"],
    ["endingPatrolPercent", "endingSilentPercent", "endingSignalPercent"],
  ]) {
    if (keys.reduce((sum, key) => sum + resolved[key], 0) !== 100)
      throw new Error(`INVALID_RESPONSE_POLICY sum_${keys[0]}`);
  }
  if (resolved.endingAfterRounds === 0 && resolved.endingInactivitySeconds === 0)
    throw new Error("INVALID_RESPONSE_POLICY ending_trigger");
  return Object.fromEntries(Object.keys(DEFAULT_RESPONSE_POLICY).map(key => [key, resolved[key]]));
}

export function touchResponsePlan(area, random = Math.random, policy = DEFAULT_RESPONSE_POLICY) {
  if (!["TOP", "LEFT", "RIGHT", "REAR"].includes(area)) throw new Error("INVALID_TOUCH_AREA");
  const { touchFlashPercent, touchPreyPercent, touchSlowEscapePercent } = validateResponsePolicy(policy);
  const flashDraw = random();
  const draw = random();
  const response = draw < touchPreyPercent / 100 ? "PREY_STILL"
    : draw < (touchPreyPercent + touchSlowEscapePercent) / 100
      ? "SLOW_STRAIGHT_ESCAPE" : "FAST_STRAIGHT_ESCAPE";
  return {
    area,
    direction: area === "LEFT" ? -1 : 1,
    sweepLimitDeg: 360,
    response,
    flashDuringTouch: flashDraw < touchFlashPercent / 100,
    flashDraw,
    draw,
    escapeDraw: response === "PREY_STILL" ? null : draw,
  };
}

export function chooseRobotInitiatedAction({ camera, random = Math.random, policy = DEFAULT_RESPONSE_POLICY,
  probeAllowed = true }) {
  if (!camera.detected) return { action: "SEARCH_CAT_START", reason: "cat_outside_camera" };
  const { robotFarProbePercent, robotNearSignalPercent, robotNearObservePercent,
    farCatThresholdCm } = validateResponsePolicy(policy);
  const distanceCm = camera.distanceCm;
  const draw = random();
  let action;
  let choices;
  if (distanceCm > farCatThresholdCm) {
    choices = ["SLOW_APPROACH", "PROBE_APPROACH"];
    action = probeAllowed && draw < robotFarProbePercent / 100 ? "PROBE_APPROACH" : "SLOW_APPROACH";
  } else {
    choices = ["STAY_SIGNAL", "OBSERVE", "STAY"];
    const signalProbability = robotNearSignalPercent / 100;
    action = draw < signalProbability ? "STAY_SIGNAL"
      : draw < (robotNearSignalPercent + robotNearObservePercent) / 100 ? "OBSERVE" : "STAY";
  }
  return { action, reason: `cat_visible_distance_cm=${distanceCm.toFixed(1)} random=${draw.toFixed(3)} candidates=${choices.join(",")}` +
    (distanceCm > farCatThresholdCm ? ` probe_pct=${robotFarProbePercent} probe_allowed=${probeAllowed}` : "") };
}

export function chooseCatInitiatedFollowUp(random = Math.random, policy = DEFAULT_RESPONSE_POLICY,
  probeAllowed = true) {
  const { catFollowProbePercent, catFollowObservePercent, catFollowPatrolPercent } = validateResponsePolicy(policy);
  const draw = random();
  const action = !probeAllowed
    ? draw < catFollowObservePercent / (catFollowObservePercent + catFollowPatrolPercent) ? "OBSERVE" : "END_SESSION"
    : draw < catFollowProbePercent / 100 ? "PROBE_APPROACH"
      : draw < (catFollowProbePercent + catFollowObservePercent) / 100 ? "OBSERVE" : "END_SESSION";
  return { action, reason: `cat_touch_follow_up random=${draw.toFixed(3)}` +
    ` probe_pct=${catFollowProbePercent} observe_pct=${catFollowObservePercent}` +
    ` probe_allowed=${probeAllowed}` };
}

export function chooseEndingOutcome(random = Math.random, policy = DEFAULT_RESPONSE_POLICY) {
  const { endingPatrolPercent, endingSilentPercent } = validateResponsePolicy(policy);
  const draw = random();
  const outcome = draw < endingPatrolPercent / 100 ? "CONTINUE_PATROL"
    : draw < (endingPatrolPercent + endingSilentPercent) / 100 ? "PREY_STILL" : "PREY_SIGNAL";
  return { outcome, draw };
}

export function decideTargetAlignment(observation, tolerance) {
  if (!observation.detected) return { action: "LOST" };
  if (Math.abs(observation.centerX - .5) <= tolerance) return { action: "CENTERED" };
  return { action: "TURN", direction: observation.centerX > .5 ? 1 : -1 };
}

export function decidePatrolStep({ detected, distanceCm, targetCm }) {
  if (!detected) return { action: "SCAN" };
  if (distanceCm > targetCm + 1)
    return { action: "APPROACH", stepCm: Math.min(90, distanceCm - targetCm) };
  return { action: "NEXT_TAG" };
}

export function decideScanProgress({ detected, sweptDeg, limitDeg, timedOut }) {
  if (detected) return "FOUND";
  if (sweptDeg >= limitDeg) return "FULL_SWEEP";
  if (timedOut) return "TIMEOUT";
  return "CONTINUE";
}

export function turnObstacleReason({ direction, leftCm, rightCm, leftSafeCm = 5, rightSafeCm = 5, available }) {
  if (!available) return null;
  const side = direction < 0 ? "LEFT" : "RIGHT";
  const distance = direction < 0 ? leftCm : rightCm;
  const safeCm = direction < 0 ? leftSafeCm : rightSafeCm;
  return Number.isFinite(distance) && distance >= safeCm
    ? null : `SIDE_${side}_OBSTACLE_OR_SENSOR_INVALID`;
}

export class Esp32DecisionSystem {
  constructor(random = Math.random, responsePolicy = DEFAULT_RESPONSE_POLICY) {
    this.random = random;
    this.setResponsePolicy(responsePolicy);
    this.reset();
  }

  setResponsePolicy(policy) {
    this.responsePolicy = validateResponsePolicy(policy);
  }

  reset() {
    this.attackCount = 0;
    this.catAbsentSince = 0;
    this.catWasNear = false;
    this.searchAttempted = false;
    this.lastRobotProbeAt = -Infinity;
    this.lastCatProbeAt = -Infinity;
    this.initiator = null;
    this.catFollowUpStage = null;
    this.lastCatTouchAt = null;
    this.catInteractionRounds = 0;
    this.endingOutcome = null;
  }

  chooseRobotAction(camera, now) {
    const probeAllowed = now - this.lastRobotProbeAt >= this.responsePolicy.robotProbeCooldownSeconds * 1000;
    const choice = chooseRobotInitiatedAction({ camera, random: this.random,
      policy: this.responsePolicy, probeAllowed });
    if (choice.action === "PROBE_APPROACH") this.lastRobotProbeAt = now;
    if (choice.action === "SEARCH_CAT_START") this.searchAttempted = true;
    return choice;
  }

  chooseCatFollowUp(camera, now) {
    const probeAllowed = camera.detected &&
      now - this.lastCatProbeAt >= this.responsePolicy.catProbeCooldownSeconds * 1000;
    const choice = chooseCatInitiatedFollowUp(this.random, this.responsePolicy, probeAllowed);
    if (choice.action === "PROBE_APPROACH") this.lastCatProbeAt = now;
    this.catFollowUpStage = "DONE";
    return choice;
  }

  abortCatFollowUp() {
    if (this.initiator === "CAT_TOUCH") this.catFollowUpStage = "FAILED";
  }

  chooseCatEnding(trigger) {
    const choice = chooseEndingOutcome(this.random, this.responsePolicy);
    this.endingOutcome = choice.outcome;
    return { action: "END_SESSION", endingOutcome: choice.outcome,
      reason: `cat_ending trigger=${trigger} rounds=${this.catInteractionRounds}` +
        ` random=${choice.draw.toFixed(3)} outcome=${choice.outcome}` };
  }

  finishInteraction() {
    this.initiator = null;
    this.catFollowUpStage = null;
    this.lastCatTouchAt = null;
    this.catInteractionRounds = 0;
    this.endingOutcome = null;
    this.catAbsentSince = 0;
  }

  onTouchPress(area, now = 0, newEpisode = true) {
    const plan = touchResponsePlan(area, this.random, this.responsePolicy);
    this.attackCount += 1;
    this.catWasNear = true;
    this.searchAttempted = true;
    this.catAbsentSince = 0;
    if (this.initiator !== "CAT_TOUCH") this.catInteractionRounds = 0;
    this.initiator = "CAT_TOUCH";
    if (newEpisode) {
      this.catInteractionRounds += 1;
      this.catFollowUpStage = "PENDING";
    }
    this.lastCatTouchAt = now;
    return {
      action: "ATTACK_RESPONSE",
      plan,
      reason: `attack_touch_${area} count=${this.attackCount} interaction_round=${this.catInteractionRounds}` +
        ` random=${plan.draw.toFixed(3)}` +
        ` escape_random=${plan.escapeDraw === null ? "NONE" : plan.escapeDraw.toFixed(3)}` +
        ` contact_flash_random=${plan.flashDraw.toFixed(3)} contact_flash=${plan.flashDuringTouch}` +
        ` touch_flash_pct=${this.responsePolicy.touchFlashPercent}` +
        ` touch_prey_pct=${this.responsePolicy.touchPreyPercent}` +
        ` touch_slow_escape_pct=${this.responsePolicy.touchSlowEscapePercent}` +
        ` touch_fast_escape_pct=${this.responsePolicy.touchFastEscapePercent}` +
        ` response=${plan.response} direction=${plan.direction < 0 ? "LEFT" : "RIGHT"} sweep_limit_deg=${plan.sweepLimitDeg}`,
    };
  }

  onMoodAlert(source, { phase, camera, now = 0 }) {
    if (phase !== "PATROL" || !["CAMERA_NEGATIVE", "HTTP_NEGATIVE"].includes(source)) return null;
    this.initiator = "ROBOT_SEARCH";
    this.catFollowUpStage = null;
    this.catInteractionRounds = 0;
    const choice = this.chooseRobotAction(camera, now);
    return { action: choice.action, reason: `mood_alert_${source} next=${choice.action}`, enterInteraction: true };
  }

  tick({ phase, now, autoBusy, nextDecisionAt, obstacleBlockedUntil,
    sessionElapsedMs, sessionLimitSeconds, camera }) {
    if (phase === "PATROL") {
      if (!autoBusy && now >= nextDecisionAt && now >= obstacleBlockedUntil)
        return { action: "PATROL", reason: "patrol_no_alert" };
      return null;
    }
    if (phase !== "INTERACTION") return null;
    const policy = this.responsePolicy;
    const limitSeconds = sessionLimitSeconds ?? policy.sessionLimitSeconds;
    // Motion can point the camera away from the cat. Keep an existing absence
    // timer, but only start a new one after the current behavior completes.
    if (autoBusy) {
      if (camera.detected && camera.distanceCm <= policy.catLeaveDistanceCm) this.catAbsentSince = 0;
      return null;
    }
    if (sessionElapsedMs !== null && sessionElapsedMs >= limitSeconds * 1000)
      return { action: "END_SESSION", endingOutcome: "CONTINUE_PATROL",
        reason: `session_limit_${limitSeconds}` };
    if (this.initiator === "CAT_TOUCH") {
      if (this.catFollowUpStage === "FAILED")
        return { action: "END_SESSION", endingOutcome: "CONTINUE_PATROL",
          reason: "cat_touch_response_failed" };
      if (policy.endingAfterRounds > 0 && this.catInteractionRounds >= policy.endingAfterRounds)
        return { action: "END_SESSION", endingOutcome: "CONTINUE_PATROL",
          reason: "cat_round_limit_after_release" };
      if (policy.endingInactivitySeconds > 0 && this.lastCatTouchAt !== null &&
          now - this.lastCatTouchAt >= policy.endingInactivitySeconds * 1000)
        return { action: "END_SESSION", endingOutcome: "CONTINUE_PATROL",
          reason: "cat_inactive_after_touch" };
      if (now < nextDecisionAt || now < obstacleBlockedUntil) return null;
      if (this.catFollowUpStage === "PENDING") {
        const followUp = this.chooseCatFollowUp(camera, now);
        return followUp.action === "END_SESSION"
          ? { ...followUp, endingOutcome: "CONTINUE_PATROL" } : followUp;
      }
      return null;
    }
    if (camera.detected && camera.distanceCm <= policy.catLeaveDistanceCm) {
      this.catWasNear = true;
      this.searchAttempted = false;
    }
    if (this.catWasNear && (!camera.detected || camera.distanceCm > policy.catLeaveDistanceCm)) {
      if (!this.catAbsentSince) this.catAbsentSince = now;
    } else this.catAbsentSince = 0;
    if (this.catAbsentSince && now - this.catAbsentSince >= policy.catLeaveSeconds * 1000)
      return { action: "END_SESSION", reason: `cat_left_for_${policy.catLeaveSeconds}s` };
    if (this.catAbsentSince) return null;
    if (!camera.detected && this.searchAttempted)
      return { action: "END_SESSION", reason: "cat_not_found_after_one_search" };
    if (now < nextDecisionAt) return null;
    if (now < obstacleBlockedUntil) return { action: "OBSERVE", reason: "uno_obstacle_or_invalid_distance" };
    return this.chooseRobotAction(camera, now);
  }
}
