export const IO_CHANNELS = ["MOTOR_LEFT", "MOTOR_RIGHT", "LED", "BUZZER", "SONAR_LEFT_TRIG", "SONAR_RIGHT_TRIG", "SONAR_LEFT_ECHO", "SONAR_RIGHT_ECHO", "ENCODER_LEFT", "ENCODER_RIGHT", "TOUCH_TOP", "TOUCH_LEFT", "TOUCH_RIGHT", "TOUCH_REAR"];
const lengths = { STATE: 5, ML: 6, MR: 6, EL: 5, ER: 5, SL: 6, SR: 6, T: 5, OUT: 6, FRONT: 2 };
const sourceNames = { P: "物理输入", V: "虚拟脉冲", I: "测试注入", O: "输入关闭", TEST: "电脑模拟", HOST: "上位机" };
const source = value => sourceNames[value] || value;
const level = value => Number(value) === -1 ? "未采样" : Number(value) ? "HIGH" : "LOW";
const distance = value => Number(value) < 0 ? "无效" : `${value} cm`;
const direction = value => Number(value) > 0 ? "正转" : Number(value) < 0 ? "反转" : "停止";

// Rows are independently timestamped; a partial reply must not make older rows fresh.
export class HardwareTelemetry {
  constructor() { this.reset(); }
  reset() { this.rows = new Map(); }
  accept(line, now) {
    const match = /^DATA (\d+) HW (\S+) (.*)$/.exec(line);
    if (!match || !lengths[match[2]]) return null;
    const kind = match[2];
    const values = match[3].split(/\s+/);
    if (values.length !== lengths[kind]) return null;
    const textIndexes = kind === "STATE" ? [1] : kind === "T" ? [0, 4] : ["EL", "ER"].includes(kind) ? [4] : ["SL", "SR"].includes(kind) ? [5] : kind === "FRONT" ? [1] : [];
    if (values.some((value, index) => !textIndexes.includes(index) && !/^-?\d+(?:\.\d+)?$/.test(value))) return null;
    if (kind === "T" && !["TOP", "LEFT", "RIGHT", "REAR"].includes(values[0])) return null;
    if (kind === "STATE" && (!["IDLE", "MOVING", "STOPPED"].includes(values[1]) || Number(values[4]) < 0 || Number(values[4]) > 16383)) return null;
    const key = kind === "T" ? `T_${values[0]}` : kind;
    const row = { key, kind, values, at: now, id: Number(match[1]) };
    this.rows.set(key, row);
    return row;
  }
  display(key, now) {
    const row = this.rows.get(key);
    if (!row) return { text: "等待 Uno 回报", state: "waiting" };
    if (now - row.at > 2500) return { text: "读数已过期 · 等待新回报", state: "stale" };
    const v = row.values;
    let text;
    if (["ML", "MR"].includes(key)) text = `输入指令 ${v[0]}（${direction(v[0])}）\n引脚驱动 ${v[1]} · PWM ${v[2]}/255\nIN ${level(v[4])}/${level(v[5])} · 输出${Number(v[3]) ? "开启" : "关闭"}`;
    else if (["EL", "ER"].includes(key)) text = `${v[2]} 脉冲 · ${v[3]} RPM\n数字输入 ${level(v[1])} · ${source(v[4])}`;
    else if (["SL", "SR"].includes(key)) text = `物理测距 ${distance(v[3])}\n控制读数 ${distance(v[4])} · ${source(v[5])}\nTRIG ${Number(v[0]) ? "开启" : "关闭"} / ECHO ${Number(v[1]) ? "开启" : "关闭"} · ${level(v[2])}`;
    else if (key.startsWith("T_")) text = `输入 ${level(v[2])} · ${Number(v[3]) ? "按下" : "松开"}\n${source(v[4])}`;
    else if (key === "OUT") text = `LED 指令 ${Number(v[1]) ? "亮" : "灭"} → ${Number(v[2])===-1?"交由 ESP 输出":level(v[2])}（${Number(v[0]) ? "开启" : "关闭"}）\n蜂鸣器指令 ${Number(v[4]) ? "响" : "停"} → ${level(v[5])}（${Number(v[3]) ? "开启" : "关闭"}）`;
    else if (key === "FRONT") text = `${distance(v[0])} · ${source(v[1])}\n通过串口接收前向判定`;
    else text = `${v[1]} · 运行 ${(Number(v[0]) / 1000).toFixed(1)} s\n调试模式 ${Number(v[2]) ? 'ON' : 'OFF'} · 电机 ${Number(v[3]) ? 'DRY_RUN（禁止物理输出）' : 'LIVE'}\n物理 I/O 掩码 ${v[4]}`;
    return { text, state: "fresh" };
  }
}

