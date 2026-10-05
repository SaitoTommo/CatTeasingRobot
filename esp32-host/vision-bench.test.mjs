import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import test from 'node:test';
const source=readFileSync(new URL('./vision-bench-ui.js',import.meta.url),'utf8');
const elements=new Map();const $=id=>{if(!elements.has(id))elements.set(id,{value:'',textContent:'',style:{}} );return elements.get(id);};
const ctx=vm.createContext({$,fetch:()=>new Promise(()=>{}),setTimeout(){},AbortSignal,location:{hostname:"localhost"}});vm.runInContext(source,ctx);
test('latency statistics handle even medians, outliers and nearest-rank P95',()=>{
 const result=vm.runInContext('benchStats([1,2,3,100])',ctx);assert.equal(result.mean,26.5);assert.equal(result.median,2.5);assert.equal(result.p95,100);assert.equal(result.min,1);assert.equal(result.max,100);
 assert.equal(vm.runInContext('benchStats([])',ctx),null);assert.equal(vm.runInContext('benchStats(Array.from({length:100},(_,i)=>i+1)).p95',ctx),95);
});
test('history deduplicates repeated replies and exposes missing records and epoch reset',()=>{
 vm.runInContext('mergeBench({epoch:1,enabled:true,gapMs:0,captureGapMs:0,count:4,samples:[[3,30,1,8,10,20,10,15],[4,31,1,8,10,20,10,15]]})',ctx);
 assert.equal(vm.runInContext('benchRows.length',ctx),2);assert.equal(vm.runInContext('benchMissed',ctx),2);
 vm.runInContext('mergeBench({epoch:1,enabled:true,gapMs:0,captureGapMs:0,count:4,samples:[[4,31,1,8,10,20,10,15]]})',ctx);assert.equal(vm.runInContext('benchRows.length',ctx),2);
 vm.runInContext('mergeBench({epoch:2,enabled:true,gapMs:100,captureGapMs:0,count:1,samples:[[1,40,1,8,10,0,0,15]]})',ctx);assert.equal(vm.runInContext('benchRows.length',ctx),1);assert.equal(vm.runInContext('benchMissed',ctx),0);
});
test('zero cooldown is sent unchanged without a motion command',async()=>{
 const requests=[];ctx.send=async body=>requests.push(body);ctx.alert=message=>{throw Error(message);};$('benchGap').value='0';$('benchCaptureGap').value='0';
 await vm.runInContext('setBench(true,true)',ctx);assert.equal(requests.length,1);assert.equal(requests[0].op,'vision_bench');assert.equal(requests[0].gapMs,0);assert.equal(requests[0].captureGapMs,0);
});
test('overlay uses metadata paired with the JPEG and clears a prior cat on a negative frame',async()=>{
 const strokes=[],draws=[];const canvas=$('benchCanvas');canvas.getContext=()=>({drawImage:(...args)=>draws.push(args),strokeRect:(...args)=>strokes.push(args),fillText(){}});
 ctx.createImageBitmap=async()=>({close(){}});let meta={epoch:2,id:9,frameSeq:90,width:320,height:240,cat:{detected:true,score:.9,box:[10,20,50,70]}};
 ctx.fetch=async()=>({ok:true,headers:{get:()=>JSON.stringify(meta)},blob:async()=>({})});
 await vm.runInContext('benchFramePoll()',ctx);assert.deepEqual(strokes,[[10,20,40,50]]);assert.equal(draws.length,1);assert.equal(canvas.width,320);
 meta={...meta,id:10,frameSeq:91,cat:{detected:false}};await vm.runInContext('benchFramePoll()',ctx);assert.equal(strokes.length,1);assert.equal(draws.length,2);assert.match($('benchFrame').textContent,/未检测到猫/);
});
