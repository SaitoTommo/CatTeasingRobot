import { ARENA, clamp, cameraObservation } from "./core.mjs";

function moveToward(point, target, maxPixels) {
  const dx = target.x - point.x;
  const dy = target.y - point.y;
  const distance = Math.hypot(dx, dy);
  if (distance < .01) return distance;
  const step = Math.min(distance, maxPixels);
  point.x = clamp(point.x + dx / distance * step, ARENA.catRadius, ARENA.width - ARENA.catRadius);
  point.y = clamp(point.y + dy / distance * step, ARENA.catRadius, ARENA.height - ARENA.catRadius);
  return distance - step;
}

function sideTarget(robot, area) {
  if (area === "TOP") return { x: robot.x, y: robot.y };
  const offset = area === "LEFT" ? -Math.PI / 2 : area === "RIGHT" ? Math.PI / 2 : Math.PI;
  return {
    x: robot.x + Math.cos(robot.heading + offset) * 32,
    y: robot.y + Math.sin(robot.heading + offset) * 32,
  };
}

export class CatScenario {
  constructor(world) {
    this.world = world;
    this.running = false;
    this.behavior = "ATTACK";
    this.attackArea = "TOP";
    this.speedCmS = 20;
    this.holdMs = 700;
    this.intervalMs = 7000;
    this.leaveAfterAttacks = 3;
    this.attackCount = 0;
    this.departing = false;
    this.phase = "APPROACH";
    this.activeTouch = null;
    this.phaseUntil = 0;
    this.nextAttackAt = 0;
    this.topApproachReady = false;
    this.topApproachWaypoint = null;
  }

  configure({ behavior, attackArea, speedCmS, holdMs, intervalMs, leaveAfterAttacks }) {
    const changed = this.behavior !== behavior || this.attackArea !== attackArea;
    this.behavior = behavior;
    this.attackArea = attackArea;
    this.speedCmS = clamp(Number(speedCmS) || 20, 1, 60);
    this.holdMs = clamp(Number(holdMs) || 700, 100, 3000);
    this.intervalMs = clamp(Number(intervalMs) || 7000, 500, 10000);
    this.leaveAfterAttacks = clamp(Number(leaveAfterAttacks) || 0, 0, 30);
    if (changed) {
      this.phase = "APPROACH";
      this.activeTouch = null;
      this.nextAttackAt = 0;
      this.attackCount = 0;
      this.departing = false;
      this.topApproachReady = false;
      this.topApproachWaypoint = null;
    }
  }

  start() {
    this.running = true;
    this.phase = "APPROACH";
    this.activeTouch = null;
    this.nextAttackAt = 0;
    this.attackCount = 0;
    this.departing = false;
    this.topApproachReady = false;
    this.topApproachWaypoint = null;
  }

  stop() {
    this.running = false;
    this.activeTouch = null;
    this.departing = false;
  }

  // null means an attack scenario with no contact now; undefined uses geometric contact.
  touchOverride() {
    return this.running && this.behavior === "ATTACK" ? this.activeTouch : undefined;
  }

  step(now, elapsedSeconds) {
    if (!this.running) return [];
    const robot = this.world.robot;
    const cat = this.world.cat;
    const speed = this.speedCmS * ARENA.pxPerCm * clamp(elapsedSeconds, 0, .05);
    const dx = cat.x - robot.x;
    const dy = cat.y - robot.y;
    const distance = Math.hypot(dx, dy);
    if (this.behavior === "STILL") return [];
    if (this.behavior === "APPROACH" || this.behavior === "CHASE") {
      const stopDistance = this.behavior === "CHASE" ? 50 : 58;
      if (distance > stopDistance) {
        const fraction = (distance - stopDistance) / distance;
        moveToward(cat, { x: cat.x - dx * fraction, y: cat.y - dy * fraction }, speed);
      }
      return [];
    }
    if (this.behavior === "RETREAT" || this.departing) {
      const bearing = distance > 1 ? Math.atan2(dy, dx) : robot.heading + Math.PI;
      moveToward(cat, {
        x: cat.x + Math.cos(bearing) * speed,
        y: cat.y + Math.sin(bearing) * speed,
      }, speed);
      return [];
    }
    if (this.behavior !== "ATTACK") return [];
    if (this.phase === "HOLD") {
      if (now < this.phaseUntil) return [];
      this.activeTouch = null;
      if (this.leaveAfterAttacks && this.attackCount >= this.leaveAfterAttacks) {
        this.departing = true;
        this.phase = "LEAVING";
        return [{ type: "ATTACK_RELEASE", area: this.attackArea }, { type: "CAT_LEAVING", area: this.attackArea }];
      }
      this.phase = "RETREAT";
      this.nextAttackAt = now + this.intervalMs;
      this.topApproachReady = false;
      this.topApproachWaypoint = null;
      return [{ type: "ATTACK_RELEASE", area: this.attackArea }];
    }
    if (this.phase === "RETREAT") {
      if (distance < 140) {
        const bearing = distance > 1 ? Math.atan2(dy, dx) : robot.heading + Math.PI;
        moveToward(cat, {
          x: robot.x + Math.cos(bearing) * 145,
          y: robot.y + Math.sin(bearing) * 145,
        }, speed);
      }
      if (now >= this.nextAttackAt) this.phase = "APPROACH";
      return [];
    }
    if (this.attackArea === "TOP" && !this.topApproachReady) {
      const flank = this.topApproachWaypoint ||= {
        x: clamp(robot.x - Math.cos(robot.heading) * 70 + Math.sin(robot.heading) * 85,
          ARENA.catRadius, ARENA.width - ARENA.catRadius),
        y: clamp(robot.y - Math.sin(robot.heading) * 70 - Math.cos(robot.heading) * 85,
          ARENA.catRadius, ARENA.height - ARENA.catRadius),
      };
      moveToward(cat, flank, speed);
      if (Math.hypot(cat.x - flank.x, cat.y - flank.y) <= 12) this.topApproachReady = true;
      return [];
    }
    const target = sideTarget(robot, this.attackArea);
    moveToward(cat, target, speed);
    const contactDistance = this.attackArea === "TOP" ? ARENA.robotRadius * .55
      : ARENA.robotRadius + ARENA.catRadius;
    if (Math.hypot(cat.x - robot.x, cat.y - robot.y) > contactDistance) return [];
    this.activeTouch = this.attackArea;
    this.attackCount += 1;
    this.phase = "HOLD";
    this.phaseUntil = now + this.holdMs;
    return [{ type: "ATTACK_BEGIN", area: this.attackArea }];
  }
}

export function chooseEsp32Action({ world, catBehavior, catLeaving, random = Math.random }) {
  const distanceCm = Math.hypot(world.cat.x - world.robot.x, world.cat.y - world.robot.y) / ARENA.pxPerCm;
  const camera = cameraObservation(world);
  if (catLeaving || catBehavior === "RETREAT") {
    return { action: "OBSERVE", reason: "cat_is_leaving" };
  }
  if (!camera.detected) return { action: "SEARCH_CAT_START", reason: "cat_outside_camera" };
  const choices = distanceCm > 40 ? ["SLOW_APPROACH", "PROBE_APPROACH"]
    : catBehavior === "ATTACK" ? ["OBSERVE", "STAY"] : ["OBSERVE", "STAY", "STAY_SIGNAL"];
  const draw = random();
  const action = choices[Math.min(choices.length - 1, Math.floor(draw * choices.length))];
  return { action, reason: `cat_visible_distance_cm=${distanceCm.toFixed(1)} random=${draw.toFixed(3)} candidates=${choices.join(",")}` };
}
