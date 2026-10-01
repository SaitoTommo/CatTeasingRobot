import { readFile, writeFile } from 'node:fs/promises';
import { gzipSync, gunzipSync } from 'node:zlib';

const root = new URL('./ESP32CatHost/', import.meta.url);
const html = await readFile(new URL('debug_page.html', root));
const compressed = gzipSync(html, { level: 9 });
if (!gunzipSync(compressed).equals(html)) throw new Error('Debug page compression round-trip failed');
const rows = [];
for (let i = 0; i < compressed.length; i += 24) rows.push([...compressed.subarray(i, i + 24)].join(','));
await writeFile(new URL('debug_page.h', root), '#pragma once\n// Generated from debug_page.html by generate-debug-page.mjs.\nconst uint8_t DEBUG_PAGE_GZIP[] PROGMEM={\n' + rows.join(',\n') + '\n};\n');
console.log(`Debug page: ${html.length} -> ${compressed.length} bytes (lossless gzip)`);
