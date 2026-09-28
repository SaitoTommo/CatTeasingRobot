import test from "node:test";
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createServer } from "node:net";

async function unusedPort() {
  const server = createServer();
  await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
  const port = server.address().port;
  await new Promise(resolve => server.close(resolve));
  return port;
}

async function startLab() {
  const port = await unusedPort();
  const child = spawn(process.execPath, ["server.mjs"], {
    cwd: import.meta.dirname,
    env: { ...process.env, UNO_WEB_LAB_PORT: String(port) },
    stdio: "ignore",
  });
  const origin = `http://127.0.0.1:${port}`;
  for (let attempt = 0; attempt < 50; attempt += 1) {
    try { const page = await fetch(origin); if (page.ok) return { child, origin }; }
    catch { /* not listening yet */ }
    await new Promise(resolve => setTimeout(resolve, 50));
  }
  child.kill();
  throw new Error("lab server did not start");
}

test("mood_alert queue accepts an HTTP find-cat command and hands it out once", async () => {
  const { child, origin } = await startLab();
  try {
    assert.equal((await (await fetch(`${origin}/mood_alert/pending`)).json()).source, null);
    const post = await fetch(`${origin}/mood_alert`, {
      method: "POST", body: JSON.stringify({ source: "HTTP_NEGATIVE" }),
    });
    assert.equal(post.status, 200);
    assert.equal((await post.json()).queued, true);
    const first = await (await fetch(`${origin}/mood_alert/pending`)).json();
    assert.equal(first.source, "HTTP_NEGATIVE");
    const second = await (await fetch(`${origin}/mood_alert/pending`)).json();
    assert.equal(second.source, null, "one-shot commands must not be delivered twice");
    const form = await fetch(`${origin}/mood_alert`, {
      method: "POST", body: "source=HTTP_NEGATIVE",
    });
    assert.equal(form.status, 200);
    assert.equal((await (await fetch(`${origin}/mood_alert/pending`)).json()).source, "HTTP_NEGATIVE");
    const bad = await fetch(`${origin}/mood_alert`, {
      method: "POST", body: JSON.stringify({ source: "CAMERA_DETECTED" }),
    });
    assert.equal(bad.status, 400, "only intent vocabulary is accepted, never scene truth");
  } finally {
    child.kill();
  }
});

test("the HTTP server serves every local module imported by the browser", async () => {
  const { child, origin } = await startLab();
  try {
    const page = await fetch(origin);
    assert.equal(page?.status, 200);
    const visited = new Set();
    async function visit(path) {
      if (visited.has(path)) return;
      visited.add(path);
      const response = await fetch(`${origin}/${path}`);
      assert.equal(response.status, 200, `${path} must be served`);
      assert.match(response.headers.get("content-type"), /javascript/);
      const source = await response.text();
      for (const match of source.matchAll(/from\s+["']\.\/([^"']+)["']/g))
        await visit(match[1]);
    }
    await visit("app.mjs");
    assert.ok(visited.has("escape-planner.mjs"));
  } finally {
    child.kill();
  }
});
