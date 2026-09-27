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
  ["/app.mjs", ["app.mjs", "text/javascript; charset=utf-8"]],
]);
const port = Number(process.env.UNO_WEB_LAB_PORT || 8765);

createServer(async (request, response) => {
  let pathname;
  try { pathname = new URL(request.url, "http://localhost").pathname; }
  catch { response.writeHead(400).end("Bad request"); return; }
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
