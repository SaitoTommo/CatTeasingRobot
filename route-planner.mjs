import { observedTravelCm } from "./escape-planner.mjs";
export function decideLocalAvoidance({ frontCm, requestedCm }) {
  if (!Number.isFinite(frontCm)) return { action: "STOP" };
  const distanceCm = observedTravelCm(frontCm, Math.min(12, requestedCm));
  return distanceCm < 5 ? { action: "SCAN" } : { action: "DRIVE", distanceCm };
}
