import fs from 'node:fs';
import crypto from 'node:crypto';
const source=new URL('../../../.codex-remote-attachments/01a10ce8-1c6f-7440-9496-178313911ee6/6711a1cf-cce9-4d0a-b2e2-33e3bcd75533/1-esp32-local-history-2026-10-06T00-43-43-537Z.json',import.meta.url);
const raw=fs.readFileSync(source),d=JSON.parse(raw),boot=d.lastState.bootId;
const window=d.logs.filter(r=>r.bootId===boot&&r.ms>=989000&&r.ms<993000);
const events=window.filter(r=>['DECISION','TAG_SCAN_SAMPLE','TAG_REACHED'].includes(r.source)||r.source==='UNO_TX'&&/ (?:MOTOR|STOP|SR)\b/.test(r.line));
const result={source:source.pathname,sha256:crypto.createHash('sha256').update(raw).digest('hex'),bootId:boot,route:d.lastState.config.route,minimumDrivePwm:d.lastState.config.minimumDrivePwm,events:events.map(({ms,source,line})=>({ms,source,line})),limits:'Device receive timestamps and a truncated history window; no synchronized target pose, per-frame target-negative reason, video or externally measured motion.'};
fs.writeFileSync(new URL('log-evidence.json',import.meta.url),JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify(result,null,2));
