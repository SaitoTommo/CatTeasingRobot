// Proposed wire protocol and browser-only Uno model. This is not Arduino firmware.
export const ARENA = { width: 1000, height: 600, robotRadius: 26, catRadius: 18, pxPerCm: 4 };
export const TOUCH_AREAS = ["TOP", "LEFT", "RIGHT", "REAR"];

export function initialWorld() {
  return {
    robot: { x: 410, y: 315, heading: 0 },
    cat: { x: 675, y: 255 },
    obstacle: { x: 780, y: 435, radius: 37 },
    tags: {
      A: { x: 650, y: 315 }, B: { x: 570, y: 555 },
      C: { x: 330, y: 475 }, D: { x: 410, y: 235 },
    },
  };
}

export function clamp(value, min, max) { return Math.max(min, Math.min(max, value)); }
export function wrapAngle(angle) { return Math.atan2(Math.sin(angle), Math.cos(angle)); }
export function radians(degrees) { return degrees * Math.PI / 180; }
export function degrees(radiansValue) { return radiansValue * 180 / Math.PI; }

function rayCircleDistance(origin, angle, circle) {
  const dx = Math.cos(angle);
  const dy = Math.sin(angle);
  const ox = origin.x - circle.x;
  const oy = origin.y - circle.y;
  if (ox * ox + oy * oy <= circle.radius * circle.radius) return 0;
  const b = 2 * (ox * dx + oy * dy);
  const c = ox * ox + oy * oy - circle.radius * circle.radius;
  const discriminant = b * b - 4 * c;
  if (discriminant < 0) return Infinity;
  const t1 = (-b - Math.sqrt(discriminant)) / 2;
  const t2 = (-b + Math.sqrt(discriminant)) / 2;
  return t1 >= 0 ? t1 : (t2 >= 0 ? t2 : Infinity);
}

function rayWallDistance(origin, angle) {
  const dx = Math.cos(angle);
  const dy = Math.sin(angle);
  const distances = [];
  if (Math.abs(dx) > 1e-6) {
    distances.push((0 - origin.x) / dx, (ARENA.width - origin.x) / dx);
  }
  if (Math.abs(dy) > 1e-6) {
    distances.push((0 - origin.y) / dy, (ARENA.height - origin.y) / dy);
  }
  return Math.min(...distances.filter(value => value >= 0));
}

export function cameraFrontDistanceCm(world) {
  const origin = {
    x: world.robot.x + Math.cos(world.robot.heading) * ARENA.robotRadius,
    y: world.robot.y + Math.sin(world.robot.heading) * ARENA.robotRadius,
  };
  const objects = [
    { ...world.cat, radius: ARENA.catRadius },
    world.obstacle,
  ];
  let nearest = Infinity;
  for (const offset of [-radians(30), -radians(15), 0, radians(15), radians(30)]) {
    const ray = world.robot.heading + offset;
    nearest = Math.min(nearest, rayWallDistance(origin, ray));
    for (const object of objects) nearest = Math.min(nearest, rayCircleDistance(origin, ray, object));
  }
  return clamp(nearest / ARENA.pxPerCm, 0, 400);
}

export function sideSonarDistanceCm(world, side) {
  const direction = side === "LEFT" ? -1 : 1;
  const angle = world.robot.heading + direction * Math.PI / 2;
  const origin = {
    x: world.robot.x + Math.cos(angle) * ARENA.robotRadius,
    y: world.robot.y + Math.sin(angle) * ARENA.robotRadius,
  };
  let nearest = Infinity;
  for (const offset of [-radians(8), 0, radians(8)]) {
    const ray = angle + offset;
    nearest = Math.min(nearest, rayWallDistance(origin, ray));
    for (const object of [{ ...world.cat, radius: ARENA.catRadius }, world.obstacle])
      nearest = Math.min(nearest, rayCircleDistance(origin, ray, object));
  }
  return clamp(nearest / ARENA.pxPerCm, 0, 400);
}

export function cameraTargetObservation(world, target) {
  const dx = target.x - world.robot.x;
  const dy = target.y - world.robot.y;
  const distancePx = Math.hypot(dx, dy);
  const relative = wrapAngle(Math.atan2(dy, dx) - world.robot.heading);
  const blockers = [world.obstacle];
  if (target !== world.cat) blockers.push({ ...world.cat, radius: ARENA.catRadius });
  const occluded = blockers.some(object =>
    rayCircleDistance(world.robot, Math.atan2(dy, dx), object) < distancePx);
  const visible = distancePx <= 400 * ARENA.pxPerCm && Math.abs(relative) <= radians(38) && !occluded;
  return {
    detected: visible,
    centerX: visible ? clamp(.5 + relative / radians(76), 0, 1) : null,
    relativeDeg: visible ? degrees(relative) : null,
    distanceCm: visible ? distancePx / ARENA.pxPerCm : null,
  };
}

