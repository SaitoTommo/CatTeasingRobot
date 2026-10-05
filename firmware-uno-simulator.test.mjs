import test from 'node:test';
import assert from 'node:assert/strict';
import {initialWorld} from './core.mjs';
import {FirmwareUnoSimulator} from './firmware-uno-simulator.mjs';
import {CompactTelemetry} from './compact-telemetry.mjs';
import {compactReply} from './esp32-link.mjs';
function bench(){const lines=[],uno=new FirmwareUnoSimulator(initialWorld(),line=>lines.push(line));uno.boot(0);return {uno,lines,send:(id,text,t=0)=>uno.receive(compactReply(`C ${id} ${text}`),t)};}
test('complete TB telemetry publishes all 13 rows, injected sources and coherent timestamps',()=>{
 const {uno,lines,send}=bench();uno.injectTouch('TOP','PRESS',100);send(7,'GT',1200);
 const decoder=new CompactTelemetry();let rows;for(const line of lines){const r=decoder.accept(line,1200);if(r)rows=r;}
 assert.equal(rows.length,13);assert.ok(rows.some(r=>r.includes('STATE 1200')));assert.ok(rows.some(r=>r.includes('T TOP 0 -1 1 I')));assert.ok(rows.some(r=>r.includes('SL 0 0 -1 -1.0')));
 assert.ok(lines.some(r=>r.startsWith('READY SIM_UNO_TB_V1_SCAN_V1*')));
});
test('scan stops before VQ, rejects stale generation, finishes once and deduplicates retransmission',()=>{
 const {uno,lines,send}=bench();send(1,'SESSION_RESUME');send(2,'SR 5 2 100 8 0');
 let t=0;for(;t<5000&&!lines.some(r=>r.startsWith('VQ '));t+=20)uno.step(t);
 assert.ok(lines.some(r=>r.startsWith('MD 2 ')));assert.ok(lines.some(r=>r.startsWith('VQ 2 1 5 ')));assert.equal(uno.active,null);
 send(3,'VR 2 1 6 1 9',t);assert.ok(lines.some(r=>r.startsWith('ERROR 3 STALE_VISION')));
 send(4,'VR 2 1 5 1 9',t);uno.step(t+20);assert.ok(lines.some(r=>r.startsWith('SD 2 5 1')));
 const starts=lines.filter(r=>r.startsWith('MS ')).length;send(2,'SR 5 2 100 8 0',t+30);uno.step(t+50);assert.equal(lines.filter(r=>r.startsWith('MS ')).length,starts);
});
test('invalid CRC cannot start motion; scan bounds match firmware and touch aborts',()=>{
 const {uno,lines,send}=bench();uno.receive('C 1 MOTOR 255 255*00',0);assert.equal(uno.active,null);
 send(2,'SC 49 100 3000 300000 3000');assert.ok(lines.some(r=>r.startsWith('ERROR 2 INVALID_SCAN_FRAME')));
 send(3,'SESSION_RESUME');send(4,'SR 1 65 100 100 0');assert.ok(lines.some(r=>r.startsWith('ERROR 4 INVALID_SCAN_FRAME')));
 send(5,'SR 1 3 100 100 0');uno.step(10);uno.injectTouch('TOP','PRESS',20);uno.step(20);assert.equal(uno.active,null);assert.ok(lines.some(r=>r.startsWith('SD 5 1 4')));
});
