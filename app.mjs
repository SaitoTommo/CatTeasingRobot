import {
  ARENA, MockUno, cameraObservation, cameraTargetObservation, clamp, contactArea,
  cameraFrontDistanceCm, forwardPathClearanceCm, degrees, initialWorld, radians, sideSonarDistanceCm, wrapAngle,
} from "./core.mjs";
import { CatMotionInput } from "./cat-input.mjs";
import {
  DEFAULT_RESPONSE_POLICY, RESPONSE_POLICY_LIMITS, Esp32DecisionSystem, decidePatrolStep, decideScanProgress,
  decideTargetAlignment, turnObstacleReason,
} from "./decision-system.mjs";
import { SceneSensorPublisher } from "./sensor-stream.mjs";
import { planSceneDetour } from "./route-planner.mjs";
import { planSimulatedEscapeRoute } from "./escape-planner.mjs";

const $ = id => document.getElementById(id);
const canvas = $("arena");
const ctx = canvas.getContext("2d");
const world = initialWorld();
const catInput = new CatMotionInput(world);
const decisionSystem = new Esp32DecisionSystem();
const RESPONSE_POLICY_STORAGE_KEY = "uno-web-lab.response-policy.v1";
const sceneSensors = new SceneSensorPublisher();
const trace = [];
const pendingMotion = new Map();
const pendingCommandDone = new Map();
const pendingSafeDistance = new Map();
const pendingSerialOutputs = new Map();
const serialOutputs = {
  led: "OFF", flashStartedAt: 0, flashIntervalMs: 0, flashUntil: 0, buzzerUntil: 0,
};
let nextCommandId = 1;
let port = null;
let reader = null;
let writer = null;
let reading = false;
let serialBuffer = "";
let mode = "mock";
let paused = false;
let drag = null;
let sessionStartedAt = null;
let behaviorToken = 0;
let activeSearch = null;
let activeScanMotion = null;
let lastTouchSide = null;
let lastTouchAt = 0;
let lastSerialAutoArea = null;
const manualTouchAreas = new Set();
const manualTouchTimers = new Map();
let manualFrontInput = null;
let toastTimer;
let realState = "未连接";
let realDistanceText = "未读取";
let realLeftDistanceText = "未读取";
let realRightDistanceText = "未读取";
let serialDryRunConfirmed = false;
let serialTrackConfigured = false;
const serialSafeSideCm = { LEFT: 5, RIGHT: 5 };
let patrolWaypointIndex = 0;
const PATROL_TAGS = ["A", "B", "C", "D"];
const pendingSceneMotions = new Map();
let activeSceneMotion = null;
let manualRobotRevision = 0;
let lastFrameAt = performance.now();
let patrolStartedAt = 0;
let interactionPhase = "IDLE";
let autoControlEnabled = false;
let autoDecisionVersion = 0;
let autoBusy = false;
let activeAutoAction = null;
let nextAutoDecisionAt = 0;
let obstacleBlockedUntil = 0;
let resumePatrolAfterInteraction = false;
let initialLookPending = false;
// Touch-burst debouncing: a cat lands several swipes per attack episode; each press is
// still recorded as evidence, but the RESPONSE waits for a quiet window so one burst
// produces exactly one response instead of a response per swipe.
let pendingTouchBurst = null;
let touchBurstTimer = null;
let touchDuringBehavior = false;
let nextObstacleStatusQueryAt = 0;
const unoTouches = new Set();
let faceReady = false;
const BEHAVIOR_LABELS = {
  PATROL: "巡游", STAY: "保持静止", STAY_SIGNAL: "静止＋声光回应", SLOW_APPROACH: "缓慢接近",
  PROBE_APPROACH: "走停试探", FAST_FORWARD: "快速前进",
  SLOW_STRAIGHT_ESCAPE: "缓慢直线逃跑", FAST_STRAIGHT_ESCAPE: "快速直线逃跑",
  CAPTURE_WIGGLE: "原地轻摇，模拟被抓",
  SEARCH_CAT_START: "寻找猫", ATTACK_RESPONSE: "触碰后找猫并回应", OBSERVE: "观察猫", END_SESSION: "结束互动",
};

function explainReason(reason) {
  if (reason.startsWith("attack_touch_")) {
    const area = /^attack_touch_(TOP|LEFT|RIGHT|REAR)/.exec(reason)?.[1] || "未知位置";
    const count = /count=(\d+)/.exec(reason)?.[1] || "?";
    const draw = /random=([\d.]+)/.exec(reason)?.[1];
    const response = /response=(STAY_SIGNAL|SLOW_STRAIGHT_ESCAPE|FAST_STRAIGHT_ESCAPE)/.exec(reason)?.[1];
    const direction = area === "LEFT" ? "向左" : area === "RIGHT" ? "向右" : area === "REAR" ? "向后" : "旋转";
    const action = response === "SLOW_STRAIGHT_ESCAPE" ? "缓慢逃跑再面向猫"
      : response === "FAST_STRAIGHT_ESCAPE" ? "快速逃跑再面向猫" : "面向猫做声光回应";
    return `Uno 回报第 ${count} 次触碰（${area}）；松开后${direction}找猫，看到即停并对准，最多 360°；随后${action}${draw ? `；随机数 ${draw}` : ""}`;
  }
  if (reason.startsWith("mood_alert_")) return `外部猫状态告警：${reason.endsWith("CAMERA_NEGATIVE") ? "摄像头判断" : "HTTP 判断"}`;
  if (reason.startsWith("cat_left_for_")) return `猫自行离开，并持续远离 ${/cat_left_for_(\d+)s/.exec(reason)?.[1] || "?"} 秒`;
  if (reason.startsWith("cat_inactive_for_")) return `猫在 ${/cat_inactive_for_(\d+)s/.exec(reason)?.[1] || "?"} 秒内没有再次触碰，恢复巡游`;
  if (reason.startsWith("cat_ending ")) {
    const trigger = /trigger=([^ ]+)/.exec(reason)?.[1];
    const outcome = /outcome=([^ ]+)/.exec(reason)?.[1];
    const label = ({ CONTINUE_PATROL: "直接恢复巡游", PREY_STILL: "安静地倒下",
      PREY_SIGNAL: "带声光地倒下" })[outcome] || outcome;
    return `猫主动互动结束（${trigger === "rounds" ? "达到互动轮数" : trigger === "inactivity" ? "无新触碰超时" : "达到时长上限"}）；抽中：${label}`;
  }
  if (reason.startsWith("cat_touch_follow_up")) return "猫主动触碰后的独立后续回应概率";
  if (reason.startsWith("session_limit_")) return `达到设定的互动时长：${reason.split("_").at(-1)} 秒`;
  if (reason.startsWith("patrol_no_alert")) return "尚无猫状态告警或 Uno 触碰，继续巡游";
  if (reason.startsWith("cat_visible_distance_cm=")) {
    const distance = /^cat_visible_distance_cm=([\d.]+)/.exec(reason)?.[1] || "?";
    const roll = /random=([\d.]+)/.exec(reason)?.[1];
    return `模拟摄像头发现猫，约 ${distance} cm；随机数 ${roll ?? "—"}；候选 ${/candidates=([^ ]+)/.exec(reason)?.[1] || "—"}`;
  }
  if (reason === "cat_is_leaving") return "猫正在离开，停止追逐并观察";
  if (reason.startsWith("cat_visible_near_distance_cm=")) return `模拟摄像头发现近处的猫，距离约 ${reason.split("=")[1]} cm`;
  return ({ cat_outside_camera: "猫不在模拟摄像头视野内", cat_retreated_for_5s: "猫持续远离 5 秒",
    session_limit_60s: "互动达到 60 秒上限", uno_obstacle_or_invalid_distance: "Uno 回报障碍物或距离无效",
    manual_button: "用户点击顶层动作", manual_cloud_stub: "人工注入的云端 HTTP 建议" })[reason] || reason;
}

function showDecision(action, source, reason) {
  faceReady = false;
  $("decisionInput").textContent = source === "ESP32_AUTO" ? "传感器与状态数据"
    : source === "SIM_HTTP" ? "模拟 HTTP 建议" : "人工指令";
  $("decisionAction").textContent = `${BEHAVIOR_LABELS[action] || action} (${action})`;
  $("decisionReason").textContent = explainReason(reason);
  $("faceCatStatus").textContent = action === "PATROL" ? "巡游中；尚未进入互动取景" : "动作进行中，等待收尾对准猫";
  addLog("event", `TOP_LEVEL_DECISION action=${action} source=${source} reason=${reason}`, "ESP32·SIM");
}

function timestamp() { return new Date().toLocaleTimeString("zh-CN", { hour12: false }); }
function resetSerialOutputs() {
  pendingSerialOutputs.clear();
  Object.assign(serialOutputs, {
    led: "OFF", flashStartedAt: 0, flashIntervalMs: 0, flashUntil: 0, buzzerUntil: 0,
  });
}

function resetSerialSafeDistances() {
  pendingSafeDistance.clear();
  serialSafeSideCm.LEFT = 5;
  serialSafeSideCm.RIGHT = 5;
}

function receiveSerialOutputs(line, commandDone, error) {
  const now = performance.now();
  const status = /^DATA \d+ STATUS \S+ .*\bLED=(OFF|ON|FLASH)\b.*\bBUZZER=(OFF|ON)\b/.exec(line);
  if (status) {
    serialOutputs.led = status[1];
    if (status[1] !== "FLASH") serialOutputs.flashUntil = 0;
    else if (serialOutputs.flashUntil <= now) {
      serialOutputs.flashStartedAt = now;
      serialOutputs.flashIntervalMs = 220;
      serialOutputs.flashUntil = Infinity;
    }
    if (status[2] === "OFF") serialOutputs.buzzerUntil = 0;
    else if (serialOutputs.buzzerUntil <= now) serialOutputs.buzzerUntil = now + 5000;
  }
  if (error) pendingSerialOutputs.delete(Number(error[1]));
  if (!commandDone) return;
  const id = Number(commandDone[1]);
  const command = pendingSerialOutputs.get(id);
  pendingSerialOutputs.delete(id);
  const suffix = commandDone[2];
  if (suffix === "LED_ON" || suffix === "LED_OFF") {
    serialOutputs.led = suffix === "LED_ON" ? "ON" : "OFF";
    serialOutputs.flashUntil = 0;
  } else if (suffix === "LED_FLASH_STARTED" && command) {
    const [, interval, duration] = /^LED FLASH (\d+) (\d+)$/.exec(command) || [];
    if (interval && duration) {
      serialOutputs.led = "FLASH";
      serialOutputs.flashStartedAt = now;
      serialOutputs.flashIntervalMs = Number(interval);
      serialOutputs.flashUntil = now + Number(duration);
    }
  } else if (suffix === "BEEP_STARTED" && command) {
    const duration = /^BEEP (\d+)$/.exec(command)?.[1];
    if (duration) serialOutputs.buzzerUntil = now + Number(duration);
  } else if (suffix === "SESSION_STOPPED") {
    resetSerialOutputs();
  }
}

function robotVisualState(now) {
  const isMock = mode === "mock";
  const clock = isMock ? mock.now : now;
  const outputs = isMock ? mock : serialOutputs;
  const led = outputs.led === "FLASH" && clock >= (isMock ? mock.ledUntil : serialOutputs.flashUntil)
    ? "OFF" : outputs.led;
  const flashStartedAt = isMock ? mock.ledFlashStartedAt : serialOutputs.flashStartedAt;
  const flashIntervalMs = isMock ? mock.ledInterval : serialOutputs.flashIntervalMs;
  const ledLit = led === "ON" || (led === "FLASH" && flashIntervalMs > 0 &&
    Math.floor((clock - flashStartedAt) / flashIntervalMs) % 2 === 0);
  return {
    led, ledLit,
    buzzerOn: clock < outputs.buzzerUntil,
    moving: isMock ? mock.state === "MOVING" : realState === "MOVING",
  };
}

function addLog(type, text, origin = mode.toUpperCase()) {
  const item = { timestamp: new Date().toISOString(), type, origin, text };
  trace.push(item);
  if (trace.length > 50000) trace.shift();
  const row = document.createElement("div");
  row.className = `log-entry ${type}`;
  const time = document.createElement("span");
  time.className = "log-time";
  time.textContent = timestamp();
  const kind = document.createElement("span");
  kind.className = "log-type";
  kind.textContent = origin;
  const message = document.createElement("span");
  message.className = "log-text";
  message.textContent = text;
  row.append(time, kind, message);
  $("log").append(row);
  while ($("log").children.length > 300) $("log").firstChild.remove();
  $("log").scrollTop = $("log").scrollHeight;
}

function showToast(message) {
  const toast = $("toast");
  toast.textContent = message;
  toast.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { toast.hidden = true; }, 4200);
}

function clearManualTouchPulses(release = false) {
  for (const timer of manualTouchTimers.values()) clearTimeout(timer);
  manualTouchTimers.clear();
  if (release) for (const area of manualTouchAreas) send(`DEBUG_INJECT TOUCH ${area} RELEASE`);
  manualTouchAreas.clear();
  document.querySelectorAll("[data-touch]").forEach(button => button.setAttribute("aria-pressed", "false"));
}

function resumeSimulation(reason) {
  if (!paused) return;
  paused = false;
  lastFrameAt = performance.now();
  $("pauseSim").textContent = "暂停模拟";
  addLog("event", `SIM_RESUMED reason=${reason}`, "SYSTEM");
}

function sceneMotionFromCommand(command) {
  const [verb, a, b] = command.split(" ");
  if (verb === "MOTOR") return { left: Number(a) / 255, right: Number(b) / 255 };
  if (verb === "DRIVE") {
    const speed = Math.sign(Number(a)) * Number(b) / 255;
    return { left: speed, right: speed, kind: "DRIVE", target: Number(a) };
  }
  if (verb === "ROTATE") {
    const speed = Math.sign(Number(a)) * Number(b) / 255;
    return { left: speed, right: -speed, kind: "ROTATE", target: Number(a) };
  }
  const words = command.split(" ");
  if (verb === "MOVE") {
    const speed = (a === "FORWARD" ? 1 : -1) * Number(words[2]) / 255;
    return { left: speed, right: speed };
  }
  if (verb === "TURN") {
    const speed = (a === "RIGHT" ? 1 : -1) * Number(words[2]) / 255;
    return { left: speed, right: -speed };
  }
  return null;
}

function stepSerialSceneMotion(elapsedSeconds) {
  if (!activeSceneMotion || activeSceneMotion.haltedByCommand) return;
  const seconds = clamp(elapsedSeconds, 0, .05);
  const wheelMm = Number($("wheelDiameterMm").value) || 65;
  const trackMm = Number($("trackWidthMm").value) || 120;
  const fullSpeedPxS = 200 / 60 * Math.PI * wheelMm / 10 * ARENA.pxPerCm;
  const leftSpeed = activeSceneMotion.left * fullSpeedPxS;
  const rightSpeed = activeSceneMotion.right * fullSpeedPxS;
  const rotation = (leftSpeed - rightSpeed) / (trackMm / 10 * ARENA.pxPerCm);
  world.robot.heading = wrapAngle(world.robot.heading + rotation * seconds);
  const travel = (leftSpeed + rightSpeed) / 2 * seconds;
  world.robot.x = clamp(world.robot.x + Math.cos(world.robot.heading) * travel,
    ARENA.robotRadius, ARENA.width - ARENA.robotRadius);
  world.robot.y = clamp(world.robot.y + Math.sin(world.robot.heading) * travel,
    ARENA.robotRadius, ARENA.height - ARENA.robotRadius);
}

