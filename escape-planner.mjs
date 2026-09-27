import { cameraFrontDistanceCm, forwardPathClearanceCm, radians, wrapAngle } from "./core.mjs";

// Choose a heading that leaves room to drive and then stop before an obstacle.
export function chooseEscapeRoute(candidates, requestedDistanceCm, marginCm = 15) {
  const viable = candidates
    .filter(candidate => candidate.turnAllowed && Number.isFinite(candidate.frontClearanceCm))
    .map(candidate => ({ ...candidate,
      distanceCm: Math.min(requestedDistanceCm, Math.floor(candidate.frontClearanceCm - marginCm)) }))
    .filter(candidate => candidate.distanceCm >= 5);
  viable.sort((a, b) => b.distanceCm - a.distanceCm || a.priority - b.priority);
  return viable[0] ?? null;
}

export function planSimulatedEscapeRoute(world, requestedDistanceCm, canTurn = () => true) {
  const awayHeading = Math.atan2(world.robot.y - world.cat.y, world.robot.x - world.cat.x);
  const candidates = [0, -90, 90].map((offset, priority) => {
    const angle = Math.round(wrapAngle(awayHeading + radians(offset) - world.robot.heading) * 180 / Math.PI);
    const orientedWorld = { ...world,
      robot: { ...world.robot, heading: wrapAngle(world.robot.heading + radians(angle)) } };
    return { angle, priority,
      turnAllowed: (angle === 0 || canTurn(angle < 0 ? -1 : 1)) && cameraFrontDistanceCm(orientedWorld) >= 15,
      frontClearanceCm: forwardPathClearanceCm(orientedWorld) };
  });
  return chooseEscapeRoute(candidates, requestedDistanceCm);
}