export function cameraObservation(world) {
  const observation = cameraTargetObservation(world, world.cat);
  const distancePx = observation.distanceCm * ARENA.pxPerCm;
  return {
    ...observation,
    bboxSize: observation.detected ? clamp(180 / Math.max(distancePx, 40), .05, .8) : null,
    source: "SIM_CAMERA",
  };
}

export function contactArea(world) {
  const dx = world.cat.x - world.robot.x;
  const dy = world.cat.y - world.robot.y;
  const distance = Math.hypot(dx, dy);
  if (distance > ARENA.robotRadius + ARENA.catRadius) return null;
  if (distance < ARENA.robotRadius * .55) return "TOP";
  const angle = wrapAngle(Math.atan2(dy, dx) - world.robot.heading);
  if (Math.abs(angle) < radians(45)) return null; // No front mechanical switch.
  if (Math.abs(angle) > radians(135)) return "REAR";
  return angle > 0 ? "RIGHT" : "LEFT";
}

function numberArg(value, min, max) {
  if (value === undefined || value === "" || !/^-?(?:\d+)(?:\.\d+)?$/.test(value)) return null;
  const parsed = Number(value);
  return Number.isFinite(parsed) && parsed >= min && parsed <= max ? parsed : null;
}

export class MockUno {
  constructor(world, emit) {
    this.world = world;
    this.emit = emit;
    this.reset(0);
  }

  reset(now) {
    this.active = null;
    this.state = "IDLE";
    this.sessionStopped = false;
    this.led = "OFF";
    this.ledFlashStartedAt = 0;
    this.ledInterval = 0;
    this.ledUntil = 0;
    this.buzzerUntil = 0;
    this.cameraFrontInput = null;
    this.cameraFrontAt = -Infinity;
    this.now = now;
    this.sideOverrides = { LEFT: undefined, RIGHT: undefined };
    this.sideOverrideAt = { LEFT: -Infinity, RIGHT: -Infinity };
    this.safeSideCm = { LEFT: 5, RIGHT: 5 };
    this.touch = Object.fromEntries(TOUCH_AREAS.map(area => [area, false]));
    this.touchStarted = {};
    this.autoArea = null;
    this.simulateRearTouch = true;
    this.pendingAutoArea = null;
    this.pendingAutoSince = now;
    this.encoder = { left: 0, right: 0 };
    this.debugEnabled = true;
    this.dryRunMotors = true;
    this.wheelDiameterMm = 67.5;
    this.trackWidthMm = null;
    this.lastStep = now;
    this.obstacleLatched = false;
    this.emit("READY SIM_UNO_V0.1");
  }

  distanceCm() {
    if (this.cameraFrontInput === null || this.now - this.cameraFrontAt > 500) return null;
    return this.cameraFrontInput === "CLEAR" ? 400
      : this.cameraFrontInput === "BLOCKED" ? 0 : this.cameraFrontInput;
  }

  sideDistanceCm(side) {
    if (this.sideOverrides[side] !== undefined && this.now - this.sideOverrideAt[side] > 500) return null;
    return this.sideOverrides[side] === undefined ? sideSonarDistanceCm(this.world, side) : this.sideOverrides[side];
  }

  safetyReason(left, right) {
    const forward = (left > 0 && right >= 0) || (right > 0 && left >= 0);
    const front = this.distanceCm();
    if (forward && (front === null || front < 15))
      return ["FRONT_OBSTACLE_OR_CAMERA_INVALID", "FRONT", front];
    if (left < right) {
      const distance = this.sideDistanceCm("LEFT");
      if (distance === null || distance < this.safeSideCm.LEFT)
        return ["SIDE_LEFT_OBSTACLE_OR_SENSOR_INVALID", "LEFT", distance];
    }
    if (left > right) {
      const distance = this.sideDistanceCm("RIGHT");
      if (distance === null || distance < this.safeSideCm.RIGHT)
        return ["SIDE_RIGHT_OBSTACLE_OR_SENSOR_INVALID", "RIGHT", distance];
    }
    return null;
  }

