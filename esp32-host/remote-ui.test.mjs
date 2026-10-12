import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import test from 'node:test';
import assert from 'node:assert/strict';
function fixture(){
 const elements=new Map(),requests=[],events={};
 function element(id){if(!elements.has(id))elements.set(id,{value:id==='remoteSpeed'?'100':'',textContent:'',children:[],disabled:false,append(x){this.children.push(x);},replaceChildren(){this.children=[];}});return elements.get(id);}
 const document={hidden:false,getElementById:element,createElement:()=>({}),querySelectorAll:()=>[],addEventListener:(name,fn)=>events[name]=fn};
 const c=vm.createContext({document,Date,Math,setTimeout:()=>0,console,send:async d=>{requests.push(d);return true;}});
 vm.runInContext("const $=id=>document.getElementById(id);let deviceOnline=true,pageState=null;function visionAge(d,v){return Number.isFinite(v?.at)?d.ms-v.at:Infinity;}",c);
 vm.runInContext(readFileSync(new URL('./remote-ui.js',import.meta.url),'utf8'),c);
 const run=code=>vm.runInContext(code,c);
 const client=run('remoteClient');
 function draw(override={}){const d={ms:1000,mode:'SIM_UNO',phase:'STOPPED',firmware:'ESP32_CAT_HOST_0.43_REMOTE_RESUME',patrolStatus:{targetIndex:1,targetId:46},config:{route:[9,46,46],visionMaxAgeMs:1500},unoConfig:{},...override};run('pageState='+JSON.stringify(d)+';syncRemote(pageState)');return d;}
 function active(){draw({phase:'REMOTE',remote:{active:true,busy:true,client,session:'lease',capable:true}});}
 return {run,draw,active,element,requests,events,document};
}
test('both cards select the same occurrence and send route-index resume without remote mode',async()=>{
 const f=fixture();f.draw();assert.equal(f.element('patrolTarget').value,'1');assert.equal(f.element('remoteTarget').value,'1');
 f.run('routeSelect(2)');assert.equal(f.element('patrolTarget').value,'2');await f.run('resumeRoute()');assert.equal(f.requests.at(-1).index,2);assert.equal(f.requests.at(-1).action,'resume_from');assert.deepEqual(Array.from(f.requests.at(-1).route),[9,46,46]);
 f.run("routeFilter('999')");assert.equal(f.element('patrolResume').disabled,true);assert.equal(f.element('remoteResume').disabled,true);
 f.draw({config:{route:[20]}});assert.equal(f.element('patrolTarget').value,'0');assert.match(f.element('remoteResume').textContent,/20/);
});
test('pointer hold and release emit ordered movement; an outstanding request cannot replay released motion',async()=>{
 const f=fixture();f.active();await f.run("remoteDirection='forward';remotePointer=1;remoteBeat()");assert.equal(f.requests.at(-1).direction,'forward');assert.equal(f.requests.at(-1).pwm,100);
 f.run('remoteRelease({pointerId:2})');assert.equal(f.run('remoteDirection'),'forward');f.run('remoteRelease({pointerId:1})');assert.equal(f.requests.at(-1).action,'move_stop');assert.equal(f.run('remoteDirection'),'');
 const before=f.requests.length;await f.run('remoteBeat()');assert.equal(f.requests.length,before+1);assert.equal(f.requests.at(-1).action,'heartbeat');
 const sequences=f.requests.map(x=>x.seq);assert.deepEqual(sequences,[...sequences].sort((a,b)=>a-b));
});
test('buzzer release is independent of movement and session change cancels both holds',async()=>{
 const f=fixture();f.active();f.run("remoteDirection='left';remotePointer=1;remoteBeep=true;beepPointer=2");f.run('remoteRelease({pointerId:2})');assert.equal(f.run('remoteDirection'),'left');assert.equal(f.requests.at(-1).action,'beep_hold');assert.equal(f.requests.at(-1).held,false);
 f.draw({remote:{active:true,busy:true,client:f.run('remoteClient'),session:'new',capable:true}});assert.equal(f.run('remoteDirection'),'');assert.equal(f.run('remoteBeep'),false);
});
test('hidden page exits and foreign ownership cannot issue remote commands',async()=>{
 const f=fixture();f.active();f.document.hidden=true;f.events.visibilitychange();assert.equal(f.requests.at(-1).action,'exit');assert.equal(f.run('remoteState'),null);
 f.draw({remote:{active:true,busy:true,client:'other',session:'foreign'}});const before=f.requests.length;await f.run("remoteSend('light')");assert.equal(f.requests.length,before);
});
test('expired detections and lost links do not show clearance or current tags',()=>{
 const f=fixture();f.draw({cameraVision:{at:0,tagsValid:true,tags:[{id:46}],source:'OV_CAMERA'},ms:4000});assert.match(f.element('tagDetectionStatus').textContent,/已过期/);
 f.run('remoteOffline()');assert.match(f.element('tagDetectionStatus').textContent,/连接中断/);assert.equal(f.element('enterRemote').disabled,true);
});
test('rendered template contains unique status and control IDs and both entry points',()=>{
 const html=readFileSync(new URL('./ESP32CatHost/debug_page.html',import.meta.url),'utf8');const ids=[...html.matchAll(/\bid="([^"]+)"/g)].map(m=>m[1]);assert.equal(new Set(ids).size,ids.length);
 for(const id of ['controlPanel','remotePanel','patrolResume','remoteResume','avoidancePanel','catDetectionPanel','tagDetectionPanel'])assert.ok(ids.includes(id));
});