function reconcileSceneMotion(motion, line, completed) {
  if (!motion || motion.manualRobotRevision !== manualRobotRevision ||
      (motion.kind !== "DRIVE" && motion.kind !== "ROTATE")) return;
  const counts = /\bLEFT_PULSES=(\d+) RIGHT_PULSES=(\d+)/.exec(line);
  const wheelCm = (Number($("wheelDiameterMm").value) || 65) / 10;
  const trackCm = (Number($("trackWidthMm").value) || 120) / 10;
  let leftCm;
  let rightCm;
  if (counts) {
    const cmPerPulse = Math.PI * wheelCm / 20;
    leftCm = Math.sign(motion.left) * Number(counts[1]) * cmPerPulse;
    rightCm = Math.sign(motion.right) * Number(counts[2]) * cmPerPulse;
  } else if (completed && motion.kind === "DRIVE") {
    leftCm = rightCm = motion.target;
  } else if (completed && motion.kind === "ROTATE") {
    leftCm = trackCm * radians(motion.target) / 2;
    rightCm = -leftCm;
  } else return;
  const distanceCm = (leftCm + rightCm) / 2;
  const rotation = (leftCm - rightCm) / trackCm;
  const start = motion.startPose;
  const heading = start.heading + rotation;
  const travelPx = distanceCm * ARENA.pxPerCm;
  if (Math.abs(rotation) < 1e-6) {
    world.robot.x = clamp(start.x + Math.cos(start.heading) * travelPx, ARENA.robotRadius, ARENA.width - ARENA.robotRadius);
    world.robot.y = clamp(start.y + Math.sin(start.heading) * travelPx, ARENA.robotRadius, ARENA.height - ARENA.robotRadius);
  } else {
    const radiusPx = travelPx / rotation;
    world.robot.x = clamp(start.x + radiusPx * (Math.sin(heading) - Math.sin(start.heading)), ARENA.robotRadius, ARENA.width - ARENA.robotRadius);
    world.robot.y = clamp(start.y - radiusPx * (Math.cos(heading) - Math.cos(start.heading)), ARENA.robotRadius, ARENA.height - ARENA.robotRadius);
  }
  world.robot.heading = wrapAngle(heading);
}

function receiveLine(rawLine, source) {
  const line = rawLine.trim();
  if (!line) return;
  let type = line.startsWith("ERROR") ? "error" : line.startsWith("EVENT") ? "event" : "response";
  addLog(type, line, source);
  const motionDone = /^EVENT MOTION_DONE (\d+)/.exec(line);
  const motionAborted = /^EVENT MOTION_ABORTED (\d+) (\S+)/.exec(line);
  const commandDone = /^DONE (\d+)(?: (\S+))?/.exec(line);
  const error = /^ERROR (\d+) (.+)/.exec(line);
  if (activeScanMotion) {
    if (motionAborted && activeScanMotion.commandIds.has(Number(motionAborted[1])) && motionAborted[2] !== "REPLACED") {
      activeScanMotion.abortReason = motionAborted[2];
    }
    if (error && activeScanMotion.commandIds.has(Number(error[1]))) activeScanMotion.abortReason = error[2];
  }
  const touchEvent = /^EVENT \S+ TOUCH (TOP|LEFT|RIGHT|REAR) (PRESS|RELEASE)/.exec(line);
  if (touchEvent) {
    addLog("event", `UNO_TO_ESP32 TOUCH area=${touchEvent[1]} phase=${touchEvent[2]}`, "ESP32·SIM");
    if (touchEvent[2] === "PRESS") {
      unoTouches.add(touchEvent[1]);
      lastTouchSide = touchEvent[1];
      lastTouchAt = Date.now();
      onUnoTouchPress(touchEvent[1]);
    } else {
      unoTouches.delete(touchEvent[1]);
    }
  }
  if (source === "SERIAL") {
    receiveSerialOutputs(line, commandDone, error);
    if (line.startsWith("READY ")) {
      resetSerialOutputs();
      resetSerialSafeDistances();
      if (!/^READY UNO_CAT_V5(\.\d+)?$/.test(line)) {
        addLog("error", "FIRMWARE_UPDATE_REQUIRED expected=UNO_CAT_V5.1", "SYSTEM");
        showToast("Uno 固件需要更新；请烧录当前 UnoCatController.ino，重连后确认 READY UNO_CAT_V5.1。");
      } else if (line === "READY UNO_CAT_V5") {
        addLog("event", "FIRMWARE_LEGACY note=建议烧录 V5.1（串口收发包互不阻塞加固）", "SYSTEM");
      }
      unoTouches.clear();
      clearManualTouchPulses();
      lastSerialAutoArea = null;
      serialTrackConfigured = false;
      serialDryRunConfirmed = false;
      sceneSensors.reset();
      activeSceneMotion = null;
      realDistanceText = "未读取";
      realLeftDistanceText = realRightDistanceText = "未读取";
      $("streamSceneSensors").checked = false;
      if (autoControlEnabled || interactionPhase !== "IDLE") {
        autoControlEnabled = false;
        interactionPhase = "IDLE";
        autoDecisionVersion += 1;
        autoBusy = false;
        cancelBehavior("UNO_RESET");
        addLog("event", "DECISION_STOP reason=UNO_RESET", "ESP32·SIM");
      }
      if (/^READY UNO_CAT_V5/.test(line)) send("GET_CONFIG");
    }
    const state = /DATA \d+ STATUS (\S+)/.exec(line);
    const config = /DATA \d+ CONFIG WHEEL_MM=(\d+(?:\.\d+)?) TRACK_MM=(UNSET|\d+(?:\.\d+)?)/.exec(line);
    const safeConfig = /DATA \d+ CONFIG .*\bSAFE_LEFT_CM=(\d+(?:\.\d+)?) SAFE_RIGHT_CM=(\d+(?:\.\d+)?)/.exec(line);
    const measuredDistance = /^DATA \d+ DISTANCE (INVALID|\d+(?:\.\d+)?)/.exec(line);
    const measuredSide = /^DATA \d+ SIDE_(LEFT|RIGHT) (INVALID|\d+(?:\.\d+)?)/.exec(line);
    if (state) {
      realState = state[1];
      const wasDryRunConfirmed = serialDryRunConfirmed;
      serialDryRunConfirmed = /DEBUG=ON/.test(line) && /MOTOR_MODE=DRY_RUN/.test(line);
      if (!wasDryRunConfirmed && serialDryRunConfirmed && $("streamSceneSensors").checked) {
        sceneSensors.reset();
        streamSceneSensors(performance.now(), true);
        send("GET_DISTANCE");
      }
    }
    if (config) {
      serialTrackConfigured = Number(config[2]) > 0;
      $("wheelDiameterMm").value = config[1];
      if (serialTrackConfigured) $("trackWidthMm").value = config[2];
    }
    if (safeConfig) {
      serialSafeSideCm.LEFT = Number(safeConfig[1]);
      serialSafeSideCm.RIGHT = Number(safeConfig[2]);
      $("safeLeftCm").value = safeConfig[1];
      $("safeRightCm").value = safeConfig[2];
    }
    if (/^DONE \d+ GEOMETRY_SET$/.test(line)) serialTrackConfigured = true;
    if (commandDone) {
      const id = Number(commandDone[1]);
      const pending = pendingSafeDistance.get(id);
      pendingSafeDistance.delete(id);
      if (pending && commandDone[2] === "SAFE_DISTANCE_SET") serialSafeSideCm[pending.side] = pending.cm;
    }
    if (error) {
      const id = Number(error[1]);
      if (pendingSafeDistance.has(id)) showToast(`Uno 未更新安全距离：${error[2]}`);
      pendingSafeDistance.delete(id);
    }
    if (/^DONE \d+ SESSION_STARTED$/.test(line)) realState = "IDLE";
    if (measuredDistance) realDistanceText = measuredDistance[1] === "INVALID"
      ? "无效" : `${measuredDistance[1]} cm`;
    if (measuredSide) {
      const shown = measuredSide[2] === "INVALID" ? "无效" : `${measuredSide[2]} cm`;
      if (measuredSide[1] === "LEFT") realLeftDistanceText = shown;
      else realRightDistanceText = shown;
    }
    const motionStarted = /^EVENT MOTION_STARTED (\d+)/.exec(line);
    if (motionStarted) {
      const id = Number(motionStarted[1]);
      const sceneMotion = pendingSceneMotions.get(id);
      activeSceneMotion = sceneMotion ? {
        ...sceneMotion, id, startPose: { ...world.robot }, manualRobotRevision,
      } : null;
      pendingSceneMotions.delete(id);
    }
    if (line.includes("MOTION_STARTED")) realState = "MOVING";
    if (motionDone || motionAborted) {
      if (activeSceneMotion?.id === Number((motionDone || motionAborted)[1])) {
        reconcileSceneMotion(activeSceneMotion, line, Boolean(motionDone));
      }
      realState = motionAborted?.[2] === "REPLACED" ? "MOVING" : "IDLE";
      activeSceneMotion = null;
    }
    if (/^DONE \d+ SESSION_STOPPED$/.test(line)) {
      realState = "STOPPED";
      activeSceneMotion = null;
      sceneSensors.reset(); // SESSION_STOP clears the Uno's injected sensor values.
      lastSerialAutoArea = null;
    }
  }
  if (motionDone) settleMotion(Number(motionDone[1]), null);
  else if (motionAborted) settleMotion(Number(motionAborted[1]), new Error(motionAborted[2]));
  else if (error) settleMotion(Number(error[1]), new Error(error[2]));
  if (commandDone) settleCommandDone(Number(commandDone[1]), commandDone[2] || "");
  else if (error) settleCommandDone(Number(error[1]), null, new Error(error[2]));
  if (source === "SERIAL" && error) pendingSceneMotions.delete(Number(error[1]));
  if (source === "SERIAL" && error?.[2] === "DEBUG_DISABLED") {
    serialDryRunConfirmed = false;
    $("streamSceneSensors").checked = false;
  }
  if (source === "SERIAL" && /^(FRONT_OBSTACLE_OR_CAMERA_INVALID|SIDE_(LEFT|RIGHT)_OBSTACLE_OR_SENSOR_INVALID)$/.test(error?.[2] || "")) {
    obstacleBlockedUntil = performance.now() + 5000;
    if (performance.now() >= nextObstacleStatusQueryAt) {
      showToast(`Uno 拒绝动作：${error[2]}。请核对摄像头与左右超声波读数。`);
      send("GET_DISTANCE");
      nextObstacleStatusQueryAt = performance.now() + 1000;
    }
  }
  if (line.startsWith("EVENT OBSTACLE_NEAR")) obstacleBlockedUntil = performance.now() + 5000;
  if (activeSearch && (/^EVENT OBSTACLE_NEAR FRONT/.test(line) || /^EVENT MOTION_ABORTED \d+ (FRONT_OBSTACLE_OR_CAMERA_INVALID|ENCODER_STALL)/.test(line))) {
    activeSearch.stopReason = "UNO_SAFETY_STOP";
    cancelBehavior("UNO_SAFETY_STOP");
    send("STOP");
  }
}

const mock = new MockUno(world, line => receiveLine(line, "SIM_UNO"));

function settleMotion(id, error) {
  const item = pendingMotion.get(id);
  if (!item) return;
  clearTimeout(item.timeout);
  pendingMotion.delete(id);
  if (error) item.reject(error);
  else item.resolve();
}

function settleCommandDone(id, suffix, error = null) {
  const pending = pendingCommandDone.get(id);
  if (!pending) return;
  clearTimeout(pending.timeout);
  pendingCommandDone.delete(id);
  if (error) pending.reject(error);
  else if (suffix !== pending.expectedSuffix) pending.reject(new Error(`UNEXPECTED_DONE_${suffix}`));
  else pending.resolve();
}

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

// Transport hardening: serial bursts used to overflow the Uno's 64-byte RX ring
// (injection + motion written in the same millisecond while the firmware blocks on
// Serial.print), which fragmented the last line of every escape. All serial writes now
// go through one FIFO with a minimum gap between lines.
const SERIAL_WRITE_GAP_MS = 15;
let serialWriteTail = Promise.resolve();
let serialLastWriteAt = 0;
function enqueueSerialWrite(line, id) {
  serialWriteTail = serialWriteTail.then(async () => {
    if (!writer) return;
    const wait = SERIAL_WRITE_GAP_MS - (performance.now() - serialLastWriteAt);
    if (wait > 0) await sleep(wait);
    try { await writer.write(new TextEncoder().encode(`${line}\n`)); }
    catch (error) {
      receiveLine(`ERROR ${id} WRITE_FAILED`, "SERIAL");
      showToast(`串口写入失败：${error.message}`);
    }
    serialLastWriteAt = performance.now();
  });
}

// Test-only seam: a wire filter applied before MockUno.receiveWire. Returning a
// mutated line simulates a corrupted frame; returning null simulates a lost frame.
export const testHooks = { wireFault: null };
function applyWireFault(line) {
  return testHooks.wireFault ? testHooks.wireFault(line) : line;
}

// Rejections that prove the Uno never executed the command, so resending is safe.
const MOTION_TRANSPORT_ERRORS = /^(UNKNOWN_OR_INVALID_COMMAND|BAD_ENVELOPE|TOO_MANY_ARGUMENTS|LINE_TOO_LONG|WRITE_FAILED)$/;
async function motionDeviceIdle() {
  send("GET_STATUS");
  await sleep(200);
  return mode === "mock" ? !mock.active : realState !== "MOVING";
}
async function sendMotionWithTransportRetry(command, attempts = 3) {
  let lastError = null;
  for (let attempt = 1; attempt <= attempts; attempt++) {
    try { return await sendMotion(command); }
    catch (error) {
      lastError = error;
      const timeoutButIdle = error.message === "MOTION_TIMEOUT" && await motionDeviceIdle();
      if ((!MOTION_TRANSPORT_ERRORS.test(error.message) && !timeoutButIdle) || attempt === attempts) throw error;
      addLog("event", `MOTION_RETRY command="${command}" next_attempt=${attempt + 1} reason=${error.message}`, "ESP32·SIM");
      await sleep(80);
    }
  }
  throw lastError;
}

// Timestamp of the last completed stationary cat view; END_SESSION uses this to avoid
// holding a second gaze right after the post-escape observation.
let lastViewHoldAt = 0;
const END_SESSION_HOLD_GRACE_MS = 10000;