  output(line) { this.emit(line); }
  ack(id) { this.output(`ACK ${id}`); }
  done(id, suffix = "") { this.output(`DONE ${id}${suffix ? ` ${suffix}` : ""}`); }
  error(id, reason) { this.output(`ERROR ${id} ${reason}`); }

  stopMotion(reason) {
    if (!this.active) return;
    const id = this.active.id;
    this.active = null;
    this.state = this.sessionStopped ? "STOPPED" : "IDLE";
    this.output(`EVENT MOTION_ABORTED ${id} ${reason}`);
  }

  startMotion(id, left, right, durationMs, kind, now, estimated = false, gentleWiggle = false) {
    if (this.sessionStopped) return this.error(id, "SESSION_STOPPED");
    if (this.active) return this.error(id, "BUSY");
    if (Object.values(this.touch).some(Boolean) && !gentleWiggle) return this.error(id, "TOUCH_ACTIVE");
    const unsafe = this.safetyReason(left, right);
    if (unsafe) return this.error(id, unsafe[0]);
    this.active = { id, left, right, endAt: now + durationMs, kind, estimated };
    this.state = "MOVING";
    this.ack(id);
    this.output(`EVENT MOTION_STARTED ${id} ${kind}${estimated ? " ESTIMATED" : ""}`);
  }

  injectTouch(area, phase, now, source = "DEBUG") {
    if (!TOUCH_AREAS.includes(area)) return false;
    const pressed = phase === "PRESS";
    if (this.touch[area] === pressed) return true;
    this.touch[area] = pressed;
    if (pressed) {
      this.touchStarted[area] = now;
      this.stopMotion("TOUCH");
      this.output(`EVENT ${source} TOUCH ${area} PRESS ${Math.round(now)}`);
    } else {
      const duration = Math.round(now - (this.touchStarted[area] ?? now));
      this.output(`EVENT ${source} TOUCH ${area} RELEASE ${Math.round(now)} ${duration}`);
      delete this.touchStarted[area];
    }
    return true;
  }

  updateAutomaticTouch(now) {
    let nextArea = contactArea(this.world);
    if (nextArea === "REAR" && !this.simulateRearTouch) nextArea = null;
    if (nextArea !== this.pendingAutoArea) {
      this.pendingAutoArea = nextArea;
      this.pendingAutoSince = now;
      return;
    }
    if (nextArea === this.autoArea || now - this.pendingAutoSince < 45) return;
    const source = "SIM_CONTACT";
    if (this.autoArea) this.injectTouch(this.autoArea, "RELEASE", now, source);
    this.autoArea = nextArea;
    if (nextArea) this.injectTouch(nextArea, "PRESS", now, source);
  }

