function distance(a, b) { return Math.hypot(a.x - b.x, a.y - b.y); }

function segmentDistance(a, b, point) {
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const lengthSquared = dx * dx + dy * dy;
  const t = lengthSquared ? Math.max(0, Math.min(1,
    ((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSquared)) : 0;
  return distance({ x: a.x + t * dx, y: a.y + t * dy }, point);
}

function withinArena(point, arena, robotRadius) {
  return point.x >= robotRadius && point.x <= arena.width - robotRadius &&
    point.y >= robotRadius && point.y <= arena.height - robotRadius;
}

function clearLeg(start, end, obstacles, clearance, allowDeparture = false) {
  return obstacles.every(obstacle => {
    const limit = clearance(obstacle);
    if (segmentDistance(start, end, obstacle) >= limit) return true;
    if (!allowDeparture || distance(start, obstacle) >= limit || distance(end, obstacle) < limit)
      return false;
    const outward = (end.x - start.x) * (start.x - obstacle.x) +
      (end.y - start.y) * (start.y - obstacle.y);
    return outward >= 0;
  });
}

export function planSceneDetour({ start, target, stopRadiusPx, obstacles, arena,
  robotRadius, pxPerCm, frontSafeCm = 15 }) {
  const clearance = obstacle => obstacle.radius + robotRadius + frontSafeCm * pxPerCm + 6;
  const fromTarget = Math.atan2(start.y - target.y, start.x - target.x);
  const goalAt = offset => ({
    x: target.x + Math.cos(fromTarget + offset) * stopRadiusPx,
    y: target.y + Math.sin(fromTarget + offset) * stopRadiusPx,
  });
  const directGoal = goalAt(0);
  if (withinArena(directGoal, arena, robotRadius) &&
      obstacles.every(obstacle => distance(directGoal, obstacle) >= clearance(obstacle)) &&
      clearLeg(start, directGoal, obstacles, clearance))
    return { blocked: false, paths: [] };

  const offsets = [0, Math.PI / 4, -Math.PI / 4, Math.PI / 2, -Math.PI / 2,
    3 * Math.PI / 4, -3 * Math.PI / 4, Math.PI];
  const paths = [];
  for (const offset of offsets) {
    const goal = goalAt(offset);
    if (!withinArena(goal, arena, robotRadius) ||
        obstacles.some(obstacle => distance(goal, obstacle) < clearance(obstacle))) continue;
    if (clearLeg(start, goal, obstacles, clearance)) {
      paths.push({ side: "DIRECT", points: [goal], lengthPx: distance(start, goal) });
      continue;
    }
    const blocker = obstacles.filter(obstacle => segmentDistance(start, goal, obstacle) < clearance(obstacle))
      .sort((a, b) => distance(start, a) - distance(start, b))[0];
    if (!blocker) continue;
    const angle = Math.atan2(goal.y - start.y, goal.x - start.x);
    const along = { x: Math.cos(angle), y: Math.sin(angle) };
    const across = { x: -along.y, y: along.x };
    const radius = clearance(blocker);
    for (const sign of [-1, 1]) {
      const before = {
        x: blocker.x - along.x * radius * .9 + across.x * radius * 1.2 * sign,
        y: blocker.y - along.y * radius * .9 + across.y * radius * 1.2 * sign,
      };
      const after = {
        x: blocker.x + along.x * radius * .9 + across.x * radius * 1.2 * sign,
        y: blocker.y + along.y * radius * .9 + across.y * radius * 1.2 * sign,
      };
      if (![before, after].every(point => withinArena(point, arena, robotRadius))) continue;
      if (!clearLeg(start, before, obstacles, clearance, true) ||
          !clearLeg(before, after, obstacles, clearance) ||
          !clearLeg(after, goal, obstacles, clearance)) continue;
      paths.push({ side: sign < 0 ? "LEFT" : "RIGHT", points: [before, after, goal],
        lengthPx: distance(start, before) + distance(before, after) + distance(after, goal),
        around: blocker.name });
    }
  }
  paths.sort((a, b) => a.lengthPx - b.lengthPx);
  return { blocked: true, paths };
}