function send(command, preflight = true) {
  const normalized = command.trim().replace(/\s+/g, " ");
  if (!normalized || normalized.length > 96 || /[\r\n]/.test(command)) {
    showToast("命令为空、过长或包含换行。");
    return null;
  }
  if (paused && sceneMotionFromCommand(normalized) && normalized !== "MOTOR 0 0") {
    resumeSimulation("MOTION_COMMAND");
  }
  const requestedMotion = sceneMotionFromCommand(normalized);
  if (preflight && requestedMotion && normalized !== "MOTOR 0 0")
    streamSceneSensors(performance.now(), true, requestedMotion);
  const id = nextCommandId++;
  const line = `CMD ${id} ${normalized}`;
  if (mode === "serial") {
    if (/^(LED |BEEP |SESSION_STOP$)/.test(normalized)) pendingSerialOutputs.set(id, normalized);
    const safeDistance = /^SET_SAFE_DISTANCE (LEFT|RIGHT) (\d+(?:\.\d+)?)$/.exec(normalized);
    if (safeDistance) pendingSafeDistance.set(id, { side: safeDistance[1], cm: Number(safeDistance[2]) });
    if (normalized === "DEBUG_MOTOR_MODE LIVE" || normalized === "DEBUG_MODE OFF") {
      serialDryRunConfirmed = false;
      $("streamSceneSensors").checked = false;
    }
    const sceneMotion = sceneMotionFromCommand(normalized);
    if (sceneMotion && normalized !== "MOTOR 0 0") pendingSceneMotions.set(id, sceneMotion);
    if (normalized === "STOP" || normalized === "SESSION_STOP" || normalized === "MOTOR 0 0") {
      if (activeSceneMotion) activeSceneMotion.haltedByCommand = true;
    }
  }
  addLog("command", line, mode === "mock" ? "TX·SIM" : "TX·SERIAL");
  if (mode === "mock") {
    const wireLine = applyWireFault(line);
    if (wireLine !== null) mock.receiveWire(wireLine, performance.now());
  } else if (writer) enqueueSerialWrite(line, id);
  return id;
}

async function sendMotion(command) {
  const motion = sceneMotionFromCommand(command);
  if (mode === "serial" && serialDryRunConfirmed && $("streamSceneSensors").checked) {
    // Confirmed one-at-a-time sync so the motion line never shares the wire with its
    // injections; the Uno must be idle on TX when ROTATE/DRIVE arrive.
    const syncCommands = sceneSensorCommands(performance.now(), true, motion) || [];
    for (const syncCommand of syncCommands) {
      const expectedSuffix = syncCommand.startsWith("CAMERA_FRONT") ? "CAMERA_FRONT_SET" : "";
      try { await sendAndWaitDone(syncCommand, expectedSuffix); }
      catch (error) { addLog("event", `SENSOR_SYNC_FAILED command="${syncCommand}" reason=${error.message}`, "SYSTEM"); }
    }
  } else {
    streamSceneSensors(performance.now(), true, motion);
  }
  // The promise is registered before sending: the mock can return an error synchronously.
  const expectedId = nextCommandId;
  const promise = new Promise((resolve, reject) => {
    const timeout = setTimeout(() => settleMotion(expectedId, new Error("MOTION_TIMEOUT")), 14000);
    pendingMotion.set(expectedId, { resolve, reject, timeout });
  });
  if (send(command, false) === null) settleMotion(expectedId, new Error("INVALID_COMMAND"));
  return promise;
}

function sendAndWaitDone(command, expectedSuffix) {
  const id = nextCommandId;
  const promise = new Promise((resolve, reject) => {
    const timeout = setTimeout(() => settleCommandDone(id, null, new Error("COMMAND_TIMEOUT")), 5000);
    pendingCommandDone.set(id, { resolve, reject, timeout, expectedSuffix });
  });
  if (send(command, false) === null) settleCommandDone(id, null, new Error("INVALID_COMMAND"));
  return promise;
}

function cancelBehavior(reason = "USER_STOP") {
  behaviorToken += 1;
  faceReady = false;
  $("faceCatStatus").textContent = `动作中断：${reason}`;
  for (const [id] of pendingMotion) settleMotion(id, new Error(reason));
}

function logCamera() {
  const camera = cameraObservation(world);
  const description = camera.detected
    ? `cat_detected=true center_x=${camera.centerX.toFixed(2)} distance_est_cm=${camera.distanceCm.toFixed(1)} bbox=${camera.bboxSize.toFixed(2)}`
    : `cat_detected=false relative_bearing_deg=${camera.relativeDeg.toFixed(1)}`;
  addLog("event", `SIM_CAMERA ${description}`, "ESP32·SIM");
}

function sceneTurnBlockReason(direction) {
  const sceneClearanceKnown = mode === "mock" ||
    (mode === "serial" && serialDryRunConfirmed && $("streamSceneSensors").checked);
  return turnObstacleReason({ direction, available: sceneClearanceKnown,
    leftSafeCm: mode === "mock" ? mock.safeSideCm.LEFT : serialSafeSideCm.LEFT,
    rightSafeCm: mode === "mock" ? mock.safeSideCm.RIGHT : serialSafeSideCm.RIGHT,
    leftCm: mode === "mock" ? mock.sideDistanceCm("LEFT")
      : sceneClearanceKnown ? sideSonarDistanceCm(world, "LEFT") : null,
    rightCm: mode === "mock" ? mock.sideDistanceCm("RIGHT")
      : sceneClearanceKnown ? sideSonarDistanceCm(world, "RIGHT") : null });
}

async function scanUntilVisible(token, observe, { direction = 1, pwm = 75, timeoutMs = Infinity,
  maxSweepDeg = 360, active = () => true, allowReverse = true } = {}) {
  const scan = { commandIds: new Set(), abortReason: null };
  activeScanMotion = scan;
  let lastHeading = world.robot.heading;
  let swept = 0;
  let lastCommandAt = -Infinity;
  let motorStarted = false;
  let turnDirection = direction;
  let reversed = false;
  let blockedReason = null;
  let deadline = performance.now() + timeoutMs;
  try {
    while (token === behaviorToken && active()) {
      const now = performance.now();
      swept += Math.abs(wrapAngle(world.robot.heading - lastHeading));
      lastHeading = world.robot.heading;
      if (scan.abortReason) {
        if (scan.abortReason === "TOUCH") {
          scan.abortReason = null;
          // Time spent pinned by the cat's grab does not consume the sweep budget.
          const resumeAt = performance.now();
          const released = await waitForTouchRelease(token, decisionSystem.responsePolicy.touchReleaseTimeoutMs);
          deadline += performance.now() - resumeAt;
          if (!released)
            return { found: false, reason: token === behaviorToken ? "TOUCH_HELD" : "CANCELLED", sweptDeg: degrees(swept) };
          lastCommandAt = -Infinity;
          continue;
        }
        if (/^SIDE_(LEFT|RIGHT)_OBSTACLE_OR_SENSOR_INVALID$/.test(scan.abortReason)) {
          const reason = scan.abortReason;
          scan.abortReason = null;
          blockedReason = reason;
          if (allowReverse && !reversed) {
            turnDirection *= -1;
            reversed = true;
            addLog("event", `SCAN_REVERSE reason=${reason} direction=${turnDirection < 0 ? "LEFT" : "RIGHT"}`, "ESP32·SIM");
          }
          lastCommandAt = -Infinity;
        } else {
          throw new Error(scan.abortReason);
        }
      }
      if (document.visibilityState === "hidden") return { found: false, reason: "PAGE_HIDDEN", sweptDeg: degrees(swept) };
      if (unoTouches.size) {
        // Holding position while the cat grips the robot: pause the sweep timer too.
        const resumeAt = performance.now();
        await new Promise(resolve => setTimeout(resolve, 25));
        deadline += performance.now() - resumeAt;
        continue;
      }
      const observation = observe();
      const scanDecision = decideScanProgress({ detected: observation.detected,
        sweptDeg: degrees(swept), limitDeg: maxSweepDeg, timedOut: now >= deadline });
      if (scanDecision === "FOUND") return { found: true, observation, sweptDeg: degrees(swept) };
      if (scanDecision !== "CONTINUE") return { found: false,
        reason: scanDecision === "TIMEOUT" ? blockedReason || scanDecision : scanDecision,
        sweptDeg: degrees(swept) };
      if (now - lastCommandAt >= 350) {
        const currentBlock = sceneTurnBlockReason(turnDirection);
        if (currentBlock) {
          blockedReason = currentBlock;
          if (!allowReverse || reversed) {
            await new Promise(resolve => setTimeout(resolve, 25));
            continue;
          }
          turnDirection *= -1;
          reversed = true;
          addLog("event", `SCAN_REVERSE reason=${currentBlock} direction=${turnDirection < 0 ? "LEFT" : "RIGHT"}`, "ESP32·SIM");
          if (sceneTurnBlockReason(turnDirection))
            return { found: false, reason: "BOTH_SIDES_BLOCKED", sweptDeg: degrees(swept) };
        }
        blockedReason = null;
        streamSceneSensors(now, true, { left: turnDirection * pwm, right: -turnDirection * pwm });
        scan.commandIds.add(nextCommandId);
        send(`MOTOR ${turnDirection * pwm} ${-turnDirection * pwm}`, false);
        motorStarted = true;
        lastCommandAt = now;
      }
      await new Promise(resolve => setTimeout(resolve, 25));
    }
    return { found: false, reason: "CANCELLED", sweptDeg: degrees(swept) };
  } finally {
    if (activeScanMotion === scan) activeScanMotion = null;
    if (motorStarted && token === behaviorToken) send("STOP");
  }
}

async function searchCat(token, timeoutMs = decisionSystem.responsePolicy.searchTimeoutMs, maxSweepDeg = 360, direction = null) {
  const search = { token, stopReason: "CANCELLED" };
  activeSearch = search;
  const sweepLimitDeg = Number.isFinite(maxSweepDeg) ? clamp(maxSweepDeg, 0, 360) : 360;
  const scanDirection = direction ?? (lastTouchSide === "LEFT" ? -1 : 1);
  addLog("event", `SEARCH_CAT_START camera=SIM_CAMERA sweep_limit_deg=${sweepLimitDeg} direction=${scanDirection < 0 ? "LEFT" : "RIGHT"}`, "ESP32·SIM");
  try {
    const result = await scanUntilVisible(token, () => cameraObservation(world), {
      direction: scanDirection, pwm: decisionSystem.responsePolicy.searchPwm,
      timeoutMs, maxSweepDeg: sweepLimitDeg,
      allowReverse: direction === null,
    });
    if (result.found) {
      addLog("event", `SEARCH_CAT_STOP reason=CAT_DETECTED center_x=${result.observation.centerX.toFixed(2)} sweep_deg=${result.sweptDeg.toFixed(1)}`, "ESP32·SIM");
      logCamera();
      return { found: true, reason: "CAT_DETECTED" };
    }
    const reason = token === behaviorToken ? result.reason : search.stopReason;
    addLog("event", `SEARCH_CAT_STOP reason=${reason} sweep_deg=${result.sweptDeg.toFixed(1)}`, "ESP32·SIM");
    return { found: false, reason };
  } catch (error) {
    addLog("error", `SEARCH_CAT_STOP reason=${error.message}`, "ESP32·SIM");
    throw error;
  } finally {
    if (activeSearch === search) activeSearch = null;
  }
}

async function alignToCat(token) {
  addLog("event", "ALIGN_CAT_START after_search_stop", "ESP32·SIM");
  const deadline = performance.now() + decisionSystem.responsePolicy.alignTimeoutMs;
  let turnDirection = 0;
  let lastTurnCommandAt = 0;
  let lastBlockedReason = null;
  const turn = { commandIds: new Set(), abortReason: null };
  activeScanMotion = turn;
  try {
    while (token === behaviorToken && performance.now() < deadline) {
      if (turn.abortReason) {
        const reason = turn.abortReason;
        turn.abortReason = null;
        if (turnDirection) send("STOP");
        turnDirection = 0;
        lastTurnCommandAt = 0;
        if (reason === "TOUCH") {
          if (!await waitForTouchRelease(token, decisionSystem.responsePolicy.touchReleaseTimeoutMs)) return false;
          continue;
        }
        if (/^SIDE_(LEFT|RIGHT)_OBSTACLE_OR_SENSOR_INVALID$/.test(reason)) {
          lastBlockedReason = reason;
          await new Promise(resolve => setTimeout(resolve, 25));
          continue;
        }
        addLog("error", `ALIGN_CAT_STOP reason=${reason}`, "ESP32·SIM");
        return false;
      }
      if (unoTouches.size) {
        await new Promise(resolve => setTimeout(resolve, 25));
        continue;
      }
      const camera = cameraObservation(world);
      const alignment = decideTargetAlignment(camera, decisionSystem.responsePolicy.centerTolerancePercent / 100);
      if (alignment.action === "LOST") {
        if (turnDirection) send("STOP");
        addLog("event", "ALIGN_CAT_STOP reason=CAT_LOST", "ESP32·SIM");
        return false;
      }
      if (alignment.action === "CENTERED") {
        if (turnDirection) send("STOP");
        addLog("event", `ALIGN_CAT_STOP reason=CENTERED center_x=${camera.centerX.toFixed(2)}`, "ESP32·SIM");
        return true;
      }
      const direction = alignment.direction;
      const blockedReason = sceneTurnBlockReason(direction);
      if (blockedReason) {
        if (turnDirection) send("STOP");
        turnDirection = 0;
        lastBlockedReason = blockedReason;
        await new Promise(resolve => setTimeout(resolve, 25));
        continue;
      }
      lastBlockedReason = null;
      if (direction !== turnDirection || performance.now() - lastTurnCommandAt >= 350) {
        turn.commandIds.add(nextCommandId);
        const pwm = decisionSystem.responsePolicy.alignPwm;
        send(`MOTOR ${direction * pwm} ${-direction * pwm}`);
        turnDirection = direction;
        lastTurnCommandAt = performance.now();
      }
      await new Promise(resolve => setTimeout(resolve, 25));
    }
    if (token === behaviorToken) {
      if (turnDirection) send("STOP");
      addLog("event", `ALIGN_CAT_STOP reason=${lastBlockedReason || "TIMEOUT"}`, "ESP32·SIM");
    }
    return false;
  } finally {
    if (activeScanMotion === turn) activeScanMotion = null;
  }
}

async function behaviorPause(durationMs, token) {
  addLog("event", `PAUSE ${durationMs}ms`, "ESP32·SIM");
  await new Promise(resolve => setTimeout(resolve, durationMs));
  return token === behaviorToken;
}