  receiveWire(line, now) {
    this.now = now;
    const match = /^CMD\s+(\d+)\s+(.{1,96})$/.exec(line.trim());
    if (!match) return this.output("ERROR 0 BAD_ENVELOPE");
    const id = Number(match[1]);
    const words = match[2].trim().toUpperCase().split(/\s+/);
    const [verb, a, b, c] = words;
    if (words.length > 5) return this.error(id, "TOO_MANY_ARGUMENTS");

    if (verb === "PING" && words.length === 1) { this.ack(id); return this.done(id, "PONG"); }
    if (verb === "GET_STATUS" && words.length === 1) {
      this.ack(id);
      this.output(`DATA ${id} STATUS ${this.state} LED=${this.led} BUZZER=${now < this.buzzerUntil ? "ON" : "OFF"} DEBUG=${this.debugEnabled ? "ON" : "OFF"} MOTOR_MODE=${this.dryRunMotors ? "DRY_RUN" : "LIVE"}`);
      return this.done(id);
    }
    if (verb === "GET_DISTANCE" && words.length === 1) {
      this.ack(id);
      const distance = this.distanceCm();
      this.output(`DATA ${id} DISTANCE ${distance === null ? "INVALID" : distance.toFixed(1)} SOURCE=SIM_CAMERA_FRONT`);
      const left = this.sideDistanceCm("LEFT");
      this.output(`DATA ${id} SIDE_LEFT ${left === null ? "INVALID" : left.toFixed(1)} SOURCE=SIM_SONAR_LEFT`);
      const right = this.sideDistanceCm("RIGHT");
      this.output(`DATA ${id} SIDE_RIGHT ${right === null ? "INVALID" : right.toFixed(1)} SOURCE=SIM_SONAR_RIGHT`);
      return this.done(id);
    }
    if (verb === "GET_ENCODER" && words.length === 1) {
      this.ack(id);
      this.output(`DATA ${id} ENCODER ${Math.round(this.encoder.left)} ${Math.round(this.encoder.right)}`);
      return this.done(id);
    }
    if (verb === "GET_CONFIG" && words.length === 1) {
      this.ack(id);
      this.output(`DATA ${id} CONFIG WHEEL_MM=${this.wheelDiameterMm.toFixed(1)} TRACK_MM=${this.trackWidthMm === null ? "UNSET" : this.trackWidthMm.toFixed(1)} SLOTS_PER_REV=20 SAFE_LEFT_CM=${this.safeSideCm.LEFT.toFixed(1)} SAFE_RIGHT_CM=${this.safeSideCm.RIGHT.toFixed(1)}`);
      return this.done(id);
    }
    if (verb === "SET_GEOMETRY" && words.length === 3) {
      const wheel = numberArg(a, 20, 150);
      const track = numberArg(b, 50, 300);
      if (wheel === null || track === null) return this.error(id, "INVALID_ARGUMENT");
      if (this.active) return this.error(id, "BUSY");
      this.wheelDiameterMm = wheel;
      this.trackWidthMm = track;
      this.ack(id);
      return this.done(id, "GEOMETRY_SET");
    }
    if (verb === "CAMERA_FRONT" && words.length === 2) {
      const distance = numberArg(a, 0, 400);
      if (!["CLEAR", "BLOCKED", "INVALID"].includes(a) && distance === null)
        return this.error(id, "INVALID_ARGUMENT");
      this.cameraFrontInput = a === "INVALID" ? null : distance ?? a;
      this.cameraFrontAt = now;
      this.ack(id);
      return this.done(id, "CAMERA_FRONT_SET");
    }
    if (verb === "SET_SAFE_DISTANCE" && words.length === 3 && ["LEFT", "RIGHT"].includes(a)) {
      const distance = numberArg(b, 5, 100);
      if (distance === null) return this.error(id, "INVALID_ARGUMENT");
      if (this.active) return this.error(id, "BUSY");
      this.safeSideCm[a] = distance;
      this.ack(id);
      return this.done(id, "SAFE_DISTANCE_SET");
    }
    if (verb === "SESSION_START" && words.length === 1) {
      this.sessionStopped = false;
      this.state = this.active ? "MOVING" : "IDLE";
      this.ack(id);
      return this.done(id, "SESSION_STARTED");
    }
    if (verb === "SESSION_STOP" && words.length === 1) {
      this.ack(id);
      this.sessionStopped = true;
      this.stopMotion("SESSION_STOP");
      this.led = "OFF";
      this.buzzerUntil = 0;
      this.cameraFrontInput = null;
      this.sideOverrides = { LEFT: undefined, RIGHT: undefined };
      this.state = "STOPPED";
      return this.done(id, "SESSION_STOPPED");
    }
    if (verb === "STOP" && words.length === 1) {
      this.ack(id);
      this.stopMotion("STOP_COMMAND");
      return this.done(id, "MOTOR_STOPPED");
    }
    if (verb === "KEEP" && words.length === 1) {
      if (this.active?.kind !== "MOTOR") return this.error(id, "NO_ACTIVE_MOTOR");
      this.active.endAt = now + 500;
      return this.done(id, "LEASE_RENEWED");
    }
    if (verb === "MOVE" && words.length === 4 && ["FORWARD", "BACKWARD"].includes(a)) {
      const pwm = numberArg(b, 1, 255);
      const duration = numberArg(c, 1, 10000);
      if (pwm === null || duration === null) return this.error(id, "INVALID_ARGUMENT");
      const speed = pwm / 255 * (a === "FORWARD" ? 1 : -1);
      return this.startMotion(id, speed, speed, duration, `MOVE_${a}`, now);
    }
    if (verb === "TURN" && words.length === 4 && ["LEFT", "RIGHT"].includes(a)) {
      const pwm = numberArg(b, 1, 255);
      const duration = numberArg(c, 1, 10000);
      if (pwm === null || duration === null) return this.error(id, "INVALID_ARGUMENT");
      const speed = pwm / 255 * (a === "RIGHT" ? 1 : -1);
      return this.startMotion(id, speed, -speed, duration, `TURN_${a}`, now, false, pwm <= 90 && duration <= 400);
    }
    if (verb === "MOTOR" && words.length === 3) {
      const left = numberArg(a, -255, 255);
      const right = numberArg(b, -255, 255);
      if (left === null || right === null) return this.error(id, "INVALID_ARGUMENT");
      if (left === 0 && right === 0) {
        this.ack(id);
        this.stopMotion("MOTOR_ZERO");
        return this.done(id, "MOTOR_STOPPED");
      }
      if (this.active?.kind === "MOTOR" && this.active.left === left / 255 && this.active.right === right / 255) {
        this.active.endAt = now + 500;
        return this.done(id, "LEASE_RENEWED");
      }
      if (this.active?.kind === "MOTOR") this.stopMotion("REPLACED");
      return this.startMotion(id, left / 255, right / 255, 500, "MOTOR", now);
    }
    if (verb === "DRIVE" && words.length === 3) {
      const cm = numberArg(a, -100, 100);
      const pwm = numberArg(b, 1, 255);
      if (cm === null || cm === 0 || pwm === null) return this.error(id, "INVALID_ARGUMENT");
      const duration = Math.abs(cm) * ARENA.pxPerCm / (120 * pwm / 255) * 1000;
      if (duration > 10000) return this.error(id, "DURATION_LIMIT");
      const speed = Math.sign(cm) * pwm / 255;
      return this.startMotion(id, speed, speed, duration, "DRIVE", now, true);
    }
    if (verb === "ROTATE" && words.length === 3) {
      const angle = numberArg(a, -360, 360);
      const pwm = numberArg(b, 1, 255);
      if (angle === null || angle === 0 || pwm === null) return this.error(id, "INVALID_ARGUMENT");
      const angularSpeed = 2 * 120 * pwm / 255 / (ARENA.pxPerCm * (this.trackWidthMm ?? 120) / 10);
      const duration = Math.abs(radians(angle)) / angularSpeed * 1000;
      if (duration > 10000) return this.error(id, "DURATION_LIMIT");
      const speed = Math.sign(angle) * pwm / 255;
      return this.startMotion(id, speed, -speed, duration, "ROTATE", now, true);
    }
    if (verb === "LED" && ["ON", "OFF"].includes(a) && words.length === 2) {
      if (this.sessionStopped && a === "ON") return this.error(id, "SESSION_STOPPED");
      this.led = a;
      this.ledFlashStartedAt = 0;
      this.ledInterval = 0;
      this.ledUntil = 0;
      this.ack(id);
      return this.done(id, `LED_${a}`);
    }
    if (verb === "LED" && a === "FLASH" && words.length === 4) {
      if (this.sessionStopped) return this.error(id, "SESSION_STOPPED");
      const interval = numberArg(b, 20, 5000);
      const duration = numberArg(c, 20, 10000);
      if (interval === null || duration === null) return this.error(id, "INVALID_ARGUMENT");
      this.led = "FLASH";
      this.ledFlashStartedAt = now;
      this.ledInterval = interval;
      this.ledUntil = now + duration;
      this.ack(id);
      return this.done(id, "LED_FLASH_STARTED");
    }
    if (verb === "BEEP" && words.length === 2) {
      if (this.sessionStopped) return this.error(id, "SESSION_STOPPED");
      const duration = numberArg(a, 1, 5000);
      if (duration === null) return this.error(id, "INVALID_ARGUMENT");
      this.buzzerUntil = now + duration;
      this.ack(id);
      return this.done(id, "BEEP_STARTED");
    }
    if (verb === "DEBUG_MODE" && words.length === 2 && ["ON", "OFF"].includes(a)) {
      this.debugEnabled = a === "ON";
      this.ack(id);
      return this.done(id, this.debugEnabled ? "DEBUG_ON" : "DEBUG_OFF");
    }
    if (verb === "DEBUG_MOTOR_MODE" && words.length === 2 && ["DRY_RUN", "LIVE"].includes(a)) {
      if (!this.debugEnabled) return this.error(id, "DEBUG_DISABLED");
      this.dryRunMotors = a === "DRY_RUN";
      this.cameraFrontInput = null;
      this.sideOverrides = { LEFT: undefined, RIGHT: undefined };
      this.ack(id);
      return this.done(id, a);
    }
    if (verb === "DEBUG_INJECT") {
      if (!this.debugEnabled) return this.error(id, "DEBUG_DISABLED");
      if (!this.dryRunMotors &&
          !((a === "TOUCH" && c === "PRESS") ||
            (a === "SONAR" && ["AUTO", "INVALID"].includes(c)))) {
        return this.error(id, "DRY_RUN_REQUIRED");
      }
      if (a === "TOUCH" && words.length === 4 && TOUCH_AREAS.includes(b) && ["PRESS", "RELEASE"].includes(c)) {
        this.ack(id);
        this.injectTouch(b, c, now, "DEBUG_INJECT");
        return this.done(id);
      }
      if (a === "SONAR" && words.length === 4 && ["LEFT", "RIGHT"].includes(b)) {
        const distance = c === "AUTO" ? undefined : (c === "INVALID" ? null : numberArg(c, 0, 400));
        if (distance === null && c !== "INVALID") return this.error(id, "INVALID_ARGUMENT");
        this.sideOverrides[b] = distance;
        this.sideOverrideAt[b] = now;
        this.ack(id);
        this.output(`EVENT DEBUG_INJECT SONAR ${b} ${distance === undefined ? "AUTO" : distance === null ? "INVALID" : distance.toFixed(1)}`);
        return this.done(id);
      }
      if (a === "ENCODER" && words.length === 4) {
        const left = numberArg(b, 0, 10000);
        const right = numberArg(c, 0, 10000);
        if (left === null || right === null) return this.error(id, "INVALID_ARGUMENT");
        this.encoder.left += left;
        this.encoder.right += right;
        this.ack(id);
        this.output(`EVENT DEBUG_INJECT ENCODER +${left} +${right}`);
        return this.done(id);
      }
    }
    return this.error(id, "UNKNOWN_OR_INVALID_COMMAND");
  }