test('custom route accepts ordered comma/space/arrow and JSON input and validates tag bounds',()=>{
 const f=fixture();for(const text of ['43, 12, 43','43 12 43','43→12→43','[43,12,43]','43，12，43'])assert.deepEqual(Array.from(f.run('parseCustomRoute('+JSON.stringify(text)+')')),[43,12,43]);
 for(const text of ['', '-1,43','587','1.5','[1.5]','["43"]','1e2','1x','null','[1,]'])assert.throws(()=>f.run('parseCustomRoute('+JSON.stringify(text)+')'));
 assert.throws(()=>f.run('parseCustomRoute('+JSON.stringify(Array(129).fill(1).join(','))+')'));
});
test('custom route draft is synced between cards, preserved by polling, and committed only on matched board confirmation',async()=>{
 const f=fixture();f.draw();f.run("editRouteDraft('43, 12, 43')");f.draw();assert.equal(f.element('remoteRouteDraft').value,'43, 12, 43');assert.deepEqual(Array.from(f.run('routeNodes')),[9,46,46]);
 await f.run('saveCustomRoute()');const request=f.requests.at(-1);assert.equal(request.op,'route_save');assert.deepEqual(Array.from(request.route),[43,12,43]);assert.match(f.element('patrolRouteSaveStatus').textContent,/等待 ESP/);
 f.draw({config:{route:[43,12,43]},routeSave:{requestId:'foreign',status:'saved'}});assert.notEqual(f.run('routeSavePending'),null);
 f.draw({config:{route:[43,12,43]},routeSave:{requestId:request.requestId,status:'saved'}});assert.equal(f.run('routeSavePending'),null);assert.match(f.element('remoteRouteSaveStatus').textContent,/已保存/);
 f.run('routeSelect(2)');await f.run('resumeRoute()');assert.equal(f.requests.at(-1).index,2);assert.deepEqual(Array.from(f.requests.at(-1).route),[43,12,43]);
});
test('custom route saving refuses active patrol, reports NVS failure, and retains editable draft',async()=>{
 const f=fixture();f.draw({phase:'PATROL'});f.run("editRouteDraft('43,12')");await f.run('saveCustomRoute()');assert.equal(f.requests.length,0);assert.equal(f.element('remoteSaveRoute').disabled,true);
 f.draw();await f.run('saveCustomRoute()');const request=f.requests.at(-1);
 f.draw({routeSave:{requestId:request.requestId,status:'error',error:'NVS save failed'}});assert.match(f.element('patrolRouteSaveStatus').textContent,/NVS save failed/);assert.equal(f.element('remoteRouteDraft').value,'43,12');assert.deepEqual(Array.from(f.run('routeNodes')),[9,46,46]);
 f.run('remoteOffline()');assert.equal(f.element('patrolSaveRoute').disabled,true);
});

test('saved 43,46,51 shows all three nodes including last and resumes its original position',async()=>{
 const f=fixture();f.element('settings').value=JSON.stringify({route:[43,46],minimumDrivePwm:140});f.draw({config:{route:[43,46,51]}});
 for(const name of ['patrol','remote']){assert.equal(f.element(name+'Target').children.length,3);assert.equal(f.element(name+'Target').children.at(-1).value,'2');assert.match(f.element(name+'Target').children.at(-1).textContent,/51/);}
 const settings=JSON.parse(f.element('settings').value);assert.deepEqual(settings.route,[43,46,51]);assert.equal(settings.minimumDrivePwm,140);
 f.run('routeSelect(2)');await f.run('resumeRoute()');assert.equal(f.requests.at(-1).index,2);assert.deepEqual(Array.from(f.requests.at(-1).route),[43,46,51]);
});
