import { observedTravelCm } from "./escape-planner.mjs";
export function decideLocalAvoidance({ frontCm, requestedCm, maxStepCm = 12 }) {
  if (!Number.isFinite(frontCm)) return { action: "STOP" };
  const distanceCm = observedTravelCm(frontCm, Math.min(maxStepCm, requestedCm));
  return distanceCm < 5 ? { action: "SCAN" } : { action: "DRIVE", distanceCm };
}
