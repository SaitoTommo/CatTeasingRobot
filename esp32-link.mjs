import { serialCrc8 } from './hardware-telemetry.mjs';
export function validateRoute(route) {
  if (!Array.isArray(route) || !route.length || route.length > 128 ||
      route.some(id => !Number.isInteger(id) || id < 0 || id > 586))
    throw new Error('tag36h11 路线需要 1–128 个 ID，编号范围 0–586');
  return [...route];
}
export function decodeHostCommand(wire) {
  const m=/^(C \d+ .+)\*([0-9A-F]{2})$/.exec(wire);
  if (!m || serialCrc8(m[1])!==m[2]) throw new Error('ESP32 command CRC error');
  return m[1].replace(/^C /,'CMD ').replace(/^(CMD \d+) CF\b/,'$1 CAMERA_FRONT').replace(/^(CMD \d+) GT$/,'$1 GET_TELEMETRY');
}
export function compactReply(line) { return `${line}*${serialCrc8(line)}`; }
