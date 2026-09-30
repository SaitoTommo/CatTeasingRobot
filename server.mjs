import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = dirname(fileURLToPath(import.meta.url));
const files = new Map([
  ["/", ["index.html", "text/html; charset=utf-8"]],
  ["/index.html", ["index.html", "text/html; charset=utf-8"]],
  ["/styles.css", ["styles.css", "text/css; charset=utf-8"]],
  ["/core.mjs", ["core.mjs", "text/javascript; charset=utf-8"]],
  ["/cat-input.mjs", ["cat-input.mjs", "text/javascript; charset=utf-8"]],
  ["/decision-system.mjs", ["decision-system.mjs", "text/javascript; charset=utf-8"]],
  ["/sensor-stream.mjs", ["sensor-stream.mjs", "text/javascript; charset=utf-8"]],
  ["/route-planner.mjs", ["route-planner.mjs", "text/javascript; charset=utf-8"]],
  ["/escape-planner.mjs", ["escape-planner.mjs", "text/javascript; charset=utf-8"]],
  ["/hardware-telemetry.mjs", ["hardware-telemetry.mjs", "text/javascript; charset=utf-8"]],
  ["/robot-observations.mjs", ["robot-observations.mjs", "text/javascript; charset=utf-8"]],
  ["/app.mjs", ["app.mjs", "text/javascript; charset=utf-8"]],
]);
const port = Number(process.env.UNO_WEB_LAB_PORT || 8765);

// One-shot find-cat commands addressed at the simulated ESP32 host. The queue
// carries intent only ("HTTP_NEGATIVE") — never cat position or any scene truth.
const commandQueue = [];
const COMMAND_QUEUE_LIMIT = 16;
const ALLOWED_SOURCES = new Set(["HTTP_NEGATIVE"]);

function jsonReply(response, status, payload) {
  response.writeHead(status, {
    "content-type": "application/json; charset=utf-8",
    "cache-control": "no-store",
  }).end(JSON.stringify(payload));
}

async function readBody(request) {
  const chunks = [];
  let size = 0;
  for await (const chunk of request) {
    size += chunk.length;
    if (size > 4096) return null;
    chunks.push(chunk);
  }
  return Buffer.concat(chunks).toString("utf8");
}

function parseSource(raw) {
  if (!raw) return null;
  try { return JSON.parse(raw).source ?? null; } catch { /* fall through */ }
  try { return new URLSearchParams(raw).get("source"); } catch { return null; }
}

createServer(async (request, response) => {
  let pathname;
  try { pathname = new URL(request.url, "http://localhost").pathname; }
  catch { response.writeHead(400).end("Bad request"); return; }
  if (pathname === "/mood_alert" && request.method === "POST") {
    const source = parseSource(await readBody(request));
    if (!ALLOWED_SOURCES.has(source)) { jsonReply(response, 400, { error: "unsupported_source" }); return; }
    if (commandQueue.length >= COMMAND_QUEUE_LIMIT) commandQueue.shift();
    commandQueue.push({ source, receivedAt: new Date().toISOString() });
    jsonReply(response, 200, { queued: true, pending: commandQueue.length });
    return;
  }
  if (pathname === "/mood_alert/pending" && request.method === "GET") {
    const command = commandQueue.shift() || null;
    jsonReply(response, 200, { source: command?.source ?? null, pending: commandQueue.length });
    return;
  }
  const resource = files.get(pathname);
  if (!resource || request.method !== "GET") {
    response.writeHead(404, { "content-type": "text/plain; charset=utf-8" }).end("Not found");
    return;
  }
  try {
    const contents = await readFile(join(root, resource[0]));
    response.writeHead(200, {
      "content-type": resource[1],
      "content-length": contents.length,
      "cache-control": "no-store",
      "x-content-type-options": "nosniff",
    }).end(contents);
  } catch {
    response.writeHead(500, { "content-type": "text/plain; charset=utf-8" }).end("Unable to read file");
  }
}).listen(port, "127.0.0.1", () => {
  process.stdout.write(`Uno Web Lab: http://localhost:${port}\n`);
});
