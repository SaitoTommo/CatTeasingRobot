// Only current observations. No coordinates or hypothetical raycasts.
export function observedTravelCm(frontCm, requestedCm, marginCm = 15) {
  if (!Number.isFinite(frontCm) || !Number.isFinite(requestedCm)) return 0;
  return Math.max(0, Math.min(requestedCm, Math.floor(frontCm - marginCm)));
}
export function escapeTurn(camera, touchArea) {
  if (camera.detected && Number.isFinite(camera.relativeDeg)) {
    const away = camera.relativeDeg + 180;
    return Math.round(away > 180 ? away - 360 : away);
  }
  return ({ LEFT: 90, RIGHT: -90, REAR: 0, TOP: 180 })[touchArea] ?? 180;
}
