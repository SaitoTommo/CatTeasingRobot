import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import test from 'node:test';
const code=readFileSync(new URL('camera-preview-ui.js',import.meta.url),'utf8');
function fixture(){
 const elements=new Map(),draws=[],boxes=[];
 const $=id=>{if(!elements.has(id))elements.set(id,{checked:true,value:'',style:{},getAttribute(k){return this[k];},getContext(){return {drawImage:(...a)=>draws.push(a),strokeRect:(...a)=>boxes.push(a),fillText(){}};}});return elements.get(id);};
 const context=vm.createContext({$,location:{hostname:'localhost'},fetch:()=>new Promise(()=>{}),setTimeout(){},AbortSignal,createImageBitmap:async()=>({close(){}})});
 vm.runInContext(code,context);return {$,context,draws,boxes,run:s=>vm.runInContext(s,context)};
}
test('every processed frame draws its own box and negatives clear the preceding box',async()=>{
 const f=fixture();let meta={id:1,frameSeq:90,at:1000,width:320,height:240,decodeMs:12,modelMs:180,latencyMs:200,startPeriodMs:400,cat:{valid:true,detected:true,score:.9,box:[10,20,50,70]}};
 f.context.fetch=async()=>({ok:true,status:200,headers:{get:()=>JSON.stringify(meta)},blob:async()=>({})});
 await f.run('cameraPreviewPoll()');assert.deepEqual(f.boxes,[[10,20,40,50]]);assert.equal(f.draws.length,1);assert.equal(f.$('catPreviewCanvas').style.display,'block');assert.equal(f.$('video').src,'');
 assert.match(f.$('catPreviewPerformance').textContent,/模型 180 ms/);
 meta={...meta,id:2,frameSeq:91,cat:{valid:true,detected:false}};
 await f.run('cameraPreviewPoll()');assert.equal(f.boxes.length,1);assert.equal(f.draws.length,2);assert.match(f.$('previewFrame').textContent,/未检测到猫/);
});
test('preview visibility and live video choice are local and leave status updates active',async()=>{
 const f=fixture();let requests=0;f.context.fetch=async()=>{++requests;throw Error('not expected');};
 f.$('showPreview').checked=false;f.run('previewOptionsChanged()');await f.run('cameraPreviewPoll()');assert.equal(requests,0);assert.equal(f.$('video').src,'');assert.equal(f.$('catPreviewCanvas').style.display,'none');
 f.run('syncCameraPreview({ms:1200,config:{visionMaxAgeMs:1500},catPreview:{id:1,at:1000,frameSeq:9,cat:{valid:true,detected:true,score:.8},samples:[]}})');assert.match(f.$('catDetected').textContent,/检测到猫/);assert.match(f.$('previewVisibility').textContent,/已关闭/);
 f.$('showPreview').checked=true;f.$('previewSource').value='live';f.run('previewOptionsChanged()');assert.equal(f.$('video').src,'http://localhost/stream');await f.run('cameraPreviewPoll()');assert.equal(requests,0);
});
test('box toggle repaints a matched frame without starting inference or motion',async()=>{
 const f=fixture();const meta={id:1,frameSeq:2,width:320,height:240,cat:{valid:true,detected:true,box:[1,2,11,12],score:.8}};
 f.context.fetch=async()=>({ok:true,status:200,headers:{get:()=>JSON.stringify(meta)},blob:async()=>({})});
 f.$('showCatBox').checked=false;f.run('previewOptionsChanged()');await f.run('cameraPreviewPoll()');assert.equal(f.boxes.length,0);
 f.$('showCatBox').checked=true;f.run('previewOptionsChanged()');await f.run('cameraPreviewPoll()');assert.equal(f.boxes.length,1);
});
test('timing history includes distinct inferences, deduplicates, and resets on reboot',()=>{
 const f=fixture();const state={ms:2000,catPreview:{id:2,at:1900,frameSeq:12,decodeMs:10,modelMs:180,latencyMs:210,startPeriodMs:400,cat:{valid:true,detected:false},samples:[[1,11,9,175,205,0],[2,12,10,180,210,400]]}};
 f.run(`syncCameraPreview(${JSON.stringify(state)})`);f.run(`syncCameraPreview(${JSON.stringify(state)})`);assert.equal(f.run('previewTimingRows.length'),2);assert.match(f.$('catPreviewTimes').textContent,/2 \/ 12 \/ 10 \/ 180 \/ 210 \/ 400/);
 f.run('syncCameraPreview({ms:20,catPreview:{available:false,samples:[]}})');assert.equal(f.run('previewTimingRows.length'),0);assert.match(f.$('catDetected').textContent,/尚无猫识别/);
});
test('a late decoded JPEG cannot restore the preview after a device reboot',async()=>{
 const f=fixture();let finish;f.context.createImageBitmap=()=>new Promise(resolve=>finish=resolve);
 f.context.fetch=async()=>({ok:true,status:200,headers:{get:()=>JSON.stringify({id:9,width:320,height:240,cat:{valid:true,detected:true,box:[1,2,3,4]}})},blob:async()=>({})});
 f.run('syncCameraPreview({ms:1000})');const pending=f.run('cameraPreviewPoll()');await new Promise(resolve=>setImmediate(resolve));f.run('syncCameraPreview({ms:10})');let closed=false;finish({close(){closed=true;}});await pending;assert.equal(f.draws.length,0);assert.ok(closed);
});

