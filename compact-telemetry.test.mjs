import test from 'node:test';
import assert from 'node:assert/strict';
import { CompactTelemetry, compactCrc } from './compact-telemetry.mjs';
import { HardwareTelemetry, decodeSerialFrame } from './hardware-telemetry.mjs';
function fixture(id=3154){
  const bytes=new Uint8Array(50),v=new DataView(bytes.buffer);
  bytes[0]=1;v.setUint32(1,123456,true);v.setUint16(6,16383,true);
  v.setInt16(8,-45,true);v.setInt16(10,45,true);v.setInt16(12,-120,true);v.setInt16(14,120,true);
  v.setUint32(18,4294967295,true);v.setUint32(22,123,true);v.setFloat32(26,12.5,true);v.setFloat32(30,27.25,true);
  for(const [offset,n] of [[34,291],[36,291],[38,575],[40,575],[48,-10]])v.setInt16(offset,n,true);
  bytes[16]=6;bytes[17]=3;bytes[44]=15;bytes[47]=0;
  const protect=s=>`${s}*${compactCrc(s)}`;
  return [...[0,1,2].map(part=>protect(`TB ${id} ${part} ${Array.from(bytes.slice(part*17,part*17+17),b=>b.toString(16).padStart(2,'0').toUpperCase()).join('')}`)),protect(`DONE ${id} TELEMETRY`)];
}
test('all sensors publish atomically only after a complete checked batch',()=>{
  const hw=new HardwareTelemetry(),frames=fixture();
  for(const frame of frames.slice(0,3)){assert.equal(hw.accept(frame,100),null);assert.equal(hw.rows.size,0);assert.ok(frame.length+2<=50);assert.notEqual(decodeSerialFrame(frame),null);}
  assert.equal(hw.accept(frames[3],120).kind,'STATE');assert.equal(hw.rows.size,13);
  assert.equal(hw.rows.get('EL').values[2],'4294967295');assert.equal(hw.rows.get('ML').values[1],'-120');
  assert.equal(hw.rows.get('SR').values[3],'57.5');assert.deepEqual(hw.rows.get('FRONT').values,['-1.0','HOST']);
  assert.ok([...hw.rows.values()].every(row=>row.at===120));
  const compactBytes=frames.reduce((n,s)=>n+s.length+2,0);
  const legacyBytes=[...hw.rows.values()].reduce((n,r)=>n+`DATA 3154 HW ${r.kind} ${r.values.join(' ')}\r\n`.length,0);
  assert.ok(compactBytes<legacyBytes/2);
});
test('bad CRC, incomplete, wrong id, conflicting duplicate, timeout and missing CRC never refresh rows',()=>{
  for(const corrupt of [f=>[f[0],f[2],f[3]],f=>[f[0],f[1],f[2],fixture(3155)[3]],f=>[f[0].slice(0,-2)+'FF',f[1],f[2],f[3]],f=>f.map(x=>x.split('*')[0]),f=>[f[0],fixture(3155)[0],f[1],f[2],f[3]]]){
    const hw=new HardwareTelemetry();hw.accept('DATA 1 HW FRONT 7.0 HOST',1);
    for(const frame of corrupt(fixture()))hw.accept(frame,100);
    assert.equal(hw.rows.size,1);assert.equal(hw.rows.get('FRONT').at,1);
  }
  const c=new CompactTelemetry(),f=fixture();c.accept(f[0],0);c.accept(f[1],1);c.accept(f[2],2);assert.equal(c.accept(f[3],5001),null);
  c.accept(f[0],0);const changed=f[0].slice(0,-3).replace(/01/,'02');c.accept(changed+'*'+compactCrc(changed),1);c.accept(f[1],2);c.accept(f[2],3);assert.equal(c.accept(f[3],4),null);
  assert.equal(decodeSerialFrame(f[0].split('*')[0]),null);
});
