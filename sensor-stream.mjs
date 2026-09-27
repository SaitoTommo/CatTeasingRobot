const CAMERA_REFRESH_MS = 350; // Uno rejects CAMERA_FRONT after 500 ms.
const SIDE_UPDATE_MS = 250;
const SIDE_CHANGE_CM = 3;
const FRONT_STOP_CM = 15;
const DEFAULT_SIDE_SAFE_CM = 5;

function crossesStopBoundary(previous, current, stopCm = FRONT_STOP_CM) {
  if (!Number.isFinite(previous) && !Number.isFinite(current)) return false;
  if (!Number.isFinite(previous) || !Number.isFinite(current)) return true;
  return (previous < stopCm) !== (current < stopCm);
}

export class SceneSensorPublisher {
  constructor() { this.reset(); }

  reset() {
    this.cameraAt = -Infinity;
    this.cameraValue = null;
    this.sideValues = { LEFT: null, RIGHT: null };
    this.sideAt = { LEFT: -Infinity, RIGHT: -Infinity };
  }

  plan({ now, cameraValue, sides = null, sideSafeCm = { LEFT: DEFAULT_SIDE_SAFE_CM, RIGHT: DEFAULT_SIDE_SAFE_CM },
    moving = false, beforeMotion = false, frontRequired = true }) {
    if (!moving && !beforeMotion) return [];
    const commands = [];
    const cameraChangedSafety = crossesStopBoundary(Number(this.cameraValue), Number(cameraValue)) ||
      ((this.cameraValue === "INVALID") !== (cameraValue === "INVALID"));
    if (frontRequired && (beforeMotion || cameraChangedSafety || now - this.cameraAt >= CAMERA_REFRESH_MS)) {
      commands.push(`CAMERA_FRONT ${cameraValue}`);
      this.cameraAt = now;
      this.cameraValue = cameraValue;
    }
    if (!sides) return commands;
    for (const side of ["LEFT", "RIGHT"]) {
      const value = sides[side];
      const previous = this.sideValues[side];
      const urgent = previous === null || crossesStopBoundary(Number(previous), Number(value), sideSafeCm[side]);
      const changed = previous === null || urgent || Math.abs(Number(value) - Number(previous)) >= SIDE_CHANGE_CM;
      if (!changed || (!urgent && now - this.sideAt[side] < SIDE_UPDATE_MS)) continue;
      commands.push(`DEBUG_INJECT SONAR ${side} ${value}`);
      this.sideValues[side] = value;
      this.sideAt[side] = now;
    }
    return commands;
  }
}