  step(now) {
    this.now = now;
    const dt = clamp((now - this.lastStep) / 1000, 0, .05);
    this.lastStep = now;
    this.updateAutomaticTouch(now);
    if (this.led === "FLASH" && now >= this.ledUntil) this.led = "OFF";
    if (this.active) {
      const motion = this.active;
      const unsafe = this.safetyReason(motion.left, motion.right);
      if (unsafe) {
        if (!this.obstacleLatched) {
          this.output(`EVENT OBSTACLE_NEAR ${unsafe[1]} ${unsafe[2] === null ? "INVALID" : unsafe[2].toFixed(1)}`);
          this.obstacleLatched = true;
        }
        this.stopMotion(unsafe[0]);
      } else if (dt > 0) {
        const left = motion.left;
        const right = motion.right;
        const average = (left + right) / 2;
        const omega = (left - right) * 120 / (ARENA.pxPerCm * (this.trackWidthMm ?? 120) / 10);
        const travel = average * 120 * dt;
        const nextHeading = wrapAngle(this.world.robot.heading + omega * dt);
        this.world.robot.heading = nextHeading;
        this.world.robot.x = clamp(this.world.robot.x + Math.cos(nextHeading) * travel, ARENA.robotRadius, ARENA.width - ARENA.robotRadius);
        this.world.robot.y = clamp(this.world.robot.y + Math.sin(nextHeading) * travel, ARENA.robotRadius, ARENA.height - ARENA.robotRadius);
        // 20 slots on each wheel axle; one counted edge per slot.
        const pulsesPerPixel = 20 / (Math.PI * this.wheelDiameterMm / 10 * ARENA.pxPerCm);
        this.encoder.left += Math.abs(left) * 120 * dt * pulsesPerPixel;
        this.encoder.right += Math.abs(right) * 120 * dt * pulsesPerPixel;
      }
      if (this.active === motion && now >= motion.endAt) {
        this.active = null;
        this.state = "IDLE";
        if (motion.kind === "MOTOR") this.output(`EVENT MOTION_ABORTED ${motion.id} DEADMAN_TIMEOUT`);
        else this.output(`EVENT MOTION_DONE ${motion.id}${motion.estimated ? " ESTIMATED" : ""}`);
      }
    }
    if (this.distanceCm() >= 20) this.obstacleLatched = false;
    this.updateAutomaticTouch(now);
  }

  snapshot(now) {
    return {
      state: this.state,
      distanceCm: this.distanceCm(),
      camera: cameraObservation(this.world),
      touched: TOUCH_AREAS.filter(area => this.touch[area]),
      led: this.led,
      buzzer: now < this.buzzerUntil,
      encoder: { ...this.encoder },
    };
  }
}
