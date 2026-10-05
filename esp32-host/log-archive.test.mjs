import test from 'node:test';
import assert from 'node:assert/strict';
import {LocalLogArchive} from './log-archive.mjs';
function disk(){return {rows:new Map(),state:{},fail:false,readFail:false,
 async restore(){return {...structuredClone(this.state),count:this.rows.size,recent:[...this.rows.values()].slice(-500)};},
 async commit(rows,state){if(this.fail)throw Error('QuotaExceededError');for(const row of rows)if(!this.rows.has(row.key))this.rows.set(row.key,structuredClone(row));this.state=structuredClone(state);return this.rows.size;},
 async visit(append){if(this.readFail)throw Error('disk read failed');for(const row of this.rows.values())append(row);}
};}
const state=(boot,seq,from=1)=>({deviceId:'esp-a',bootId:boot,ms:seq*10,firmware:'test',logSeq:seq,
 logs:Array.from({length:seq-from+1},(_,i)=>({seq:from+i,ms:(from+i)*10,source:'UNO',line:'row '+(from+i)}))});
const exported=async archive=>JSON.parse((await archive.exportChunks()).join(''));
test('durable history exports all 1200 records after reload, despite 500-row display and ESP reboot',async()=>{
 const store=disk(),a=new LocalLogArchive(store);await a.ready;
 for(let i=1;i<=1200;i+=40)await a.ingest(state('boot-a',Math.min(1200,i+39),i));
 assert.equal(a.recent.length,500);await a.ingest(state('boot-b',3));
 const b=new LocalLogArchive(store);await b.ready;await b.ingest(state('boot-b',3));
 const data=await exported(b),logs=data.logs.filter(r=>r.source==='UNO');assert.equal(logs.length,1203);
 assert.equal(logs.filter(r=>r.seq===1).length,2);assert.equal(data.lastState.bootId,'boot-b');
 assert.equal(data.logs.filter(r=>r.source==='LOCAL_BOOT').length,2);
});
test('captured data is copied before page merge; duplicate responses and reconnects do not erase logs',async()=>{
 const store=disk(),a=new LocalLogArchive(store);const d=state('one',3);const wait=a.ingest(d);d.logs=[];await wait;
 await a.connectionLost('ESP disconnected','2026-10-06T00:01:00.000Z');await a.ingest(state('one',3));
 const data=await exported(a);assert.equal(data.logs.filter(r=>r.source==='UNO').length,3);assert.ok(data.logs.some(r=>r.source==='LOCAL_DISCONNECT'));
});
test('missed ring-buffer sequences are marked and separate device/boot identities never collide',async()=>{
 const a=new LocalLogArchive(disk());await a.ingest(state('one',3));await a.ingest(state('one',200,181));
 await a.ingest({...state('one',3),deviceId:'esp-b'});await a.ingest(state('two',1));
 const data=await exported(a);assert.ok(data.logs.some(r=>r.source==='LOCAL_GAP'&&r.line.includes('4–180')));
 assert.equal(data.logs.filter(r=>r.seq===1).length,3);
});
test('quota failures preserve pending records for full export and retry without duplicate durable rows',async()=>{
 const store=disk(),a=new LocalLogArchive(store);await a.ingest(state('one',3));store.fail=true;
 await a.ingest(state('one',5,4));assert.equal(a.pending.length,2);assert.match(a.error,/Quota/);
 let data=await exported(a);assert.equal(data.logs.filter(r=>r.source==='UNO').length,5);assert.match(data.storageError,/Quota/);
 store.fail=false;data=await exported(a);assert.equal(a.pending.length,0);assert.equal(data.logs.filter(r=>r.source==='UNO').length,5);
 const b=new LocalLogArchive(store);await b.ready;assert.equal((await exported(b)).logs.filter(r=>r.source==='UNO').length,5);
});
test('a read error rejects export instead of reporting a partial download as complete',async()=>{
 const store=disk(),a=new LocalLogArchive(store);await a.ingest(state('one',3));store.readFail=true;
 await assert.rejects(a.exportChunks(),/disk read failed/);
});
test('camera revision changes are recorded with actual and saved parameters',async()=>{
 const a=new LocalLogArchive(disk());await a.ingest({...state('one',1),cameraSettings:{revision:0,actual:{quality:12},persisted:false}});
 await a.ingest({...state('one',2,2),cameraSettings:{revision:1,actual:{quality:8},persisted:true}});
 const rows=(await exported(a)).logs.filter(r=>r.source==='LOCAL_CAMERA_SETTINGS');assert.equal(rows.length,2);assert.equal(rows[1].cameraSettings.actual.quality,8);
});
test('export serializes concurrent ingestion and an export error does not poison later saves',async()=>{
 const store=disk(),a=new LocalLogArchive(store);await a.ingest(state('one',3));
 let release,entered;const waiting=new Promise(r=>entered=r),original=store.visit.bind(store);
 store.visit=async append=>{entered();await new Promise(r=>release=r);await original(append);};
 const exporting=exported(a);await waiting;const ingesting=a.ingest(state('one',5,4));
 release();assert.equal((await exporting).logs.filter(r=>r.source==='UNO').length,3);await ingesting;
 store.visit=original;assert.equal((await exported(a)).logs.filter(r=>r.source==='UNO').length,5);
 store.readFail=true;await assert.rejects(exported(a));store.readFail=false;await a.ingest(state('one',6,6));
 assert.equal((await exported(a)).logs.filter(r=>r.source==='UNO').length,6);
});