test('camera settings revision rejects a late old JPEG and clears stale detection even when uptime grows',async()=>{
 const f=fixture();let finish;f.context.createImageBitmap=()=>new Promise(resolve=>finish=resolve);
 f.context.fetch=async()=>({ok:true,status:200,headers:{get:()=>JSON.stringify({id:9,width:320,height:240,cat:{valid:true,detected:true}})},blob:async()=>({})});
 f.run('syncCameraPreview({bootId:"a",ms:1000,cameraSettings:{revision:0},catPreview:{id:8,at:900,cat:{valid:true,detected:true},samples:[[8,1,10,100,110,0]]}})');
 const pending=f.run('cameraPreviewPoll()');await new Promise(resolve=>setImmediate(resolve));
 f.run('syncCameraPreview({bootId:"a",ms:2000,cameraSettings:{revision:1},catPreview:{available:false,samples:[]}})');
 let closed=false;finish({close(){closed=true;}});await pending;
 assert.equal(f.draws.length,0);assert.ok(closed);assert.equal(f.run('previewTimingRows.length'),0);assert.match(f.$('catDetected').textContent,/尚无猫识别/);
 f.run('syncCameraPreview({bootId:"a",ms:2100,cameraSettings:{revision:1},catPreview:{id:10,at:2050,cat:{valid:true,detected:false},samples:[[10,2,10,100,110,0]]}})');assert.equal(f.run('previewTimingMissed'),0);
 f.run('syncCameraPreview({bootId:"b",ms:3000,cameraSettings:{revision:0},catPreview:{available:false,samples:[]}})');assert.equal(f.run('previewSettingsIdentity'),'b/0');
});
test('desktop centers camera and avoidance; mobile places UART, camera and remote first',()=>{
 const html=readFileSync(new URL('ESP32CatHost/debug_page.html',import.meta.url),'utf8');
 const main=html.slice(html.indexOf('<main>'),html.indexOf('</main>'));
 assert.ok(main.indexOf('id="controlPanel"')<main.indexOf('id="cameraPanel"'));
 assert.ok(main.indexOf('id="cameraPanel"')<main.indexOf('id="remotePanel"'));
 assert.ok(html.includes('grid-template-areas:"uart preview remote"'));
 assert.ok(html.includes('grid-template-areas:"uart" "camera" "remote" "avoidance"'));
 assert.ok(main.indexOf('id="cameraPanel"')<main.indexOf('id="avoidancePanel"'));
 assert.ok(main.indexOf('id="avoidancePanel"')<main.indexOf('id="remotePanel"'));
 assert.ok(main.indexOf('id="tagDetectionPanel"')<main.indexOf('id="sensorPanel"'));
 assert.ok(main.indexOf('id="controlPanel"')<main.indexOf('id="cameraPanel"'));
 assert.ok(main.indexOf('id="cameraPanel"')<main.indexOf('id="settingsWorkspace"'));
 assert.ok(main.lastIndexOf('<section')===main.indexOf('<section id="performancePanel"'));
 assert.ok(main.indexOf('id="catPreviewCanvas"')<main.indexOf('id="catPreviewPerformance"'));
});

test('preview skips absent and unchanged frames, pauses offline, and resumes on a new result',async()=>{
 const f=fixture();let requests=0;
 f.context.deviceOnline=true;
 let meta={id:1,frameSeq:9,width:320,height:240,cat:{valid:true,detected:false}};
 f.context.fetch=async()=>{requests++;return {ok:true,status:200,headers:{get:()=>JSON.stringify(meta)},blob:async()=>({})};};
 f.run('syncCameraPreview({ms:1000,catPreview:{available:false}})');await f.run('cameraPreviewPoll()');assert.equal(requests,0);
 f.run('syncCameraPreview({ms:1100,catPreview:{id:1,available:true}})');await f.run('cameraPreviewPoll()');assert.equal(requests,1);
 await f.run('cameraPreviewPoll()');assert.equal(requests,1);
 meta={...meta,id:2};f.context.deviceOnline=false;
 f.run('syncCameraPreview({ms:1200,catPreview:{id:2,available:true}})');await f.run('cameraPreviewPoll()');assert.equal(requests,1);
 f.context.deviceOnline=true;await f.run('cameraPreviewPoll()');assert.equal(requests,2);
});

test('a JPEG finishing after a lost state connection is discarded',async()=>{
 const f=fixture();let finish,closed=false;f.context.deviceOnline=true;
 f.context.createImageBitmap=()=>new Promise(resolve=>finish=resolve);
 f.context.fetch=async()=>({ok:true,status:200,headers:{get:()=>JSON.stringify({id:1,width:320,height:240,cat:{}})},blob:async()=>({})});
 const pending=f.run('cameraPreviewPoll()');await new Promise(resolve=>setImmediate(resolve));
 f.context.deviceOnline=false;finish({close(){closed=true;}});await pending;
 assert.equal(f.draws.length,0);assert.ok(closed);
});