// At most one request on the wire; a missing reply triggers a bounded backoff.
export class TelemetryPoller {
  constructor() { this.reset(); }
  reset() { this.pending = null; this.nextAt = 0; }
  due(now, enabled) {
    if (this.pending && now - this.pending.at >= 5000) { this.pending = null; this.nextAt = now + 2000; }
    return enabled && !this.pending && now >= this.nextAt;
  }
  begin(id, now) { this.pending = { id, at: now }; this.nextAt = now + 250; }
  settle(id, now) { if (this.pending?.id === id) { this.pending = null; this.nextAt = Math.max(this.nextAt, now + 50); } }
}

// A valid ACK or terminal event can recover a lost
// MOTION_STARTED. Never start the scene merely because a command was sent.
export function confirmSceneMotion(pending, active, id, pose, revision, details = "") {
  let result = active?.id === id ? active : null;
  const request = pending.get(id);
  if (request?.renewalOf) { pending.delete(id); return active; }
  if (!result && request && (!active || active.id < id)) {
    const terminal = /^EVENT MOTION_(DONE|ABORTED)\b/.test(details);
    result = { ...request, id, startPose: { ...(terminal ? request.startPose || pose : pose) },
      manualRobotRevision: request.manualRobotRevision ?? revision };
    pending.delete(id);
  }
  const base = /\bLEFT_BASE=(\d+) RIGHT_BASE=(\d+)/.exec(details);
  if (result && base) result.encoderBase = base.slice(1).map(Number);
  return result || active;
}

export function serialCrc8(text) {
  let crc = 0;
  for (const byte of new TextEncoder().encode(text)) {
    crc ^= byte;
    for (let i = 0; i < 8; i++) crc = ((crc << 1) ^ (crc & 128 ? 7 : 0)) & 255;
  }
  return crc.toString(16).padStart(2, "0").toUpperCase();
}

export function encodeSerialCommand(line) {
  const compact = line.replace(/^(CMD \d+) CAMERA_FRONT\b/, "$1 CF")
    .replace(/^(CMD \d+) GET_TELEMETRY$/, "$1 GT")
    .replace(/^(CMD \d+) DEBUG_INJECT SONAR LEFT\b/, "$1 DI S L")
    .replace(/^(CMD \d+) DEBUG_INJECT SONAR RIGHT\b/, "$1 DI S R")
    .replace(/^CMD /, "C ");
  return `${compact}*${serialCrc8(compact)}`;
}

// Only exact, intact short frames are expanded; damaged bytes are never guessed.
export function decodeSerialFrame(raw) {
  let line = raw.trim();
  const star = line.indexOf("*");
  if (star >= 0) {
    const payload = line.slice(0, star);
    if (!/^[0-9A-F]{2}$/.test(line.slice(star + 1)) || serialCrc8(payload) !== line.slice(star + 1)) return null;
    line = payload;
  } else if (/^(?:MS|MD|MA|MP|AK)\b/.test(line)) return null;
  let match;
  if ((match = /^MS (\d+) (\d+) (\d+)$/.exec(line))) return `EVENT MOTION_STARTED ${match[1]} LEFT_BASE=${match[2]} RIGHT_BASE=${match[3]}`;
  if ((match = /^MD (\d+) (\d+) (\d+)$/.exec(line))) return `EVENT MOTION_DONE ${match[1]} LEFT_PULSES=${match[2]} RIGHT_PULSES=${match[3]}`;
  if ((match = /^MP (\d+) (\d+) (\d+)$/.exec(line))) return `EVENT MOTION_PROGRESS ${match[1]} LEFT_PULSES=${match[2]} RIGHT_PULSES=${match[3]}`;
  if ((match = /^MA (\d+) ([A-Z_]+) (\d+) (\d+)$/.exec(line))) return `EVENT MOTION_ABORTED ${match[1]} ${match[2]} LEFT_PULSES=${match[3]} RIGHT_PULSES=${match[4]}`;
  if ((match = /^AK (\d+)$/.exec(line))) return `ACK ${match[1]}`;
  return line;
}

export function encoderWheelTravel(motion, pulses, wheelMm) {
  if (motion.lastPulses && pulses.some((count, i) => count < motion.lastPulses[i])) return null;
  const cmPerPulse = Math.PI * wheelMm / 10 / 20;
  let leftCm = Math.sign(motion.left) * pulses[0] * cmPerPulse;
  let rightCm = Math.sign(motion.right) * pulses[1] * cmPerPulse;
  const travelCm = (Math.abs(leftCm) + Math.abs(rightCm)) / 2;
  if (motion.left && Math.abs(motion.left) === Math.abs(motion.right)) {
    leftCm = Math.sign(motion.left) * travelCm;
    rightCm = Math.sign(motion.right) * travelCm;
  }
  return { leftCm, rightCm, travelCm };
}
