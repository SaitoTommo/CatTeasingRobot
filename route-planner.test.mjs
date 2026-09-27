import test from "node:test";
import assert from "node:assert/strict";
import { ARENA, initialWorld } from "./core.mjs";
import { planSceneDetour } from "./route-planner.mjs";

test("a cat lying across the route produces a safe path around it", () => {
  const world = initialWorld();
  const args = { start: world.robot, target: world.tags.A, stopRadiusPx: 80,
    arena: ARENA, robotRadius: ARENA.robotRadius, pxPerCm: ARENA.pxPerCm };
  assert.equal(planSceneDetour({ ...args,
    obstacles: [{ ...world.cat, radius: ARENA.catRadius, name: "CAT" },
      { ...world.obstacle, name: "OBSTACLE" }] }).blocked, false);
  const blockingCat = { x: 500, y: 315, radius: ARENA.catRadius, name: "CAT" };
  const plan = planSceneDetour({ ...args, obstacles: [blockingCat,
    { ...world.obstacle, name: "OBSTACLE" }] });
  assert.equal(plan.blocked, true);
  assert.ok(plan.paths.length > 0);
  assert.equal(plan.paths[0].around, "CAT");
  assert.equal(plan.paths[0].points.length, 3);
  const goal = plan.paths[0].points.at(-1);
  assert.ok(Math.abs(Math.hypot(goal.x - world.tags.A.x, goal.y - world.tags.A.y) - 80) < .001);
});

test("detour reports no path when the route is physically boxed in", () => {
  const world = initialWorld();
  const result = planSceneDetour({ start: world.robot, target: world.tags.A,
    stopRadiusPx: 80, arena: ARENA, robotRadius: ARENA.robotRadius,
    pxPerCm: ARENA.pxPerCm,
    obstacles: [{ x: 500, y: 315, radius: 240, name: "LARGE_OBSTACLE" }] });
  assert.equal(result.blocked, true);
  assert.deepEqual(result.paths, []);
});