async function holdCatView(token) {
  const seconds = decisionSystem.responsePolicy.initialLookSeconds;
  if (seconds === 0) { lastViewHoldAt = performance.now(); return true; }
  send("STOP");
  $("faceCatStatus").textContent = `面向猫静止取景 ${seconds} 秒（模拟）`;
  addLog("event", `CAT_VIEW_HOLD_START duration_s=${seconds} stationary=true`, "ESP32·SIM");
  const deadline = performance.now() + seconds * 1000;
  while (token === behaviorToken && performance.now() < deadline) {
    if (!cameraObservation(world).detected) {
      addLog("error", "CAT_VIEW_HOLD_STOP reason=CAT_LOST", "ESP32·SIM");
      return false;
    }
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  if (token !== behaviorToken) return false;
  lastViewHoldAt = performance.now();
  addLog("event", `CAT_VIEW_HOLD_DONE duration_s=${seconds} photo_taken=false`, "ESP32·SIM");
  return true;
}

async function waitForCatPursuit(token) {
  const seconds = decisionSystem.responsePolicy.postEscapeWaitSeconds;
  if (seconds === 0) return;
  send("STOP");
  $("faceCatStatus").textContent = `逃跑后停下，等待猫再次触碰（最多 ${seconds} 秒）`;
  addLog("event", `CHASE_WAIT_START duration_s=${seconds} stationary=true`, "ESP32·SIM");
  const deadline = performance.now() + seconds * 1000;
  while (token === behaviorToken && !touchDuringBehavior && performance.now() < deadline)
    await new Promise(resolve => setTimeout(resolve, 100));
  if (token === behaviorToken)
    addLog("event", `CHASE_WAIT_DONE reason=${touchDuringBehavior ? "TOUCH" : "TIMEOUT"}`, "ESP32·SIM");
}

async function waitForTouchRelease(token, timeoutMs) {
  const deadline = performance.now() + timeoutMs;
  while (unoTouches.size && token === behaviorToken && performance.now() < deadline) {
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  return token === behaviorToken && unoTouches.size === 0;
}

function tagObservation(name) {
  return cameraTargetObservation(world, world.tags[name]);
}

async function alignToTag(name, token) {
  const deadline = performance.now() + 5000;
  let turnDirection = 0;
  let lastTurnCommandAt = 0;
  const turn = { commandIds: new Set(), abortReason: null };
  activeScanMotion = turn;
  try {
    while (token === behaviorToken && interactionPhase === "PATROL" && performance.now() < deadline) {
      if (turn.abortReason) {
        if (turnDirection) send("STOP");
        throw new Error(turn.abortReason);
      }
      const tag = tagObservation(name);
      const alignment = decideTargetAlignment(tag, .06);
      if (alignment.action === "LOST") {
        if (turnDirection) send("STOP");
        return false;
      }
      if (alignment.action === "CENTERED") {
        if (turnDirection) send("STOP");
        return true;
      }
      const direction = alignment.direction;
      const blockedReason = sceneTurnBlockReason(direction);
      if (blockedReason) {
        if (turnDirection) send("STOP");
        throw new Error(blockedReason);
      }
      if (direction !== turnDirection || performance.now() - lastTurnCommandAt >= 350) {
        turn.commandIds.add(nextCommandId);
        send(`MOTOR ${direction * 55} ${-direction * 55}`);
        turnDirection = direction;
        lastTurnCommandAt = performance.now();
      }
      await new Promise(resolve => setTimeout(resolve, 25));
    }
    if (turnDirection && token === behaviorToken) send("STOP");
    return false;
  } finally {
    if (activeScanMotion === turn) activeScanMotion = null;
  }
}

async function scanForRouteTag(name, token) {
  addLog("event", `PATROL_SCAN_START tag=${name} sweep_limit_deg=360`, "ESP32·SIM");
  $("routeStatus").textContent = `原地旋转，寻找路线标签 ${name}（最多 360°）`;
  const result = await scanUntilVisible(token, () => tagObservation(name), {
    timeoutMs: 15000, maxSweepDeg: 360,
    active: () => interactionPhase === "PATROL",
  });
  if (token !== behaviorToken || interactionPhase !== "PATROL") return false;
  if (!result.found) {
    addLog("error", `PATROL_SCAN_STOP tag=${name} reason=${result.reason} sweep_deg=${result.sweptDeg.toFixed(1)}`, "ESP32·SIM");
    throw new Error(/^(SIDE_(LEFT|RIGHT)_OBSTACLE_OR_SENSOR_INVALID|BOTH_SIDES_BLOCKED)$/.test(result.reason)
      ? result.reason : "ROUTE_TAG_NOT_FOUND");
  }
  addLog("event", `PATROL_SCAN_STOP tag=${name} reason=TAG_DETECTED sweep_deg=${result.sweptDeg.toFixed(1)}`, "ESP32·SIM");
  return true;
}

async function alignToDetourPoint(point, token) {
  const turn = { commandIds: new Set(), abortReason: null };
  activeScanMotion = turn;
  let turning = false;
  let lastCommandAt = -Infinity;
  const deadline = performance.now() + 5000;
  try {
    while (token === behaviorToken && interactionPhase === "PATROL" && performance.now() < deadline) {
      if (document.visibilityState === "hidden") throw new Error("PAGE_HIDDEN");
      if (turn.abortReason) throw new Error(turn.abortReason);
      const desired = Math.atan2(point.y - world.robot.y, point.x - world.robot.x);
      const difference = wrapAngle(desired - world.robot.heading);
      if (Math.abs(difference) <= radians(4)) return true;
      const direction = difference > 0 ? 1 : -1;
      const blockedReason = sceneTurnBlockReason(direction);
      if (blockedReason) throw new Error(blockedReason);
      if (performance.now() - lastCommandAt >= 350) {
        turn.commandIds.add(nextCommandId);
        send(`MOTOR ${direction * 55} ${-direction * 55}`);
        turning = true;
        lastCommandAt = performance.now();
      }
      await new Promise(resolve => setTimeout(resolve, 25));
    }
    if (token === behaviorToken && interactionPhase === "PATROL") throw new Error("DETOUR_TURN_TIMEOUT");
    return false;
  } finally {
    if (activeScanMotion === turn) activeScanMotion = null;
    if (turning && token === behaviorToken) send("STOP");
  }
}

async function followDetourPath(path, token, tagName) {
  addLog("event", `PATROL_DETOUR_START tag=${tagName} around=${path.around || "OPEN_SPACE"} side=${path.side} waypoints=${path.points.length}`, "ESP32·SIM");
  $("routeStatus").textContent = `标签 ${tagName} 前方受阻，正在绕行（${path.side === "LEFT" ? "左侧" : path.side === "RIGHT" ? "右侧" : "空旷路线"}）`;
  for (const [index, point] of path.points.entries()) {
    for (let step = 0; step < 30; step++) {
      if (token !== behaviorToken || interactionPhase !== "PATROL") return false;
      if (document.visibilityState === "hidden") throw new Error("PAGE_HIDDEN");
      const remainingPx = Math.hypot(point.x - world.robot.x, point.y - world.robot.y);
      if (remainingPx <= 8) break;
      if (!await alignToDetourPoint(point, token)) return false;
      if (token !== behaviorToken || interactionPhase !== "PATROL") return false;
      const stepCm = Math.min(6, remainingPx / ARENA.pxPerCm);
      await sendMotion(`DRIVE ${stepCm.toFixed(1)} 105`);
      if (step === 29) throw new Error("DETOUR_STEP_LIMIT");
    }
    addLog("event", `PATROL_DETOUR_WAYPOINT tag=${tagName} index=${index + 1} x=${world.robot.x.toFixed(1)} y=${world.robot.y.toFixed(1)}`, "ESP32·SIM");
  }
  addLog("event", `PATROL_DETOUR_DONE tag=${tagName}`, "ESP32·SIM");
  return true;
}

async function followFixedPatrol(token) {
  let announcedTag = null;
  while (token === behaviorToken && interactionPhase === "PATROL") {
    const name = PATROL_TAGS[patrolWaypointIndex];
    const tag = tagObservation(name);
    if (decidePatrolStep({ detected: tag.detected, distanceCm: tag.distanceCm,
      targetCm: clamp(Number($("tagStopCm").value) || 20, 15, 50) }).action === "SCAN") {
      if (!await scanForRouteTag(name, token)) return;
      announcedTag = null;
      continue;
    }
    if (announcedTag !== name) {
      addLog("event", `PATROL_TAG_DETECTED tag=${name} distance_cm=${tag.distanceCm.toFixed(1)}`, "ESP32·SIM");
      $("routeStatus").textContent = `已看见标签 ${name}；接近指定距离`;
      announcedTag = name;
    }
    const stopCm = clamp(Number($("tagStopCm").value) || 20, 15, 50);
    const simulatedScene = mode === "mock" ||
      (mode === "serial" && serialDryRunConfirmed && $("streamSceneSensors").checked);
    if (simulatedScene && tag.distanceCm > stopCm + 1) {
      const detour = planSceneDetour({ start: world.robot, target: world.tags[name],
        stopRadiusPx: stopCm * ARENA.pxPerCm, arena: ARENA, robotRadius: ARENA.robotRadius,
        pxPerCm: ARENA.pxPerCm,
        obstacles: [{ ...world.cat, radius: ARENA.catRadius, name: "CAT" },
          { ...world.obstacle, name: "OBSTACLE" }] });
      if (detour.blocked) {
        if (!detour.paths.length) throw new Error("DETOUR_NO_PATH");
        if (!await followDetourPath(detour.paths[0], token, name)) return;
        continue;
      }
    }
    if (!await alignToTag(name, token)) continue;
    if (token !== behaviorToken || interactionPhase !== "PATROL") return;
    const centered = tagObservation(name);
    const routeDecision = decidePatrolStep({ detected: centered.detected,
      distanceCm: centered.distanceCm, targetCm: stopCm });
    if (routeDecision.action === "SCAN") continue;
    if (routeDecision.action === "APPROACH") {
      const stepCm = routeDecision.stepCm;
      addLog("event", `PATROL_APPROACH tag=${name} distance_cm=${centered.distanceCm.toFixed(1)} target_cm=${stopCm} step_cm=${stepCm.toFixed(1)}`, "ESP32·SIM");
      await sendMotion(`DRIVE ${stepCm.toFixed(1)} 105`);
      continue;
    }
    const nextIndex = (patrolWaypointIndex + 1) % PATROL_TAGS.length;
    const nextName = PATROL_TAGS[nextIndex];
    addLog("event", `PATROL_TAG_REACHED tag=${name} distance_cm=${centered.distanceCm.toFixed(1)} next_tag=${nextName}`, "ESP32·SIM");
    // The next tag is the checkpoint even if an interaction interrupts the scan.
    patrolWaypointIndex = nextIndex;
    if (!await scanForRouteTag(nextName, token)) return;
    announcedTag = null;
    addLog("event", `PATROL_NEXT_TAG tag=${nextName}`, "ESP32·SIM");
  }
}

async function finishFacingCat(token, action, { forceSearch = false, searchLimitDeg = 360, searchDirection = null } = {}) {
  if (token !== behaviorToken) return false;
  const phase = action === "ATTACK_RESPONSE" ? "触碰回应" : "收尾";
  $("faceCatStatus").textContent = `${phase}：等待触碰松开`;
  await waitForTouchRelease(token, decisionSystem.responsePolicy.touchReleaseTimeoutMs);
  if (token !== behaviorToken) return false;
  if (unoTouches.size) {
    send("STOP");
    $("faceCatStatus").textContent = "未能面向猫：触碰持续按下";
    addLog("error", `FACE_CAT_RESULT action=${action} status=FAILED reason=TOUCH_HELD`, "ESP32·SIM");
    return false;
  }
  $("faceCatStatus").textContent = `${phase}：寻找猫`;
  const search = !forceSearch && cameraObservation(world).detected
    ? { found: true, reason: "CAT_DETECTED" }
    : await searchCat(token, forceSearch ? decisionSystem.responsePolicy.searchTimeoutMs
      : decisionSystem.responsePolicy.finalSearchTimeoutMs, searchLimitDeg, searchDirection);
  if (token !== behaviorToken) return false;
  if (!search.found) {
    send("STOP");
    $("faceCatStatus").textContent = `未能面向猫：${search.reason}`;
    addLog("error", `FACE_CAT_RESULT action=${action} status=FAILED reason=${search.reason}`, "ESP32·SIM");
    return false;
  }
  $("faceCatStatus").textContent = `${phase}：对准猫`;
  const aligned = await alignToCat(token);
  if (token !== behaviorToken) return false;
  const camera = cameraObservation(world);
  const ready = aligned && camera.detected &&
    Math.abs(camera.centerX - .5) <= decisionSystem.responsePolicy.centerTolerancePercent / 100;
  faceReady = ready;
  $("faceCatStatus").textContent = ready ? `已面向猫 · 模拟取景就绪 (x=${camera.centerX.toFixed(2)})`
    : "未能面向猫：对准失败或猫离开视野";
  addLog(ready ? "event" : "error", `FACE_CAT_RESULT action=${action} status=${ready ? "READY" : "FAILED"} center_x=${camera.detected ? camera.centerX.toFixed(2) : "INVALID"}`, "ESP32·SIM");
  if (ready) addLog("event", "SIM_CAMERA CAPTURE_READY cat_centered=true photo_taken=false", "ESP32·SIM");
  return ready;
}

async function signalToCat(token) {
  const { signalFlashMs, signalBeepMs } = decisionSystem.responsePolicy;
  send("STOP");
  send(`LED FLASH 220 ${signalFlashMs}`);
  if (signalBeepMs > 0) send(`BEEP ${signalBeepMs}`);
  addLog("event", `ATTACK_RESPONSE stationary=true light=FLASH sound=${signalBeepMs > 0 ? "BEEP" : "OFF"}`, "ESP32·SIM");
  await behaviorPause(signalFlashMs, token);
  if (token === behaviorToken) send("LED OFF");
}

async function fleeFromFacingCat(token, pace = "FAST") {
  if (mode === "serial" && !serialTrackConfigured) throw new Error("TRACK_NOT_SET");
  const { escapeDistanceCm, slowEscapePwm, fastEscapePwm, touchReleaseTimeoutMs } = decisionSystem.responsePolicy;
  const pwm = pace === "SLOW" ? slowEscapePwm : fastEscapePwm;
  let resumedAfterTouch = false;
  for (;;) {
    try {
      const route = planSimulatedEscapeRoute(world, escapeDistanceCm,
        direction => !sceneTurnBlockReason(direction));
      if (!route) throw new Error("ESCAPE_NO_SAFE_ROUTE");
      addLog("event", `ESCAPE_ROUTE angle_deg=${route.angle} planned_cm=${route.distanceCm} projected_front_cm=${route.frontClearanceCm.toFixed(1)}`, "ESP32·SIM");
      await sendMotionWithTransportRetry(`ROTATE ${route.angle} 110`);
      if (token !== behaviorToken) return;
      const currentFrontCm = cameraFrontDistanceCm(world);
      const safeDistanceCm = currentFrontCm < 15 ? 0
        : Math.min(route.distanceCm, Math.floor(forwardPathClearanceCm(world) - 15));
      if (safeDistanceCm < 5) throw new Error("ESCAPE_FRONT_BLOCKED_AFTER_TURN");
      addLog("event", `ESCAPE_PHASE pace=${pace} straight_distance_cm=${safeDistanceCm} pwm=${pwm}`, "ESP32·SIM");
      await sendMotionWithTransportRetry(`DRIVE ${safeDistanceCm} ${pwm}`);
      if (token === behaviorToken) send("STOP");
      return;
    } catch (error) {
      // A cat grab mid-escape force-stops the wheels (Uno safety interlock). Wait for
      // the release, then re-plan from the current pose and continue the flee once.
      if (error.message === "TOUCH" && !resumedAfterTouch && token === behaviorToken) {
        resumedAfterTouch = true;
        addLog("event", `ESCAPE_RESUME reason=TOUCH_WAIT_RELEASE timeout_ms=${touchReleaseTimeoutMs}`, "ESP32·SIM");
        if (await waitForTouchRelease(token, touchReleaseTimeoutMs)) continue;
        if (token !== behaviorToken) return;
      }
      throw error;
    }
  }
}

async function showEndingOutcome(token, outcome) {
  const { captureCueFlashMs, captureCueBeepMs, captureWaitMaxSeconds,
    catLeaveDistanceCm, catLeaveSeconds } = decisionSystem.responsePolicy;
  send("STOP");
  send("LED OFF");
  addLog("event", `ENDING_OUTCOME_START outcome=${outcome} stationary=true`, "ESP32·SIM");
  if (outcome === "CONTINUE_PATROL") {
    $("decisionAction").textContent = "结束互动 · 继续巡游";
    addLog("event", "ENDING_OUTCOME_DONE outcome=CONTINUE_PATROL reason=IMMEDIATE", "ESP32·SIM");
    return;
  }
  $("decisionAction").textContent = outcome === "PREY_SIGNAL" ? "像猎物一样倒下 · 声光" : "像猎物一样倒下 · 安静";
  $("faceCatStatus").textContent = "机器人保持静止，等待猫离开";
  if (outcome === "PREY_SIGNAL") {
    send(`LED FLASH 480 ${captureCueFlashMs}`);
    send("BEEP 120");
    if (!await behaviorPause(500, token)) return;
    send(`BEEP ${captureCueBeepMs}`);
    if (!await behaviorPause(captureCueFlashMs - 500, token)) return;
    send("LED OFF");
  }
  const deadline = performance.now() + captureWaitMaxSeconds * 1000;
  let awaySince = null;
  let waitReason = "MAX_WAIT";
  while (token === behaviorToken && performance.now() < deadline) {
    const camera = cameraObservation(world);
    const now = performance.now();
    if (!camera.detected || camera.distanceCm > catLeaveDistanceCm) {
      if (awaySince === null) awaySince = now;
      if (now - awaySince >= catLeaveSeconds * 1000) {
        waitReason = "CAT_LEFT";
        break;
      }
    } else awaySince = null;
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  if (token === behaviorToken)
    addLog("event", `ENDING_OUTCOME_DONE outcome=${outcome} reason=${waitReason} wait_max_s=${captureWaitMaxSeconds}`, "ESP32·SIM");
}

async function runBehavior(name, source = "MANUAL", reason = "manual_button", details = {}) {
  const needsEscapeGeometry = name === "SLOW_STRAIGHT_ESCAPE" || name === "FAST_STRAIGHT_ESCAPE" ||
    (name === "ATTACK_RESPONSE" && details.attackPlan?.response !== "STAY_SIGNAL");
  if (mode === "serial" && (name === "PATROL" || needsEscapeGeometry) && !serialTrackConfigured) {
    showToast("请先设置车轮直径和左右轮中心距，确认 Uno 已回报 GEOMETRY_SET。");
    send("GET_CONFIG");
    return;
  }
  resumeSimulation("BEHAVIOR_START");
  if (name !== "PATROL" && name !== "END_SESSION" && source !== "ESP32_AUTO" && interactionPhase === "PATROL") {
    interactionPhase = "INTERACTION";
  }
  const hadMotion = mode === "mock" ? Boolean(mock.active) : realState === "MOVING";
  if (activeSearch) activeSearch.stopReason = "REPLACED_BY_BEHAVIOR";
  cancelBehavior("NEW_BEHAVIOR");
  if (hadMotion) send("STOP");
  showDecision(name, source, reason);
  addLog("event", `BEHAVIOR ${name} source=${source}`, "ESP32·SIM");
  const endingActiveSession = name === "END_SESSION" && sessionStartedAt !== null;
  const endingOutcome = name === "END_SESSION" ? details.endingOutcome : null;
  if (name !== "END_SESSION" && name !== "PATROL" && sessionStartedAt === null) startSession(false);
  const token = behaviorToken;
  if (name === "ATTACK_RESPONSE") touchDuringBehavior = false;
  let skipFinalFacing = name === "SEARCH_CAT_START" || name === "OBSERVE" || name === "STAY" ||
    (name === "END_SESSION" && !cameraObservation(world).detected);
  if (name === "END_SESSION" && endingActiveSession && !cameraObservation(world).detected)
    addLog("error", "FACE_CAT_RESULT action=END_SESSION status=FAILED reason=CAT_NOT_DETECTED", "ESP32·SIM");
  let attackFailureReason = null;
  try {
    if (source === "ESP32_AUTO" && interactionPhase === "INTERACTION" &&
        initialLookPending && name !== "ATTACK_RESPONSE" && name !== "SEARCH_CAT_START" &&
        name !== "END_SESSION" &&
        cameraObservation(world).detected) {
      if (!await finishFacingCat(token, name)) {
        skipFinalFacing = true;
        return;
      }
      if (token !== behaviorToken) return;
      initialLookPending = false;
      if (!await holdCatView(token)) {
        skipFinalFacing = true;
        return;
      }
    }
    if (name === "END_SESSION") {
      send("STOP");
      if (endingOutcome) await showEndingOutcome(token, endingOutcome);
    } else if (name === "PATROL") {
      if (reason === "operator_start_patrol" ||
          (mode === "mock" ? mock.sessionStopped : realState === "STOPPED")) {
        await sendAndWaitDone("SESSION_START", "SESSION_STARTED");
        if (token !== behaviorToken) return;
      }
      await followFixedPatrol(token);
    } else if (name === "STAY") {
      send("STOP");
      send("LED OFF");
      logCamera();
    } else if (name === "STAY_SIGNAL") {
      if (!await finishFacingCat(token, name, { forceSearch: true, searchLimitDeg: 360 })) {
        skipFinalFacing = true;
        return;
      }
      if (token !== behaviorToken) return;
      await signalToCat(token);
    } else if (name === "OBSERVE") {
      send("STOP");
      logCamera();
    } else if (name === "SEARCH_CAT_START") {
      await searchCat(token);
    } else if (name === "ATTACK_RESPONSE") {
      const plan = details.attackPlan;
      if (!plan) throw new Error("INVALID_ATTACK_PLAN");
      addLog("event", `ATTACK_SEARCH_START area=${plan.area} direction=${plan.direction < 0 ? "LEFT" : "RIGHT"} sweep_limit_deg=${plan.sweepLimitDeg}`, "ESP32·SIM");
      const ready = await finishFacingCat(token, name, {
        forceSearch: true, searchLimitDeg: plan.sweepLimitDeg, searchDirection: plan.direction,
      });
      if (token !== behaviorToken) return;
      if (!ready) throw new Error("ATTACK_CAT_NOT_FOUND_OR_UNSAFE");
      if (initialLookPending) {
        initialLookPending = false;
        if (!await holdCatView(token)) {
          if (token !== behaviorToken) return;
          throw new Error("CAT_LOST_DURING_VIEW_HOLD");
        }
      }
      addLog("event", `ATTACK_RESPONSE_PHASE cat_aligned=true response=${plan.response}`, "ESP32·SIM");
      if (plan.response === "STAY_SIGNAL") {
        await signalToCat(token);
      } else {
        await fleeFromFacingCat(token, plan.response === "SLOW_STRAIGHT_ESCAPE" ? "SLOW" : "FAST");
        if (token !== behaviorToken) return;
        addLog("event", "ATTACK_RESPONSE_PHASE reface_after_escape", "ESP32·SIM");
        if (cameraObservation(world).detected) {
          if (!await finishFacingCat(token, name))
            addLog("event", "CHASE_WAIT_CAT_NOT_CENTERED", "ESP32·SIM");
        } else addLog("event", "CHASE_WAIT_CAT_OUTSIDE_CAMERA", "ESP32·SIM");
        if (token !== behaviorToken) return;
        await waitForCatPursuit(token);
        if (token !== behaviorToken) return;
        // 逃跑后猫没有追上来：保持不动、面向猫凝视观察一段时间，再进入互动收尾。
        if (!touchDuringBehavior && cameraObservation(world).detected) {
          if (await finishFacingCat(token, name) && token === behaviorToken) await holdCatView(token);
          addLog("event", "POST_ESCAPE_OBSERVE_DONE", "ESP32·SIM");
        }
      }
    } else if (name === "SLOW_APPROACH") {
      if (!(await searchCat(token)).found) { skipFinalFacing = true; return; }
      if (token !== behaviorToken) return;
      if (!await alignToCat(token)) { skipFinalFacing = true; return; }
      if (token !== behaviorToken) return;
      const { slowApproachCm, slowApproachPwm } = decisionSystem.responsePolicy;
      await sendMotion(`DRIVE ${slowApproachCm} ${slowApproachPwm}`);
    } else if (name === "PROBE_APPROACH") {
      if (!(await searchCat(token)).found) { skipFinalFacing = true; return; }
      if (token !== behaviorToken) return;
      if (!await alignToCat(token)) { skipFinalFacing = true; return; }
      if (token !== behaviorToken) return;
      const policy = decisionSystem.responsePolicy;
      for (const [distanceCm, pauseMs] of [
        [policy.probeStep1Cm, policy.probePause1Ms],
        [policy.probeStep2Cm, policy.probePause2Ms],
        [policy.probeStep3Cm, policy.probePause3Ms],
      ]) {
        if (token !== behaviorToken) return;
        if (!cameraObservation(world).detected) {
          addLog("event", "PROBE_APPROACH stopped: cat outside camera view", "ESP32·SIM");
          return;
        }
        await sendMotion(`DRIVE ${distanceCm} ${policy.probePwm}`);
        if (token !== behaviorToken) return;
        send("STOP");
        logCamera();
        if (!await behaviorPause(pauseMs, token)) return;
      }
    } else if (name === "FAST_FORWARD") {
      await sendMotion("DRIVE 23 205");
    } else if (name === "SLOW_STRAIGHT_ESCAPE" || name === "FAST_STRAIGHT_ESCAPE") {
      if (!await finishFacingCat(token, name, { forceSearch: true, searchLimitDeg: 360 })) {
        skipFinalFacing = true;
        return;
      }
      if (token !== behaviorToken) return;
      await fleeFromFacingCat(token, name === "SLOW_STRAIGHT_ESCAPE" ? "SLOW" : "FAST");
    } else if (name === "CAPTURE_WIGGLE") {
      send("STOP");
      const recentTouch = Date.now() - lastTouchAt <= 3000;
      addLog("event", `capture_trigger=${recentTouch ? `TOUCH_${lastTouchSide}` : "MANUAL"} wiggle=low_speed_limited`, "ESP32·SIM");
      for (const [direction, duration] of [["LEFT", 180], ["RIGHT", 360], ["LEFT", 360], ["RIGHT", 180]]) {
        if (token !== behaviorToken) return;
        await sendMotion(`TURN ${direction} 80 ${duration}`);
        if (!await behaviorPause(180, token)) return;
      }
      send("STOP");
      logCamera();
    }
  } catch (error) {
    skipFinalFacing = true;
    if (token === behaviorToken) {
      if (name === "ATTACK_RESPONSE") send("STOP");
      if (name === "ATTACK_RESPONSE") {
        attackFailureReason = error.message;
        decisionSystem.abortCatFollowUp();
      }
      $("decisionAction").textContent = `${BEHAVIOR_LABELS[name] || name} · 执行中止`;
      $("decisionReason").textContent += `；执行失败：${error.message}`;
      if (name === "PATROL") {
        if (error.message === "ROUTE_TAG_NOT_FOUND") {
          $("routeStatus").textContent = "巡游停止：旋转 360° 仍未找到路线标签";
          autoControlEnabled = false;
          interactionPhase = "IDLE";
          autoDecisionVersion += 1;
          autoBusy = false;
          addLog("event", "DECISION_STOP reason=ROUTE_TAG_NOT_FOUND", "ESP32·SIM");
        } else if (/^(FRONT_OBSTACLE_OR_CAMERA_INVALID|SIDE_(LEFT|RIGHT)_OBSTACLE_OR_SENSOR_INVALID|BOTH_SIDES_BLOCKED|TOUCH_ACTIVE|DETOUR_NO_PATH|DETOUR_TURN_TIMEOUT|DETOUR_STEP_LIMIT)$/.test(error.message)) {
          $("routeStatus").textContent = `巡游暂停：${error.message}；等待安全条件恢复`;
          obstacleBlockedUntil = performance.now() + 5000;
        } else {
          $("routeStatus").textContent = `巡游停止：${error.message}；请检查指令与串口日志`;
          autoControlEnabled = false;
          interactionPhase = "IDLE";
          autoDecisionVersion += 1;
          autoBusy = false;
          addLog("event", `DECISION_STOP reason=${error.message}`, "ESP32·SIM");
        }
      }
      addLog("error", `BEHAVIOR_ABORTED ${name} reason=${error.message}`, "ESP32·SIM");
    }
  } finally {
    if (token === behaviorToken && !skipFinalFacing && name !== "PATROL" && name !== "ATTACK_RESPONSE" && (name !== "END_SESSION" || endingActiveSession)) {
      try {
        const faced = await finishFacingCat(token, name);
        // 会话结束的最后一个动作：面向猫静止凝视 ≥5 秒（逃后观察刚完成时不重复计时），
        // 之后才允许 SESSION_STOP 关闭闩锁。
        if (faced && name === "END_SESSION" && token === behaviorToken &&
            performance.now() - lastViewHoldAt > END_SESSION_HOLD_GRACE_MS) {
          if (!await holdCatView(token) && token === behaviorToken)
            addLog("event", "FINAL_VIEW_HOLD_STOPPED reason=CAT_LOST_SESSION_ENDS_ANYWAY", "ESP32·SIM");
        }
      } catch (error) {
        if (token === behaviorToken) {
          send("STOP");
          $("faceCatStatus").textContent = "未能面向猫：收尾过程出错";
          addLog("error", `FACE_CAT_RESULT action=${name} status=FAILED reason=${error.message}`, "ESP32·SIM");
        }
      }
    }
    if (token === behaviorToken && name === "END_SESSION") {
      let stopConfirmed = false;
      for (let attempt = 1; attempt <= 3 && !stopConfirmed; attempt++) {
        try {
          await sendAndWaitDone("SESSION_STOP", "SESSION_STOPPED");
          stopConfirmed = true;
        } catch (error) {
          if (token !== behaviorToken) return;
          if (attempt < 3) {
            addLog("event", `SESSION_STOP_RETRY attempt=${attempt + 1} reason=${error.message}`, "ESP32·SIM");
            await sleep(1000);
            continue;
          }
          // 三次都无回执：用一次状态查询判断命令是否其实已执行（只是回执碎在链路上）。
          send("GET_STATUS");
          await sleep(250);
          if (mode === "mock" ? mock.sessionStopped : realState === "STOPPED") {
            addLog("event", "SESSION_STOP_CONFIRMED_VIA_STATUS", "ESP32·SIM");
            stopConfirmed = true;
            break;
          }
          $("routeStatus").textContent = `互动结束失败：${error.message}；请检查串口日志`;
          addLog("error", `SESSION_END_FAILED reason=${error.message}`, "ESP32·SIM");
          // 不再永久关闭自动决策：记录续巡意图，下一次干净的 END_SESSION 会恢复巡游，
          // 避免固件闩锁关闭后无人重开导致的停死。
          interactionPhase = "IDLE";
          resumePatrolAfterInteraction = true;
          return;
        }
      }
      if (token !== behaviorToken) return;
      const duration = sessionStartedAt === null ? 0 : ((Date.now() - sessionStartedAt) / 1000).toFixed(1);
      addLog("event", `SESSION_ENDED duration_s=${duration}`, "ESP32·SIM");
      sessionStartedAt = null;
      decisionSystem.finishInteraction();
      initialLookPending = false;
      resetTouchBurst();
      if (autoControlEnabled || resumePatrolAfterInteraction) {
        const target = PATROL_TAGS[patrolWaypointIndex];
        interactionPhase = "PATROL";
        resumePatrolAfterInteraction = false;
        $("routeStatus").textContent = `互动结束，继续巡游：寻找标签 ${target}`;
        addLog("event", `PATROL_RESUME target_tag=${target} checkpoint_index=${patrolWaypointIndex}`, "ESP32·SIM");
        if (autoControlEnabled) launchAutoDecision("PATROL", "resume_after_session", true);
        else runBehavior("PATROL", source, "resume_after_session");
      } else {
        interactionPhase = "IDLE";
        if (!endingActiveSession) $("faceCatStatus").textContent = "互动未开始，无需取景";
      }
    }
    if (token === behaviorToken && name === "ATTACK_RESPONSE") {
      if (attackFailureReason) {
        resetTouchBurst();
        addLog("event", `CAT_TOUCH_FALLBACK reason=${attackFailureReason} action=RETURN_TO_PATROL`, "ESP32·AUTO");
        launchAutoDecision("END_SESSION", `cat_touch_response_failed_${attackFailureReason}`,
          true, true, { endingOutcome: "CONTINUE_PATROL" });
      } else if (!autoControlEnabled) {
        const ending = decisionSystem.chooseCatEnding("standalone_response_finished");
        await runBehavior("END_SESSION", "ESP32_AUTO", ending.reason,
          { endingOutcome: ending.endingOutcome });
      }
    }
  }
}

function catInputConfiguration() {
  return {
    behavior: $("catScenarioBehavior").value,
    attackArea: $("attackArea").value,
    speedCmS: $("catSpeedCmS").value,
    holdMs: $("attackHoldMs").value,
    intervalMs: $("attackIntervalMs").value,
  };
}

const CAT_POLICY_KEYS = ["touchSignalPercent", "touchSlowEscapePercent", "touchFastEscapePercent",
  "catFollowProbePercent", "catFollowObservePercent", "catFollowPatrolPercent",
  "catProbeCooldownSeconds", "initialLookSeconds", "postEscapeWaitSeconds", "touchDebounceMs"];
const ENDING_POLICY_KEYS = ["endingAfterRounds", "endingInactivitySeconds",
  "endingPatrolPercent", "endingSilentPercent", "endingSignalPercent", "captureWaitMaxSeconds"];
const ROBOT_POLICY_KEYS = ["robotFarApproachPercent", "robotFarProbePercent",
  "robotNearSignalPercent", "robotNearObservePercent", "robotNearStayPercent",
  "robotProbeCooldownSeconds", "farCatThresholdCm"];
const ADVANCED_POLICY_KEYS = Object.keys(DEFAULT_RESPONSE_POLICY)
  .filter(key => !CAT_POLICY_KEYS.includes(key) && !ENDING_POLICY_KEYS.includes(key) &&
    !ROBOT_POLICY_KEYS.includes(key));

function showResponsePolicy(group = null) {
  const policy = decisionSystem.responsePolicy;
  const keys = group === "cat" ? CAT_POLICY_KEYS : group === "ending" ? ENDING_POLICY_KEYS
    : group === "robot" ? ROBOT_POLICY_KEYS
    : group === "advanced" ? ADVANCED_POLICY_KEYS : Object.keys(DEFAULT_RESPONSE_POLICY);
  for (const key of keys) $(key).value = String(policy[key]);
  $("responsePolicyStatus").textContent = `已应用：触碰后声光／慢逃／快逃 ${policy.touchSignalPercent}/${policy.touchSlowEscapePercent}/${policy.touchFastEscapePercent}%；回应后试探／观察／巡游 ${policy.catFollowProbePercent}/${policy.catFollowObservePercent}/${policy.catFollowPatrolPercent}%；初次取景 ${policy.initialLookSeconds} 秒，逃跑后等待追击 ${policy.postEscapeWaitSeconds} 秒，连击静默 ${policy.touchDebounceMs} 毫秒（0 关闭）。`;
  $("endingPolicyStatus").textContent = `已应用：${policy.endingAfterRounds} 轮触碰或 ${policy.endingInactivitySeconds} 秒无新触碰后结束；直接巡游／安静倒下／声光倒下 ${policy.endingPatrolPercent}/${policy.endingSilentPercent}/${policy.endingSignalPercent}%；倒下后最多等待 ${policy.captureWaitMaxSeconds} 秒。0 表示关闭对应触发条件。`;
  $("robotPolicyStatus").textContent = `已应用：远处接近／试探 ${policy.robotFarApproachPercent}/${policy.robotFarProbePercent}%；近处声光／观察／静止 ${policy.robotNearSignalPercent}/${policy.robotNearObservePercent}/${policy.robotNearStayPercent}%。`;
  $("advancedPolicyStatus").textContent = "高级动作参数已应用；下次相应动作生效。";
}

function restoreResponsePolicy() {
  try {
    if (typeof localStorage !== "undefined") {
      const saved = localStorage.getItem(RESPONSE_POLICY_STORAGE_KEY);
      if (saved) decisionSystem.setResponsePolicy(JSON.parse(saved));
    }
  } catch {
    decisionSystem.setResponsePolicy(DEFAULT_RESPONSE_POLICY);
  }
  showResponsePolicy();
}

function applyResponsePolicy(group) {
  const keys = group === "cat" ? CAT_POLICY_KEYS : group === "ending" ? ENDING_POLICY_KEYS
    : group === "robot" ? ROBOT_POLICY_KEYS
    : ADVANCED_POLICY_KEYS;
  const values = { ...decisionSystem.responsePolicy };
  for (const key of keys) {
    const [min, max] = RESPONSE_POLICY_LIMITS[key];
    const raw = $(key).value.trim();
    const value = Number(raw);
    if (!raw || !Number.isFinite(value) || value < min || value > max ||
        (key !== "centerTolerancePercent" && !Number.isInteger(value))) {
      showToast(`${$(key).parentElement?.textContent?.trim() || key}：请输入 ${min}–${max} 的有效数值。`);
      return;
    }
    values[key] = value;
  }
  try {
    decisionSystem.setResponsePolicy(values);
  } catch (error) {
    showToast(error.message.includes("distance_order") ? "远近分界须小于猫离开距离。"
      : error.message.includes("sum_") ? "每一组回应概率必须合计 100%。" : "互动参数无效，请检查输入。");
    return;
  }
  showResponsePolicy(group);
  try {
    if (typeof localStorage !== "undefined")
      localStorage.setItem(RESPONSE_POLICY_STORAGE_KEY, JSON.stringify(decisionSystem.responsePolicy));
  } catch {
    showToast("概率已应用，但浏览器未允许保存；刷新后需重新设置。");
  }
  addLog("event", `RESPONSE_POLICY_SET group=${group} policy=${JSON.stringify(decisionSystem.responsePolicy)}`, "ESP32·SIM");
}

function stopController(reason) {
  autoControlEnabled = false;
  interactionPhase = "IDLE";
  resumePatrolAfterInteraction = false;
  initialLookPending = false;
  resetTouchBurst();
  autoDecisionVersion += 1;
  autoBusy = false;
  cancelBehavior(reason);
  send("STOP");
  addLog("event", `DECISION_STOP reason=${reason}`, "ESP32·SIM");
}

function launchAutoDecision(action, reason, force = false, sensorTriggered = false, details = {}) {
  if (paused || (!sensorTriggered && !autoControlEnabled)) return;
  if (autoBusy && !force) return;
  const version = ++autoDecisionVersion;
  autoBusy = true;
  activeAutoAction = action;
  nextAutoDecisionAt = performance.now() + decisionSystem.responsePolicy.decisionIntervalMs;
  addLog("event", `DECISION action=${action} evidence=${reason}`, "ESP32·AUTO");
  if (action === "END_SESSION") {
    interactionPhase = "ENDING";
  }
  Promise.resolve(runBehavior(action, "ESP32_AUTO", reason, details)).finally(() => {
    if (version === autoDecisionVersion) {
      autoBusy = false;
      activeAutoAction = null;
      nextAutoDecisionAt = performance.now() + decisionSystem.responsePolicy.decisionIntervalMs;
    }
  });
}

function onUnoTouchPress(area) {
  if (paused || interactionPhase === "ENDING") return;
  const decision = decisionSystem.onTouchPress(area, performance.now());
  addLog("event", `ATTACK_EVIDENCE source=UNO_TOUCH area=${area} count=${decisionSystem.attackCount}`, "ESP32·AUTO");
  if (interactionPhase === "PATROL") {
    interactionPhase = "INTERACTION";
    initialLookPending = true;
    if (!autoControlEnabled) resumePatrolAfterInteraction = true;
    $("routeStatus").textContent = `巡游在标签 ${PATROL_TAGS[patrolWaypointIndex]} 前中断：Uno 回报触碰`;
    addLog("event", `INTERACTION_TRIGGER source=UNO_TOUCH area=${area}`, "ESP32·AUTO");
  } else if (interactionPhase === "IDLE") {
    interactionPhase = "INTERACTION";
    initialLookPending = true;
  }
  if (autoBusy && activeAutoAction === "ATTACK_RESPONSE") {
    // Touch during an active response joins the current burst: it can end a waiting
    // phase (see waitForCatPursuit) but never schedules a second response.
    touchDuringBehavior = true;
    addLog("event", `ATTACK_TOUCH_DURING_RESPONSE area=${area} action=COUNT_ONLY`, "ESP32·AUTO");
    return;
  }
  pendingTouchBurst = decision;
  if (touchBurstTimer) clearTimeout(touchBurstTimer);
  const quietMs = decisionSystem.responsePolicy.touchDebounceMs;
  if (quietMs === 0) { flushTouchBurst(); return; }
  touchBurstTimer = setTimeout(flushTouchBurst, quietMs);
}

function flushTouchBurst() {
  touchBurstTimer = null;
  const burst = pendingTouchBurst;
  pendingTouchBurst = null;
  if (!burst || paused || interactionPhase === "ENDING") return;
  if (autoBusy && activeAutoAction === "ATTACK_RESPONSE") {
    touchDuringBehavior = true;
    return;
  }
  addLog("event", `TOUCH_BURST_COMMIT action=${burst.action} quiet_ms=${decisionSystem.responsePolicy.touchDebounceMs}`, "ESP32·AUTO");
  launchAutoDecision(burst.action, burst.reason, true, true, { attackPlan: burst.plan });
}

function resetTouchBurst() {
  if (touchBurstTimer) clearTimeout(touchBurstTimer);
  touchBurstTimer = null;
  pendingTouchBurst = null;
  touchDuringBehavior = false;
}

function injectMoodAlert(source) {
  const origin = source === "HTTP_NEGATIVE" ? "HTTP·SIM" : "CAMERA·SIM";
  addLog("event", `SENSOR_INPUT mood_alert=${source}`, origin);
  if (!autoControlEnabled || paused || interactionPhase !== "PATROL") {
    addLog("event", `MOOD_ALERT_IGNORED source=${source} phase=${interactionPhase}`, "ESP32·SIM");
    showToast("请先启动巡游决策，再注入一次性猫状态告警。");
    return;
  }
  const decision = decisionSystem.onMoodAlert(source, {
    phase: interactionPhase, camera: cameraObservation(world), now: performance.now(),
  });
  if (!decision) return;
  interactionPhase = "INTERACTION";
  initialLookPending = true;
  $("routeStatus").textContent = `巡游在标签 ${PATROL_TAGS[patrolWaypointIndex]} 前中断：收到猫状态告警`;
  addLog("event", `INTERACTION_TRIGGER source=${source}`, "ESP32·AUTO");
  launchAutoDecision(decision.action, decision.reason, true, true);
}

function tickAutoDecision(now) {
  if (!autoControlEnabled || paused) return;
  const decision = decisionSystem.tick({
    phase: interactionPhase, now,
    autoBusy, nextDecisionAt: nextAutoDecisionAt, obstacleBlockedUntil,
    sessionElapsedMs: sessionStartedAt === null ? null : Date.now() - sessionStartedAt,
    sessionLimitSeconds: decisionSystem.responsePolicy.sessionLimitSeconds, camera: cameraObservation(world),
  });
  if (!decision) return;
  launchAutoDecision(decision.action, decision.reason,
    decision.action === "END_SESSION", false, { endingOutcome: decision.endingOutcome });
}

function startSession(cancelCurrent = true) {
  if (cancelCurrent) {
    const hadMotion = mode === "mock" ? Boolean(mock.active) : realState === "MOVING";
    cancelBehavior("NEW_SESSION");
    if (hadMotion) send("STOP");
  }
  sessionStartedAt = Date.now();
  send("SESSION_START");
  addLog("event", `SESSION_STARTED at=${new Date(sessionStartedAt).toISOString()}`, "ESP32·SIM");
}

function setMode(next) {
  mode = next;
  $("modeBadge").textContent = next === "mock" ? "本地模拟" : "串口连接";
  $("modeBadge").classList.toggle("serial", next === "serial");
  $("modeNotice").textContent = next === "mock"
    ? "当前回报来自 JavaScript 模拟 Uno，不是实际固件。连接 Uno USB 串口后才能向固件发送命令。"
    : "已连接 Uno 串口。空板与实体模式使用同一套固件行为；画布位置依据运动与编码器回报估计。空板请先点“准备空板模拟”，实体模式请设置实测几何参数。";
  $("connectSerial").textContent = next === "mock" ? "连接 Uno 串口" : "断开串口";
  addLog("event", next === "mock" ? "MODE SIM_UNO" : "MODE SERIAL_UNO", "SYSTEM");
}

async function connectSerial() {
  if (!navigator.serial) {
    showToast("当前浏览器不支持 Web Serial。请用 Chrome 或 Edge 访问 http://localhost:8765。");
    return;
  }
  try {
    port = await navigator.serial.requestPort();
    await port.open({ baudRate: 115200 });
    writer = port.writable.getWriter();
    reader = port.readable.getReader();
    reading = true;
    cancelBehavior("MODE_SWITCH");
    sessionStartedAt = null;
    realState = "已连接";
    realDistanceText = "未读取";
    serialDryRunConfirmed = false;
    serialTrackConfigured = false;
    resetSerialOutputs();
    resetSerialSafeDistances();
    pendingSceneMotions.clear();
    activeSceneMotion = null;
    unoTouches.clear();
    clearManualTouchPulses();
    setMode("serial");
    readSerial().catch(error => showToast(`串口读取失败：${error.message}`));
  } catch (error) {
    port = null;
    showToast(`未连接串口：${error.message}`);
  }
}

async function readSerial() {
  const decoder = new TextDecoder();
  try {
    while (reading && reader) {
      const { value, done } = await reader.read();
      if (done) break;
      serialBuffer += decoder.decode(value, { stream: true });
      if (serialBuffer.length > 4096) serialBuffer = serialBuffer.slice(-4096);
      let newline;
      while ((newline = serialBuffer.indexOf("\n")) >= 0) {
        receiveLine(serialBuffer.slice(0, newline).replace(/\r$/, ""), "SERIAL");
        serialBuffer = serialBuffer.slice(newline + 1);
      }
    }
  } finally {
    if (reading) await disconnectSerial();
  }
}

async function disconnectSerial() {
  reading = false;
  cancelBehavior("SERIAL_DISCONNECTED");
  catInput.stop();
  autoControlEnabled = false;
  interactionPhase = "IDLE";
  autoDecisionVersion += 1;
  autoBusy = false;
  $("streamSceneSensors").checked = false;
  if (reader) {
    try { await reader.cancel(); } catch { /* Already closed. */ }
    try { reader.releaseLock(); } catch { /* The read loop may still own it. */ }
    reader = null;
  }
  if (writer) {
    try { writer.releaseLock(); } catch { /* Already closed. */ }
    writer = null;
  }
  if (port) {
    try { await port.close(); } catch { /* Device may have vanished. */ }
    port = null;
  }
  lastSerialAutoArea = null;
  sceneSensors.reset();
  realState = "未连接";
  realDistanceText = "未读取";
  serialDryRunConfirmed = false;
  serialTrackConfigured = false;
  resetSerialOutputs();
  resetSerialSafeDistances();
  pendingSceneMotions.clear();
  activeSceneMotion = null;
  sessionStartedAt = null;
  unoTouches.clear();
  clearManualTouchPulses(mode === "serial" && serialDryRunConfirmed);
  faceReady = false;
  $("faceCatStatus").textContent = "串口已断开，需重新取景";
  mock.reset(performance.now());
  setMode("mock");
}

function canvasPoint(event) {
  const rect = canvas.getBoundingClientRect();
  return { x: (event.clientX - rect.left) * ARENA.width / rect.width, y: (event.clientY - rect.top) * ARENA.height / rect.height };
}

function selectDrag(point) {
  const robot = world.robot;
  const nose = { x: robot.x + Math.cos(robot.heading) * 53, y: robot.y + Math.sin(robot.heading) * 53 };
  if (Math.hypot(point.x - nose.x, point.y - nose.y) < 25) return "heading";
  for (const name of PATROL_TAGS) {
    if (Math.hypot(point.x - world.tags[name].x, point.y - world.tags[name].y) < 25) return `tag:${name}`;
  }
  if (Math.hypot(point.x - world.cat.x, point.y - world.cat.y) < 34) return "cat";
  if (Math.hypot(point.x - robot.x, point.y - robot.y) < 38) return "robot";
  if (Math.hypot(point.x - world.obstacle.x, point.y - world.obstacle.y) < world.obstacle.radius + 12) return "obstacle";
  return null;
}

canvas.addEventListener("pointerdown", event => {
  const point = canvasPoint(event);
  drag = selectDrag(point);
  if (drag) canvas.setPointerCapture(event.pointerId);
});
canvas.addEventListener("pointermove", event => {
  if (!drag) return;
  const point = canvasPoint(event);
  if (drag === "robot" || drag === "heading") manualRobotRevision += 1;
  if (drag === "heading") world.robot.heading = wrapAngle(Math.atan2(point.y - world.robot.y, point.x - world.robot.x));
  else {
    const target = drag.startsWith("tag:") ? world.tags[drag.slice(4)] : world[drag];
    const radius = drag.startsWith("tag:") ? 17 : drag === "robot" ? ARENA.robotRadius : drag === "cat" ? ARENA.catRadius : world.obstacle.radius;
    target.x = clamp(point.x, radius, ARENA.width - radius);
    target.y = clamp(point.y, radius, ARENA.height - radius);
  }
});
canvas.addEventListener("pointerup", () => { drag = null; });
canvas.addEventListener("pointercancel", () => { drag = null; });

document.addEventListener("keydown", event => {
  if (event.target instanceof HTMLElement && event.target.closest("input, select, textarea")) return;
  const key = event.key.toLowerCase();
  const step = event.shiftKey ? 22 : 11;
  let handled = true;
  if (key === "w") world.cat.y -= step;
  else if (key === "s") world.cat.y += step;
  else if (key === "a") world.cat.x -= step;
  else if (key === "d") world.cat.x += step;
  else if (key === "arrowup") world.robot.y -= step;
  else if (key === "arrowdown") world.robot.y += step;
  else if (key === "arrowleft") world.robot.x -= step;
  else if (key === "arrowright") world.robot.x += step;
  else if (key === "q") world.robot.heading = wrapAngle(world.robot.heading - radians(8));
  else if (key === "e") world.robot.heading = wrapAngle(world.robot.heading + radians(8));
  else if (key === " ") { cancelBehavior(); send("STOP"); }
  else handled = false;
  if (handled) {
    if (["arrowup", "arrowdown", "arrowleft", "arrowright", "q", "e"].includes(key)) manualRobotRevision += 1;
    event.preventDefault();
    world.cat.x = clamp(world.cat.x, ARENA.catRadius, ARENA.width - ARENA.catRadius);
    world.cat.y = clamp(world.cat.y, ARENA.catRadius, ARENA.height - ARENA.catRadius);
    world.robot.x = clamp(world.robot.x, ARENA.robotRadius, ARENA.width - ARENA.robotRadius);
    world.robot.y = clamp(world.robot.y, ARENA.robotRadius, ARENA.height - ARENA.robotRadius);
  }
});

function drawScene(visual, now) {
  ctx.clearRect(0, 0, ARENA.width, ARENA.height);
  ctx.fillStyle = "#e8f2ec";
  ctx.fillRect(0, 0, ARENA.width, ARENA.height);
  ctx.strokeStyle = "#d7e7de";
  ctx.lineWidth = 1;
  for (let x = 0; x <= ARENA.width; x += 50) { ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, ARENA.height); ctx.stroke(); }
  for (let y = 0; y <= ARENA.height; y += 50) { ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(ARENA.width, y); ctx.stroke(); }

  const robot = world.robot;
  ctx.beginPath();
  ctx.moveTo(robot.x, robot.y);
  const sightLength = Math.hypot(ARENA.width, ARENA.height) * 2;
  for (const offset of [-radians(38), radians(38)]) {
    ctx.lineTo(robot.x + Math.cos(robot.heading + offset) * sightLength,
      robot.y + Math.sin(robot.heading + offset) * sightLength);
  }
  ctx.closePath();
  ctx.fillStyle = "rgba(35, 155, 135, .11)";
  ctx.fill();

  const distance = mode === "mock" ? mock.distanceCm() : cameraFrontDistanceCm(world);
  if (distance !== null) {
    const length = Math.min(distance * ARENA.pxPerCm, 440);
    ctx.strokeStyle = "#bf8046";
    ctx.lineWidth = 3;
    ctx.setLineDash([8, 7]);
    ctx.beginPath();
    ctx.moveTo(robot.x + Math.cos(robot.heading) * 30, robot.y + Math.sin(robot.heading) * 30);
    ctx.lineTo(robot.x + Math.cos(robot.heading) * (30 + length), robot.y + Math.sin(robot.heading) * (30 + length));
    ctx.stroke();
    ctx.setLineDash([]);
  }

  ctx.fillStyle = "#b8c8c2";
  ctx.strokeStyle = "#839e96";
  ctx.lineWidth = 3;
  ctx.beginPath();
  ctx.arc(world.obstacle.x, world.obstacle.y, world.obstacle.radius, 0, Math.PI * 2);
  ctx.fill(); ctx.stroke();
  ctx.fillStyle = "#294e50";
  ctx.font = "600 14px Segoe UI, sans-serif";
  ctx.textAlign = "center";
  ctx.fillText("障碍物", world.obstacle.x, world.obstacle.y + 5);

  for (const name of PATROL_TAGS) {
    const tag = world.tags[name];
    const visible = tagObservation(name).detected;
    ctx.fillStyle = visible ? "#345fc3" : "#7188b7";
    ctx.strokeStyle = "#fff";
    ctx.lineWidth = 3;
    ctx.fillRect(tag.x - 16, tag.y - 16, 32, 32);
    ctx.strokeRect(tag.x - 16, tag.y - 16, 32, 32);
    ctx.fillStyle = "#fff";
    ctx.font = "700 19px Segoe UI, sans-serif";
    ctx.textAlign = "center";
    ctx.fillText(name, tag.x, tag.y + 7);
  }

  ctx.save();
  ctx.translate(robot.x, robot.y);
  ctx.rotate(robot.heading);
  if (visual.moving) {
    ctx.strokeStyle = "#3767ce";
    ctx.lineWidth = 5;
    ctx.beginPath(); ctx.arc(0, 0, 40, 0, Math.PI * 2); ctx.stroke();
  }
  if (visual.buzzerOn) {
    const pulse = 48 + 3 * Math.sin(now / 90);
    ctx.strokeStyle = "#bf4c8b";
    ctx.lineWidth = 4;
    ctx.beginPath(); ctx.arc(0, 0, pulse, -.9, .9); ctx.stroke();
    ctx.beginPath(); ctx.arc(0, 0, pulse, Math.PI - .9, Math.PI + .9); ctx.stroke();
  }
  if (visual.led === "FLASH" && !visual.ledLit) {
    ctx.strokeStyle = "#d58a16";
    ctx.lineWidth = 3;
    ctx.setLineDash([5, 5]);
    ctx.beginPath(); ctx.arc(0, 0, 37, 0, Math.PI * 2); ctx.stroke();
    ctx.setLineDash([]);
  }
  ctx.fillStyle = visual.ledLit ? "#f2b43b" : "#1b6f71";
  ctx.strokeStyle = visual.ledLit ? "#9b6011" : "#0a4e52";
  ctx.lineWidth = 3;
  ctx.beginPath();
  ctx.roundRect(-30, -26, 60, 52, 13);
  ctx.fill(); ctx.stroke();
  ctx.fillStyle = "#17383a";
  ctx.fillRect(-23, -34, 17, 7);
  ctx.fillRect(-23, 27, 17, 7);
  ctx.fillRect(9, -34, 17, 7);
  ctx.fillRect(9, 27, 17, 7);
  ctx.fillStyle = "#d7f3e7";
  ctx.beginPath(); ctx.moveTo(20, -12); ctx.lineTo(35, 0); ctx.lineTo(20, 12); ctx.closePath(); ctx.fill();
  ctx.fillStyle = visual.ledLit ? "#fff5d5" : "#6a9a90";
  ctx.strokeStyle = visual.ledLit ? "#bd7110" : "#d7f3e7";
  ctx.lineWidth = 2;
  ctx.beginPath(); ctx.arc(-9, 0, 7, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
  ctx.restore();
  const noseX = robot.x + Math.cos(robot.heading) * 53;
  const noseY = robot.y + Math.sin(robot.heading) * 53;
  ctx.strokeStyle = "#1b6f71";
  ctx.lineWidth = 2;
  ctx.beginPath(); ctx.moveTo(robot.x, robot.y); ctx.lineTo(noseX, noseY); ctx.stroke();
  ctx.fillStyle = "#fff";
  ctx.beginPath(); ctx.arc(noseX, noseY, 8, 0, Math.PI * 2); ctx.fill(); ctx.stroke();

  const cat = world.cat;
  ctx.save(); ctx.translate(cat.x, cat.y);
  ctx.fillStyle = "#ce794b";
  ctx.strokeStyle = "#8d472e";
  ctx.lineWidth = 3;
  ctx.beginPath(); ctx.arc(0, 0, 20, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
  ctx.beginPath(); ctx.moveTo(-17, -13); ctx.lineTo(-15, -30); ctx.lineTo(-2, -19); ctx.closePath(); ctx.fill(); ctx.stroke();
  ctx.beginPath(); ctx.moveTo(17, -13); ctx.lineTo(15, -30); ctx.lineTo(2, -19); ctx.closePath(); ctx.fill(); ctx.stroke();
  ctx.fillStyle = "#fff2da";
  ctx.beginPath(); ctx.arc(-7, -2, 3, 0, Math.PI * 2); ctx.arc(7, -2, 3, 0, Math.PI * 2); ctx.fill();
  if (catInput.activeTouch) {
    ctx.beginPath(); ctx.arc(0, 0, 29, 0, Math.PI * 2);
    ctx.strokeStyle = "#a33333"; ctx.lineWidth = 4; ctx.stroke();
    ctx.fillStyle = "#a33333"; ctx.font = "700 15px Segoe UI, sans-serif";
    ctx.textAlign = "center"; ctx.fillText(`攻击 ${catInput.activeTouch}`, 0, -40);
  }
  ctx.restore();

  ctx.textAlign = "center";
  ctx.font = "600 15px Segoe UI, sans-serif";
  ctx.fillStyle = "#21565a";
  ctx.fillText("机器人", robot.x, robot.y + 56);
  ctx.fillStyle = "#90492d";
  ctx.fillText("猫", cat.x, cat.y + 47);
  ctx.textAlign = "left";
  ctx.font = "13px Segoe UI, sans-serif";
  ctx.fillStyle = "#426860";
  ctx.fillText("模拟位置 · 非实测坐标", 18, ARENA.height - 18);
}

function updateTelemetry(now, visual) {
  const camera = cameraObservation(world);
  if (faceReady && (!camera.detected || Math.abs(camera.centerX - .5) > decisionSystem.responsePolicy.centerTolerancePercent / 100)) {
    faceReady = false;
    $("faceCatStatus").textContent = "猫或机器人已移动，模拟取景需重新对准";
    addLog("event", "SIM_CAMERA CAPTURE_READY_INVALIDATED", "ESP32·SIM");
  }
  const touch = [...unoTouches];
  const distance = mode === "mock" ? mock.distanceCm() : cameraFrontDistanceCm(world);
  $("distanceValue").textContent = mode === "serial"
    ? `模拟摄像头 ${cameraFrontDistanceCm(world).toFixed(1)} cm / Uno 最近回报 ${realDistanceText}`
    : distance === null ? "无效 / 停机" : `${distance.toFixed(1)} cm`;
  const mockSideText = side => {
    const value = mock.sideDistanceCm(side);
    return value === null ? "无效" : `${value.toFixed(1)} cm`;
  };
  $("leftSonarValue").textContent = mode === "serial"
    ? `模拟 ${sideSonarDistanceCm(world, "LEFT").toFixed(1)} cm / Uno ${realLeftDistanceText}`
    : mockSideText("LEFT");
  $("rightSonarValue").textContent = mode === "serial"
    ? `模拟 ${sideSonarDistanceCm(world, "RIGHT").toFixed(1)} cm / Uno ${realRightDistanceText}`
    : mockSideText("RIGHT");
  $("cameraValue").textContent = camera.detected ? `发现猫 · x=${camera.centerX.toFixed(2)}` : "视野外";
  $("touchValue").textContent = touch.length ? touch.join("、") : "无";
  $("motionValue").textContent = mode === "mock" ? mock.state : realState;
  $("ledValue").textContent = visual.led === "FLASH" ? "闪烁" : visual.led === "ON" ? "常亮" : "关闭";
  $("buzzerValue").textContent = visual.buzzerOn ? "鸣响中" : "关闭";
  const safeSide = mode === "mock" ? mock.safeSideCm : serialSafeSideCm;
  $("safeDistanceStatus").textContent = `当前 Uno 阈值：左 ${safeSide.LEFT.toFixed(1)} cm，右 ${safeSide.RIGHT.toFixed(1)} cm；前方摄像头判定阈值 15.0 cm`;
  $("sessionValue").textContent = sessionStartedAt === null ? "互动未开始" : `互动 ${((Date.now() - sessionStartedAt) / 1000).toFixed(1)} 秒`;
  const phaseLabel = ({ PATROL: "巡游", INTERACTION: "互动", ENDING: "收尾", IDLE: "待命" })[interactionPhase];
  $("systemStatus").textContent = `猫动作${catInput.running ? "运行中" : "停止"} · 系统${phaseLabel} · Uno 触碰 ${decisionSystem.attackCount} 次${mode === "serial" ? serialDryRunConfirmed ? " · 空板输入" : " · 实体输入" : ""}`;
}

function sceneSensorCommands(now, force = false, requestedMotion = null) {
  const simulatedInputsEnabled = mode === "mock" ||
    (mode === "serial" && serialDryRunConfirmed && $("streamSceneSensors").checked);
  if (!simulatedInputsEnabled) return null;
  const emptyBoard = mode === "serial" && serialDryRunConfirmed && $("streamSceneSensors").checked;
  const motion = requestedMotion || (mode === "mock" ? mock.active : activeSceneMotion);
  const moving = mode === "mock" ? Boolean(mock.active) : realState === "MOVING";
  const frontRequired = !motion ||
    ((motion.left > 0 && motion.right >= 0) || (motion.right > 0 && motion.left >= 0));
  return sceneSensors.plan({
    now,
    cameraValue: manualFrontInput === null ? cameraFrontDistanceCm(world).toFixed(1) : manualFrontInput,
    sides: emptyBoard ? {
      LEFT: sideSonarDistanceCm(world, "LEFT").toFixed(1),
      RIGHT: sideSonarDistanceCm(world, "RIGHT").toFixed(1),
    } : null,
    sideSafeCm: mode === "mock" ? mock.safeSideCm : serialSafeSideCm,
    moving,
    beforeMotion: force,
    frontRequired,
  });
}

function streamSceneSensors(now, force = false, requestedMotion = null) {
  const commands = sceneSensorCommands(now, force, requestedMotion);
  if (commands === null) return;
  for (const command of commands) send(command, false);
  const emptyBoard = mode === "serial" && serialDryRunConfirmed && $("streamSceneSensors").checked;
  if (!emptyBoard || manualTouchAreas.size) return;
  const override = catInput.touchOverride();
  const area = override === undefined ? contactArea(world) : override;
  if (area !== lastSerialAutoArea) {
    if (lastSerialAutoArea) send(`DEBUG_INJECT TOUCH ${lastSerialAutoArea} RELEASE`);
    lastSerialAutoArea = area;
    if (area) send(`DEBUG_INJECT TOUCH ${area} PRESS`);
  }
}

function frame(now) {
  const elapsedSeconds = clamp((now - lastFrameAt) / 1000, 0, .05);
  lastFrameAt = now;
  if (!paused) {
    if (mode === "serial") stepSerialSceneMotion(elapsedSeconds);
    for (const event of catInput.step(now, elapsedSeconds)) {
      addLog("event", `SIM_CAT ${event.type} area=${event.area}`, "CAT·SIM");
    }
    mock.touchInputOverride = manualTouchAreas.size ? null : catInput.touchOverride();
    if (mode === "mock") mock.step(now);
  }
  streamSceneSensors(now);
  tickAutoDecision(now);
  const visual = robotVisualState(now);
  drawScene(visual, now);
  updateTelemetry(now, visual);
  requestAnimationFrame(frame);
}

$("connectSerial").addEventListener("click", () => {
  if (mode === "mock") connectSerial();
  else disconnectSerial();
});
$("pauseSim").addEventListener("click", () => {
  if (paused) return resumeSimulation("OPERATOR");
  paused = true;
  cancelBehavior("SIM_PAUSED");
  send("STOP");
  $("pauseSim").textContent = "继续模拟";
  addLog("event", "SIM_PAUSED", "SYSTEM");
});
$("resetScene").addEventListener("click", () => {
  stopController("SCENE_RESET");
  catInput.stop();
  resumeSimulation("SCENE_RESET");
  manualRobotRevision += 1;
  const original = initialWorld();
  Object.assign(world.robot, original.robot);
  Object.assign(world.cat, original.cat);
  Object.assign(world.obstacle, original.obstacle);
  for (const name of PATROL_TAGS) Object.assign(world.tags[name], original.tags[name]);
  patrolWaypointIndex = 0;
  sceneSensors.reset();
  $("routeStatus").textContent = "等待开始巡游";
  if (mode === "mock") mock.reset(performance.now());
  clearManualTouchPulses(mode === "serial" && serialDryRunConfirmed);
  unoTouches.clear();
  decisionSystem.reset();
  resumePatrolAfterInteraction = false;
  faceReady = false;
  $("faceCatStatus").textContent = "位置已重置，需重新取景";
  sessionStartedAt = null;
  addLog("event", "SCENE_RESET", "SYSTEM");
});
$("startPatrol").addEventListener("click", () => {
  if (mode === "serial" && !serialTrackConfigured) {
    showToast("请先设置车轮直径和左右轮中心距，确认 Uno 已回报 GEOMETRY_SET。");
    send("GET_CONFIG");
    return;
  }
  resumeSimulation("DECISION_START");
  cancelBehavior("NEW_PATROL");
  send("STOP");
  autoControlEnabled = true;
  patrolStartedAt = performance.now();
  patrolWaypointIndex = 0;
  $("routeStatus").textContent = "固定路线：准备搜索标签 A";
  sessionStartedAt = null;
  interactionPhase = "PATROL";
  decisionSystem.reset();
  resumePatrolAfterInteraction = false;
  initialLookPending = false;
  resetTouchBurst();
  obstacleBlockedUntil = 0;
  autoDecisionVersion += 1;
  autoBusy = false;
  nextAutoDecisionAt = patrolStartedAt + 600;
  faceReady = false;
  $("decisionInput").textContent = "巡游启动；等待摄像头与 Uno 观测";
  $("decisionAction").textContent = "巡游待命";
  $("decisionReason").textContent = "等待猫状态告警或 Uno 触碰";
  $("faceCatStatus").textContent = "巡游中；尚未进入互动取景";
  if (mode === "serial" && serialDryRunConfirmed) {
    $("streamSceneSensors").checked = true;
    sceneSensors.reset();
    streamSceneSensors(patrolStartedAt, true);
  }
  addLog("event", "DECISION_START phase=PATROL", "ESP32·SIM");
  launchAutoDecision("PATROL", "operator_start_patrol", true);
});
$("applyResponsePolicy").addEventListener("click", () => applyResponsePolicy("cat"));
$("applyEndingPolicy").addEventListener("click", () => applyResponsePolicy("ending"));
$("applyRobotPolicy").addEventListener("click", () => applyResponsePolicy("robot"));
$("applyAdvancedPolicy").addEventListener("click", () => applyResponsePolicy("advanced"));
$("stopPatrol").addEventListener("click", () => {
  if (autoControlEnabled || interactionPhase !== "IDLE") stopController("OPERATOR_STOP");
});
$("startCatMotion").addEventListener("click", () => {
  catInput.configure(catInputConfiguration());
  catInput.start();
  addLog("event", `CAT_INPUT_START behavior=${catInput.behavior} attack_area=${catInput.attackArea}`, "CAT·SIM");
});
$("stopCatMotion").addEventListener("click", () => {
  catInput.stop();
  addLog("event", "CAT_INPUT_STOP", "CAT·SIM");
});
for (const id of ["catScenarioBehavior", "attackArea", "catSpeedCmS", "attackHoldMs", "attackIntervalMs"]) {
  $(id).addEventListener("change", () => {
    catInput.configure(catInputConfiguration());
    addLog("event", `CAT_INPUT behavior=${catInput.behavior} attack_area=${catInput.attackArea} speed_cm_s=${catInput.speedCmS}`, "CAT·SIM");
  });
}
document.querySelectorAll("[data-mood-alert]").forEach(button => button.addEventListener("click", () =>
  injectMoodAlert(button.dataset.moodAlert)));
function setSafeDistance(side) {
  const input = $(side === "LEFT" ? "safeLeftCm" : "safeRightCm");
  const cm = Number(input.value);
  if (!input.value.trim() || !Number.isFinite(cm) || cm < 5 || cm > 100)
    return showToast("侧边最小安全距离须在 5–100 cm 之间。");
  send(`SET_SAFE_DISTANCE ${side} ${cm.toFixed(1)}`);
}
$("setSafeLeft").addEventListener("click", () => setSafeDistance("LEFT"));
$("setSafeRight").addEventListener("click", () => setSafeDistance("RIGHT"));
$("driveTarget").addEventListener("click", () => send(`DRIVE ${$("driveCm").value} ${$("pwm").value}`));
$("rotateTarget").addEventListener("click", () => send(`ROTATE ${$("rotateDeg").value} ${$("pwm").value}`));
$("stopMotion").addEventListener("click", () => { cancelBehavior(); send("STOP"); });
document.querySelectorAll("[data-quick]").forEach(button => button.addEventListener("click", () => {
  const duration = button.dataset.quick.startsWith("TURN") ? 500 : 1000;
  send(`${button.dataset.quick} ${$("pwm").value} ${duration}`);
}));
$("sendMotor").addEventListener("click", () => send(`MOTOR ${$("leftPwm").value} ${$("rightPwm").value}`));
document.querySelectorAll("[data-command]").forEach(button => button.addEventListener("click", () => {
  if (button.dataset.command === "DEBUG_MODE OFF") {
    $("streamSceneSensors").checked = false;
    serialDryRunConfirmed = false;
  }
  send(button.dataset.command);
  if (button.dataset.command === "CAMERA_FRONT INVALID" &&
      (mode === "mock" || serialDryRunConfirmed)) {
    manualFrontInput = "INVALID";
    streamSceneSensors(performance.now(), true);
  }
  if (mode === "serial" && button.dataset.command === "DEBUG_MOTOR_MODE DRY_RUN") {
    $("streamSceneSensors").checked = true;
    sceneSensors.reset();
    send("GET_STATUS");
  }
}));
$("prepareBoardSimulation").addEventListener("click", () => {
  if (mode !== "serial") {
    showToast("请先连接已烧录固件的 Uno 串口。");
    return;
  }
  const diameter = Number($("wheelDiameterMm").value);
  const trackInput = $("trackWidthMm");
  const track = trackInput.value ? Number(trackInput.value) : 120;
  if (!Number.isFinite(diameter) || diameter < 20 || diameter > 150 ||
      !Number.isFinite(track) || track < 50 || track > 300) {
    showToast("请填写有效的车轮直径和左右轮中心距。");
    return;
  }
  if (!trackInput.value) trackInput.value = String(track);
  cancelBehavior("BOARD_SIMULATION_SETUP");
  catInput.stop();
  autoControlEnabled = false;
  interactionPhase = "IDLE";
  autoDecisionVersion += 1;
  autoBusy = false;
  send("STOP");
  send("DEBUG_MODE ON");
  send("DEBUG_MOTOR_MODE DRY_RUN");
  send(`SET_GEOMETRY ${diameter} ${track}`);
  $("streamSceneSensors").checked = true;
  lastSerialAutoArea = null;
  sceneSensors.reset();
  send("GET_STATUS");
  send("GET_CONFIG");
  showToast("已发送空板设置与几何参数；请确认 GET_STATUS、GET_CONFIG 和距离回报后再开始动作。未填写轮距时使用名义值 120 mm。");
});
$("prepareScenarioBoard").addEventListener("click", () => $("prepareBoardSimulation").click());
document.querySelectorAll("[data-touch]").forEach(button => button.addEventListener("click", () => {
  if (mode === "serial" && !serialDryRunConfirmed) {
    showToast("手动触碰注入需要先启用空板模式；实体模式使用真实触碰开关。");
    return;
  }
  const area = button.dataset.touch;
  if (manualTouchAreas.has(area)) return;
  if (mode === "serial" && lastSerialAutoArea) {
    send(`DEBUG_INJECT TOUCH ${lastSerialAutoArea} RELEASE`);
    lastSerialAutoArea = null;
  }
  manualTouchAreas.add(area);
  button.setAttribute("aria-pressed", "true");
  send(`DEBUG_INJECT TOUCH ${area} PRESS`);
  const timer = setTimeout(() => {
    manualTouchTimers.delete(area);
    if (!manualTouchAreas.delete(area)) return;
    send(`DEBUG_INJECT TOUCH ${area} RELEASE`);
    button.setAttribute("aria-pressed", "false");
    streamSceneSensors(performance.now());
  }, 250);
  manualTouchTimers.set(area, timer);
}));
$("setDistance").addEventListener("click", () => {
  const distance = Number($("injectDistance").value);
  if (!Number.isFinite(distance) || distance < 0 || distance > 400) return showToast("距离须在 0–400 cm。");
  if (mode === "serial" && !serialDryRunConfirmed) return showToast("LIVE 模式不能注入模拟摄像头距离。");
  manualFrontInput = distance.toFixed(1);
  send(`CAMERA_FRONT ${manualFrontInput}`);
  send("GET_DISTANCE");
  showToast("前向读数已送入 Uno。距离只参与行驶安全判断；静止时不会自行发起移动。");
});
$("clearDistance").addEventListener("click", () => {
  if (mode === "serial" && !serialDryRunConfirmed) return showToast("LIVE 模式不能注入模拟摄像头距离。");
  manualFrontInput = null;
  send(`CAMERA_FRONT ${cameraFrontDistanceCm(world).toFixed(1)}`);
  send("GET_DISTANCE");
});
$("streamSceneSensors").addEventListener("change", event => {
  if (event.target.checked || mode !== "serial") return;
  if (lastSerialAutoArea) send(`DEBUG_INJECT TOUCH ${lastSerialAutoArea} RELEASE`);
  send("CAMERA_FRONT INVALID");
  lastSerialAutoArea = null;
  sceneSensors.reset();
});
$("injectEncoder").addEventListener("click", () => send(`DEBUG_INJECT ENCODER ${$("injectLeftEncoder").value} ${$("injectRightEncoder").value}`));
$("openCameraStream").addEventListener("click", () => {
  let url;
  try { url = new URL($("cameraStreamUrl").value.trim()); }
  catch { return showToast("请输入完整的 HTTP 或 HTTPS 视频流 URL。"); }
  if (!["http:", "https:"].includes(url.protocol) || url.username || url.password)
    return showToast("仅支持不含账号密码的 HTTP 或 HTTPS 视频流 URL。");
  const preview = $("cameraStreamPreview");
  preview.hidden = false;
  preview.src = url.href;
  $("cameraStreamStatus").textContent = "正在连接视频流；画面预览不会作为 Uno 避障输入。";
});
$("closeCameraStream").addEventListener("click", () => {
  const preview = $("cameraStreamPreview");
  preview.removeAttribute("src");
  preview.hidden = true;
  $("cameraStreamStatus").textContent = "视频流已关闭。";
});
$("cameraStreamPreview").addEventListener("load", () => {
  $("cameraStreamStatus").textContent = "正在预览 ESP32-S3 视频；前向避障读数仍由 CAMERA_FRONT 单独提供。";
});
$("cameraStreamPreview").addEventListener("error", () => {
  $("cameraStreamStatus").textContent = "无法显示该视频流；请核对地址、摄像头服务和浏览器访问权限。";
});
$("setGeometry").addEventListener("click", () => {
  const diameter = Number($("wheelDiameterMm").value);
  const track = Number($("trackWidthMm").value);
  if (!Number.isFinite(diameter) || diameter < 20 || diameter > 150 ||
      !Number.isFinite(track) || track < 50 || track > 300) {
    showToast("请填写 20–150 mm 的车轮直径和实测的 50–300 mm 左右轮中心距。");
    return;
  }
  send(`SET_GEOMETRY ${diameter} ${track}`);
});
$("rawForm").addEventListener("submit", event => {
  event.preventDefault();
  const input = $("rawCommand");
  send(input.value);
  input.value = "";
});
$("clearLog").addEventListener("click", () => { trace.length = 0; $("log").replaceChildren(); });
$("exportLog").addEventListener("click", () => {
  const payload = { exportedAt: new Date().toISOString(), mode,
    note: "SIM_UNO is a JavaScript mock, not Arduino firmware",
    responsePolicy: decisionSystem.responsePolicy, world, trace };
  const url = URL.createObjectURL(new Blob([JSON.stringify(payload, null, 2)], { type: "application/json" }));
  const link = document.createElement("a");
  link.href = url;
  link.download = `uno-web-lab-${new Date().toISOString().replace(/[:.]/g, "-")}.json`;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
});

restoreResponsePolicy();
setMode("mock");
requestAnimationFrame(frame);
globalThis.__unoLabReady = true;
