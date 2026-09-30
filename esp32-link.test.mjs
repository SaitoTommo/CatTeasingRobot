import test from 'node:test';
import assert from 'node:assert/strict';
import { validateRoute, decodeHostCommand, compactReply } from './esp32-link.mjs';
test('route preserves arbitrary ordered IDs and repeat visits',()=>{
  assert.deepEqual(validateRoute([41,2,100,41,586,0]),[41,2,100,41,586,0]);
  for(const r of [[],[-1],[587],[1.2],['1'],Array(129).fill(0)]) assert.throws(()=>validateRoute(r));
});
test('host command is accepted only after exact CRC check',()=>{
  assert.equal(decodeHostCommand(compactReply('C 8 CF INVALID')),'CMD 8 CAMERA_FRONT INVALID');
  assert.throws(()=>decodeHostCommand('C 8 MOTOR 90 90*00'));
});
