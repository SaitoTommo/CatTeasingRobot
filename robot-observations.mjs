// Source selection only. No scene coordinates, object scripts or controller state.
const invalid = source => ({ valid: false, distanceCm: null, source });
const range = (value, source) => Number.isFinite(value) && value >= 0
  ? { valid: true, distanceCm: value, source } : invalid(source);
const fresh = (row, now) => row && now >= row.at && now - row.at <= 500;

export function sideObservation({ side, mode, physicalIo, rows, now,
  simulatedAllowed, readSimulated, readMock }) {
  if (mode === "mock") return range(readMock(side), "SIM_SENSOR");
  if (!physicalIo.has(`SONAR_${side}_TRIG`) || !physicalIo.has(`SONAR_${side}_ECHO`)) return invalid("SOURCE_UNKNOWN");
  const trig = physicalIo.get(`SONAR_${side}_TRIG`);
  const echo = physicalIo.get(`SONAR_${side}_ECHO`);
  if (trig || echo) {
    const row = rows.get(side === "LEFT" ? "SL" : "SR");
    if (!trig || !echo || !fresh(row, now) || row.values[5] !== "P") return invalid("PHYSICAL");
    return range(Number(row.values[3]), "PHYSICAL");
  }
  return simulatedAllowed ? range(readSimulated(side), "SIM_SENSOR") : invalid("DISABLED");
}

export function frontObservation({ mode, simulatedAllowed, readSimulated, rows, now }) {
  if (mode === "mock" || simulatedAllowed) return range(readSimulated(), "SIM_SENSOR");
  const row = rows.get("FRONT");
  if (!fresh(row, now) || row.values[1] !== "HOST") return invalid("HOST");
  return range(Number(row.values[0]), "HOST_CONTROL");
}

export function cameraObservationInput({ simulatedAllowed, readSimulated }) {
  if (!simulatedAllowed) return { valid: false, source: "UNAVAILABLE", detected: false,
    centerX: null, relativeDeg: null, distanceCm: null, bboxSize: null };
  return { ...readSimulated(), valid: true, source: "SIM_CAMERA" };
}

// Loss of sight and touch release are not evidence that the cat has departed.
export class CapturedCatWait {
  constructor({ startedAt, maxWaitMs, farCm, farHoldMs }) {
    Object.assign(this, { deadline: startedAt + maxWaitMs, farCm, farHoldMs });
    this.awaySince = null;
  }
  update({ camera, touchActive, now }) {
    const observedFar = camera.valid !== false && camera.detected &&
      Number.isFinite(camera.distanceCm) && camera.distanceCm > this.farCm;
    if (!touchActive && observedFar) {
      this.awaySince ??= now;
      if (now - this.awaySince >= this.farHoldMs)
        return { done: true, confirmedLeft: true, reason: "CAT_LEFT_OBSERVED" };
    } else this.awaySince = null;
    if (now >= this.deadline)
      return { done: true, confirmedLeft: false, reason: "MAX_WAIT_UNCONFIRMED" };
    return { done: false, confirmedLeft: false, reason: touchActive ? "CONTACT_ACTIVE"
      : !camera.detected || camera.valid === false ? "CAT_POSITION_UNKNOWN" : "CAT_NOT_CONFIRMED_FAR" };
  }
}
