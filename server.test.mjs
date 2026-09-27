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

test("the HTTP server serves every local module imported by the browser", async () => {
  const port = await unusedPort();
  const child = spawn(process.execPath, ["server.mjs"], {
    cwd: import.meta.dirname,
    env: { ...process.env, UNO_WEB_LAB_PORT: String(port) },
    stdio: "ignore",
  });
  const origin = `http://127.0.0.1:${port}`;
  try {
    let page;
    for (let attempt = 0; attempt < 50; attempt += 1) {
      try { page = await fetch(origin); break; }
      catch { await new Promise(resolve => setTimeout(resolve, 50)); }
    }
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
