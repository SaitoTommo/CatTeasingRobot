import test from 'node:test';
import assert from 'node:assert/strict';
import { encodeSerialCommand, decodeSerialFrame, serialCrc8 } from './hardware-telemetry.mjs';
test('scan adapter preserves keys and rejects unprotected or damaged replies',()=>{
 for(const line of ['VQ 65535 1000 2147483647 1000 2000','SD 65535 2147483647 5']){
  assert.equal(decodeSerialFrame(line),null);
  assert.equal(decodeSerialFrame(`${line}*${serialCrc8(line)}`),line);
  assert.equal(decodeSerialFrame(`${line}*00`),serialCrc8(line)==='00'?line:null);
  assert.equal(decodeSerialFrame(`${line}*GG`),null);
 }
});
test('maximum scan fields fit the real Uno envelope, line and word limits',()=>{
 for(const command of ['SC 3000 3000 60000 600000 10000','SR 2147483647 -64 255 1000 1','VR 65535 1000 2147483647 2 2147483647']){
  const wire=encodeSerialCommand(`CMD 65535 ${command}`);
  assert.ok(wire.length+1<128);
  assert.equal(wire.split('*')[0].split(' ').length,8);
 }
 for(const payload of ['VQ 65535 1000 2147483647 1000 2000','SD 65535 2147483647 5'])assert.ok(payload.length+5<64);
});
