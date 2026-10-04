import { readFile, writeFile } from 'node:fs/promises';
import { gzipSync, gunzipSync } from 'node:zlib';

const root = new URL('./ESP32CatHost/', import.meta.url);
const template = await readFile(new URL('debug_page.html', root), 'utf8');
const compact = (await readFile(new URL('../compact-telemetry.mjs', import.meta.url), 'utf8')).replace(/^export /gm, '');
const telemetry = (await readFile(new URL('../hardware-telemetry.mjs', import.meta.url), 'utf8')).replace(/^import[^\n]*\n/gm, '').replace(/^export /gm, '');
if (!template.includes('/* HARDWARE_TELEMETRY */')) throw new Error('Missing telemetry bundle marker');
const robot = await readFile(new URL('./robot-ui.js', import.meta.url), 'utf8');
const html = Buffer.from(template.replace('/* HARDWARE_TELEMETRY */', () => compact + telemetry).replace('/* ROBOT_UI */', () => robot));
const compressed = gzipSync(html, { level: 9 });
if (!gunzipSync(compressed).equals(html)) throw new Error('Debug page compression round-trip failed');
const rows = [];
for (let i = 0; i < compressed.length; i += 24) rows.push([...compressed.subarray(i, i + 24)].join(','));
await writeFile(new URL('debug_page.h', root), '#pragma once\n// Generated from debug_page.html by generate-debug-page.mjs.\nconst uint8_t DEBUG_PAGE_GZIP[] PROGMEM={\n' + rows.join(',\n') + '\n};\n');
console.log(`Debug page: ${html.length} -> ${compressed.length} bytes (lossless gzip)`);
