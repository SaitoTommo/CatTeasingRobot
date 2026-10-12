import { readFileSync } from 'node:fs';
import { gunzipSync } from 'node:zlib';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import test from 'node:test';

const root=new URL('./ESP32CatHost/',import.meta.url);
const header=readFileSync(new URL('debug_page.h',root),'utf8');
const html=gunzipSync(Buffer.from(header.split('{')[1].split('}')[0].match(/\d+/g).map(Number))).toString();
const elements=new Map();
function element(id){if(!elements.has(id))elements.set(id,{value:'',textContent:'',style:{},attrs:{},getAttribute(k){return k==='src'?this.src:this.attrs[k];},append(){},setAttribute(k,v){this.attrs[k]=v;}});return elements.get(id);}
const context=vm.createContext({document:{getElementById:element,createElement:()=>({append(){},setAttribute(){}}),querySelectorAll:()=>[]},location:{hostname:'localhost'},fetch:()=>new Promise(()=>{}),setTimeout(){},AbortSignal,TextEncoder,alert(){}});
vm.runInContext(html.match(/<script>([\s\S]*)<\/script>/)[1].replace(/\b(?:poll|flowPoll|benchPoll|benchFramePoll|cameraPreviewPoll)\(\);/g,''),context);
const base={ms:1000,mode:'UART',phase:'STOPPED',lastUnoAgeMs:999999,unoTelemetry:[],unoConfig:{},config:{}};
const sync=d=>vm.runInContext(`syncUno(${JSON.stringify(d)})`,context);

test('front parameters explain effects, retain drafts and save only after checked parking and board readback',async()=>{
 const requests=[];context.fetch=async(url,options)=>{requests.push(JSON.parse(options.body));return {ok:true};};
 const state={...base,cameraOk:true,logs:[],config:{frontAvoidanceEnabled:false,floorDifferenceThreshold:35,floorRoiTop:.75,frontBlockBottom:.88,visionMaxAgeMs:1500}};
 const draw=d=>vm.runInContext(`syncFrontControls(${JSON.stringify(d)})`,context);
 draw(state);assert.equal(element('frontCfg_floorDifferenceThreshold').value,35);
 element('frontCfg_floorDifferenceThreshold').value='25';element('frontCfg_floorRoiTop').value='0.8';element('frontCfg_frontBlockBottom').value='0.9';element('frontCfg_visionMaxAgeMs').value='1200';
 draw(state);assert.equal(element('frontCfg_floorDifferenceThreshold').value,'25');
 await vm.runInContext('saveFrontParameters()',context);assert.deepEqual(requests.at(-1),{op:'config',config:{floorDifferenceThreshold:25,floorRoiTop:.8,frontBlockBottom:.9,visionMaxAgeMs:1200}});
 draw(state);assert.match(element('frontParametersStatus').textContent,/等待板上保存回读/);
 draw({...state,config:{...state.config,...requests.at(-1).config}});assert.match(element('frontParametersStatus').textContent,/回读一致/);
 for(const override of [{phase:'PATROL'},{stopPendingId:1},{stopUnconfirmed:true},{debugBusy:true},{frontCalibration:{pending:true}}]){draw({...state,...override});const n=requests.length;await vm.runInContext('saveFrontParameters()',context);assert.equal(requests.length,n);}
 draw(state);element('frontCfg_floorRoiTop').value='0.1';const n=requests.length;await vm.runInContext('saveFrontParameters()',context);assert.equal(requests.length,n);
 for(const text of ['3×8','调小更敏感','必须重新采集','不参与 FLOOR 判断','不是障碍敏感度'])assert.ok(html.includes(text));
 vm.runInContext('frontControlsOffline()',context);assert.equal(element('saveFrontParameters').disabled,true);
 context.fetch=()=>new Promise(()=>{});
});

test('front parameter failure does not claim persistence and reboot reloads board values',async()=>{
 context.fetch=async()=>({ok:true});
 const state={...base,bootId:'front-first',logs:[],config:{frontAvoidanceEnabled:true,floorDifferenceThreshold:35,floorRoiTop:.75,frontBlockBottom:.88,visionMaxAgeMs:1500}};
 const draw=d=>vm.runInContext(`syncFrontControls(${JSON.stringify(d)})`,context);
 draw(state);for(const [key,value] of Object.entries(state.config))if(key!=='frontAvoidanceEnabled')element('frontCfg_'+key).value=String(value);
 element('frontCfg_floorDifferenceThreshold').value='25';await vm.runInContext('saveFrontParameters()',context);
 draw({...state,config:{...state.config,floorDifferenceThreshold:25},logs:[{seq:1,source:'CONFIG_ERROR',line:'NVS save failed; RAM applied but restart persistence unconfirmed'}]});
 assert.match(element('frontParametersStatus').textContent,/保存失败.*NVS/);
 draw({...state,bootId:'front-second'});assert.equal(element('frontCfg_floorDifferenceThreshold').value,35);assert.doesNotMatch(element('frontParametersStatus').textContent,/回读一致|保存失败/);
 context.fetch=()=>new Promise(()=>{});
});

test('tag recovery tuning keeps drafts, gates saves on confirmed stop and waits for board readback',async()=>{
 const requests=[];context.fetch=async(url,options)=>{requests.push(JSON.parse(options.body));return {ok:true};};
 const state={...base,config:{tagLossGraceMs:1000,tagRecoveryAngleDeg:60},logs:[],patrolStatus:{tagRecoveryStage:'GRACE',tagMemoryDirection:-1,tagLossRemainingMs:800}};
 const draw=d=>vm.runInContext(`syncTagRecovery(${JSON.stringify(d)})`,context);
 draw(state);assert.equal(element('saveTagRecovery').disabled,false);assert.match(element('tagRecoveryLive').textContent,/左/);assert.match(element('tagRecoveryLive').textContent,/800/);
 element('tagLossGraceMs').value='1500';element('tagRecoveryAngleDeg').value='45';draw(state);assert.equal(element('tagLossGraceMs').value,'1500');
 vm.runInContext('saveTagRecovery()',context);assert.deepEqual(requests.at(-1),{op:'config',config:{tagLossGraceMs:1500,tagRecoveryAngleDeg:45}});
 draw(state);assert.match(element('tagRecoveryStatus').textContent,/等待板上保存回读/);
 draw({...state,config:{tagLossGraceMs:1500,tagRecoveryAngleDeg:45}});assert.doesNotMatch(element('tagRecoveryStatus').textContent,/等待板上保存回读/);
 for(const override of [{phase:'PATROL'},{stopPendingId:1},{stopUnconfirmed:true},{debugBusy:true},{config:{}}]){draw({...state,...override});const n=requests.length;vm.runInContext('saveTagRecovery()',context);assert.equal(requests.length,n);assert.equal(element('saveTagRecovery').disabled,true);}
 draw(state);for(const [id,v] of [['tagLossGraceMs','1.5'],['tagLossGraceMs','3001'],['tagLossGraceMs','-1'],['tagRecoveryAngleDeg','19'],['tagRecoveryAngleDeg','121']]){element('tagLossGraceMs').value='1000';element('tagRecoveryAngleDeg').value='60';element(id).value=v;const n=requests.length;vm.runInContext('saveTagRecovery()',context);assert.equal(requests.length,n);}
 element('tagLossGraceMs').value='0';element('tagRecoveryAngleDeg').value='120';vm.runInContext('saveTagRecovery()',context);assert.equal(requests.at(-1).config.tagLossGraceMs,0);
 draw({...state,logs:[{seq:999999,source:'CONFIG_ERROR',line:'NVS failed'}]});assert.match(element('tagRecoveryStatus').textContent,/NVS failed/);
 context.fetch=async()=>{throw Error('offline');};await vm.runInContext('poll()',context);assert.equal(element('saveTagRecovery').disabled,true);assert.match(element('tagRecoveryLive').textContent,/连接中断/);
 context.fetch=()=>new Promise(()=>{});vm.runInContext('pollFailures=0;deviceOnline=false',context);
 assert.ok(html.includes('三个阶段合计最多约两圈'));
});

test('camera tuning preserves draft, requires confirmed parking and reports actual matching request readback',async()=>{
 const requests=[];context.fetch=async(url,options)=>{requests.push(JSON.parse(options.body));return {ok:true};};
 const actual=Object.fromEntries(vm.runInContext('Object.entries(cameraFields).map(([key,v])=>[key,v[1]])',context));
 Object.assign(actual,{quality:12,aec:1,agc:1,aec_value:300});
 const state={...base,bootId:'test-boot',cameraOk:true,visionBench:{enabled:false},cameraSettings:{available:true,revision:0,actual,persisted:false,sensorPid:3660}};
 const draw=d=>vm.runInContext(`syncCameraSettings(${JSON.stringify(d)})`,context);
 draw(state);assert.equal(element('cameraCfg_aec_value').disabled,true);assert.equal(element('applyCameraSettings').disabled,false);
 element('cameraCfg_quality').value='8';draw(state);assert.equal(element('cameraCfg_quality').value,'8');
 await vm.runInContext('applyCameraSettings()',context);assert.equal(requests.at(-1).settings.quality,8);assert.equal(element('applyCameraSettings').disabled,true);
 draw(state);assert.match(element('cameraSettingsStatus').textContent,/等待板上/);
 const saved={...state,cameraSettings:{...state.cameraSettings,revision:1,actual:{...actual,quality:9},persisted:true,requestId:requests.at(-1).requestId}};
 draw(saved);assert.equal(Number(element('cameraCfg_quality').value),9);assert.match(element('cameraSettingsStatus').textContent,/已应用并保存/);
 for(const override of [{phase:'TAG_SEARCH'},{stopPendingId:1},{stopUnconfirmed:true},{debugBusy:true},{visionBench:{enabled:true}}]){
  draw({...state,...override});const n=requests.length;await vm.runInContext('applyCameraSettings()',context);assert.equal(requests.length,n);assert.equal(element('applyCameraSettings').disabled,true);
 }
 draw(saved);element('cameraCfg_quality').value='7.5';const before=requests.length;await vm.runInContext('applyCameraSettings()',context);assert.equal(requests.length,before);
 await vm.runInContext('applyCameraSettings(true)',context);assert.equal(requests.at(-1).op,'camera_reset');
 draw({...saved,cameraSettings:{...saved.cameraSettings,requestId:requests.at(-1).requestId,error:'NVS save failed'}});assert.match(element('cameraSettingsStatus').textContent,/NVS save failed/);
 vm.runInContext('cameraOffline()',context);assert.equal(element('cameraCfg_quality').disabled,true);assert.equal(element('applyCameraSettings').disabled,true);
 context.fetch=()=>new Promise(()=>{});
});

test('camera epoch and boot changes reset drafts and current diagnostic log cursor independently of uptime',()=>{
 vm.runInContext('pageLogs=[];pageLogAfter=0;pageLogMs=0;pageLogBoot=null',context);
 vm.runInContext('mergePageLogs({bootId:"old",ms:100,logSeq:20,logs:[{seq:20}]})',context);
 vm.runInContext('mergePageLogs({bootId:"new",ms:1000,logSeq:30,logs:[{seq:1},{seq:30}]})',context);
 assert.deepEqual(Array.from(vm.runInContext('pageLogs.map(r=>r.seq)',context)),[1,30]);
 assert.ok(html.includes('导出本地全部日志'));assert.ok(!html.includes('href="/api/state" download="esp32-log.json"'));
});

test('OV3660 full-width gain ceiling uses board-provided limits instead of OV2640 enum limits',async()=>{
 const requests=[];context.fetch=async(url,options)=>{requests.push(JSON.parse(options.body));return {ok:true};};
 const actual=Object.fromEntries(vm.runInContext('Object.entries(cameraFields).map(([key,v])=>[key,v[1]])',context));actual.gainceiling=1016;
 const state={...base,bootId:'raw-gain',cameraOk:true,cameraSettings:{available:true,revision:0,actual,ranges:{gainceiling:{min:0,max:1023,unit:'REGISTER_10_BIT'}}}};
 vm.runInContext(`syncCameraSettings(${JSON.stringify(state)})`,context);
 assert.equal(Number(element('cameraCfg_gainceiling').value),1016);assert.equal(element('cameraCfg_gainceiling').max,1023);
 await vm.runInContext('applyCameraSettings()',context);assert.equal(requests.at(-1).settings.gainceiling,1016);
 vm.runInContext(`syncCameraSettings(${JSON.stringify({...state,cameraSettings:{...state.cameraSettings,requestId:requests.at(-1).requestId,persisted:true}})})`,context);
 element('cameraCfg_gainceiling').value='1024';const before=requests.length;await vm.runInContext('applyCameraSettings()',context);assert.equal(requests.length,before);
 context.fetch=()=>new Promise(()=>{});
});

test('state, flow and preview reads share one in-flight request and release it after errors',async()=>{
 let release,started=0;
 context.testRead=async()=>{started++;await new Promise(resolve=>release=resolve);return 1;};
 context.testNext=async()=>{started++;return 2;};
 const first=vm.runInContext('withPageRead(testRead)',context),second=vm.runInContext('withPageRead(testNext)',context);
 await Promise.resolve();await Promise.resolve();assert.equal(started,1);release();assert.equal(await first,1);assert.equal(await second,2);
 context.testRead=async()=>{throw Error('closed');};await assert.rejects(vm.runInContext('withPageRead(testRead)',context));
 assert.equal(await vm.runInContext('withPageRead(testNext)',context),2);
});

test('one closed keep-alive connection is retried before clearing live readings',async()=>{
 let calls=0;context.fetch=async()=>{if(++calls===1)throw TypeError('connection reset');return {ok:true,json:async()=>({ms:1234})};};
 const d=await vm.runInContext('readPageState()',context);assert.equal(d.ms,1234);assert.equal(calls,2);
 context.fetch=()=>new Promise(()=>{});
});

test('failed state polls disable controls, release video, back off and recover on successful readback',async()=>{
 const delays=[];context.setTimeout=(fn,ms)=>delays.push(ms);
 context.fetch=async()=>{throw Error('network lost');};element('video').src='http://localhost:81/stream';
 await vm.runInContext('poll()',context);assert.equal(vm.runInContext('deviceOnline',context),false);
 assert.equal(element('video').src,'');assert.equal(element('saveUno').disabled,true);assert.equal(delays.at(-1),1200);
 await vm.runInContext('poll()',context);assert.equal(delays.at(-1),2400);
 await vm.runInContext('poll()',context);assert.equal(delays.at(-1),3000);
 const state={...base,config:{route:[]},logs:[]};context.fetch=async()=>({ok:true,json:async()=>state});
 await vm.runInContext('poll()',context);assert.equal(vm.runInContext('deviceOnline',context),true);
 assert.equal(vm.runInContext('pollFailures',context),0);assert.equal(delays.at(-1),600);
 context.setTimeout=()=>{};
});

test('incremental logs retain history, deduplicate replies, cap memory and reset after a reboot',()=>{
 vm.runInContext('pageLogs=[];pageLogAfter=0;pageLogMs=0',context);
 const merge=d=>vm.runInContext(`(()=>{const d=${JSON.stringify(d)};mergePageLogs(d);return d.logs.map(r=>r.seq);})()`,context);
 assert.deepEqual(Array.from(merge({ms:100,logSeq:2,logs:[{seq:1},{seq:2}]})),[1,2]);
 assert.deepEqual(Array.from(merge({ms:200,logSeq:3,logs:[{seq:2},{seq:3}]})),[1,2,3]);
 assert.deepEqual(Array.from(merge({ms:300,logSeq:3,logs:[]})),[1,2,3]);
 const many=Array.from({length:200},(_,i)=>({seq:i+4}));const retained=merge({ms:400,logSeq:203,logs:many});assert.equal(retained.length,160);assert.equal(retained[0],44);
 assert.deepEqual(Array.from(merge({ms:10,logSeq:1,logs:[{seq:1}]})),[1]);
});

test('CAT_CHECK frame limit is saved and read back in both cat panels, rejecting fractional counts',()=>{
 const requests=[];context.fetch=async(url,options)=>{requests.push(JSON.parse(options.body));return {ok:true};};
 const config=Object.fromEntries(vm.runInContext('[...Object.entries(robotFields),...Object.entries(catFields)].map(([k,v])=>[k,v[1]/(v[3]||1)])',context));
 Object.assign(config,{catCheckMaxFrames:3,catFarWidth:.12,catNearWidth:.35,robotFarApproachPercent:85,robotFarProbePercent:15,robotNearSignalPercent:50,robotNearObservePercent:40,robotNearStayPercent:10,catConfirmPreyPercent:20,catConfirmExplorePercent:20});
 for(const [panel,render,save] of [['robot','syncRobotSettings','saveRobotSettings'],['cat','syncCatSettings','saveCatSettings']]){
  const draw=c=>vm.runInContext(`${render}(${JSON.stringify({...base,config:c,logs:[]})})`,context);
  draw(config);const field=element(panel+'Cfg_catCheckMaxFrames');field.value='2';draw(config);assert.equal(field.value,'2');
  vm.runInContext(`${save}()`,context);assert.equal(requests.at(-1).config.catCheckMaxFrames,2);
  draw({...config,catCheckMaxFrames:2});assert.equal(Number(field.value),2);
  field.value='2.5';const before=requests.length;vm.runInContext(`${save}()`,context);assert.equal(requests.length,before);
  draw({...config,catCheckMaxFrames:1});assert.equal(Number(field.value),1);
 }
});

test('legacy cat centering is disabled and cannot issue a save',()=>{
 const requests=[];context.fetch=async(url,options)=>{requests.push(JSON.parse(options.body));return {ok:true};};
 const state={...base,config:{centerTolerancePercent:5.5,tagCenterTolerancePercent:12},logs:[]};vm.runInContext(`syncCenterTolerance(${JSON.stringify(state)})`,context);
 assert.match(element('centerStatus').textContent,/不再检测居中/);
 assert.equal(element('centerPercent').disabled,true);assert.equal(element('saveCenterTolerance').disabled,true);
 vm.runInContext('saveCenterTolerance()',context);assert.equal(requests.length,0);
 assert.equal(element('centerTarget').value,'TAG');assert.equal(element('saveTagCenterTolerance').disabled,false);
});
test('horizontal FOV conversion uses perspective projection rather than a linear angle percentage',()=>{
 const percent=vm.runInContext('centerPercentFromAngles(90,10)',context);assert.ok(Math.abs(percent-8.816349)<.00001);assert.ok(Number.isNaN(vm.runInContext('centerPercentFromAngles(90,46)',context)));
});

test('cat and tag centering save independently, with tag draft and FOV target preserved',()=>{
 const requests=[];context.fetch=async(url,options)=>{requests.push(JSON.parse(options.body));return {ok:true};};
 const d={...base,config:{centerTolerancePercent:5.5,tagCenterTolerancePercent:12},logs:[]};
 vm.runInContext(`syncCenterTolerance(${JSON.stringify(d)})`,context);
 assert.match(element('tagCenterStatus').textContent,/38.0%–62.0%/);
 element('tagCenterPercent').value='15';vm.runInContext(`syncCenterTolerance(${JSON.stringify(d)})`,context);assert.equal(element('tagCenterPercent').value,'15');
 vm.runInContext('saveTagCenterTolerance()',context);assert.deepEqual(requests.at(-1),{op:'config',config:{tagCenterTolerancePercent:15}});
 element('centerTarget').value='TAG';element('centerFov').value='90';element('centerAngle').value='10';const catDraft=element('centerPercent').value;
 vm.runInContext('convertCenterAngle()',context);assert.equal(element('tagCenterPercent').value,8.8);assert.equal(element('centerPercent').value,catDraft);
 vm.runInContext(`syncCenterTolerance(${JSON.stringify({...d,config:{...d.config,tagCenterTolerancePercent:15}})})`,context);assert.doesNotMatch(element('tagCenterStatus').textContent,/等待板上保存/);
 vm.runInContext(`syncCenterTolerance(${JSON.stringify({...d,stopPendingId:3})})`,context);assert.equal(element('saveTagCenterTolerance').disabled,true);
});

test('active cat search probabilities validate groups, preserve drafts and require board readback',()=>{
 const requests=[];context.fetch=async(url,options)=>{requests.push(JSON.parse(options.body));return {ok:true};};
 const config={catCheckMaxFrames:3,robotActionLimit:5,robotFastApproachCm:15,robotFastApproachPwm:180,robotSlowFlashPercent:50,catFarWidth:.12,catNearWidth:.35,initialLookSeconds:5,decisionIntervalMs:2500,robotFarApproachPercent:85,robotFarProbePercent:15,robotNearSignalPercent:20,robotNearObservePercent:40,robotNearStayPercent:40,robotProbeCooldownSeconds:15,slowApproachCm:9,slowApproachPwm:95,probePwm:90,probeStep1Cm:3,probeStep2Cm:4,probeStep3Cm:3,probePause1Ms:700,probePause2Ms:900,probePause3Ms:600,signalFlashMs:1400,signalBeepMs:180,minimumDrivePwm:255};
 const render=(patch={})=>vm.runInContext(`syncRobotSettings(${JSON.stringify({...base,config,logs:[],...patch})})`,context);
 render();element('robotCfg_robotFarApproachPercent').value='70';element('robotCfg_robotFarProbePercent').value='30';render();assert.equal(element('robotCfg_robotFarProbePercent').value,'30');
 vm.runInContext('saveRobotSettings()',context);assert.equal(requests.at(-1).config.robotFarProbePercent,30);assert.equal(requests.at(-1).config.robotFarApproachPercent,70);assert.match(element('robotConfigStatus').textContent,/等待板上回读/);
 render({config:{...config,robotFarApproachPercent:70,robotFarProbePercent:30}});assert.doesNotMatch(element('robotConfigStatus').textContent,/等待板上保存/);assert.match(element('robotConfigStatus').textContent,/实际至少 255/);
 const n=requests.length;element('robotCfg_robotNearSignalPercent').value='100';vm.runInContext('saveRobotSettings()',context);assert.equal(requests.length,n);
 element('robotCfg_robotNearObservePercent').value='0';element('robotCfg_robotNearStayPercent').value='0';vm.runInContext('saveRobotSettings()',context);assert.equal(requests.at(-1).config.robotNearSignalPercent,100);
 render({logs:[{seq:50,source:'CONFIG_ERROR',line:'invalid group'}]});assert.match(element('robotConfigStatus').textContent,/保存失败/);
 for(const patch of [{phase:'INTERACTION'},{stopUnconfirmed:true},{stopPendingId:7},{debugBusy:true}]){render(patch);const count=requests.length;vm.runInContext('saveRobotSettings()',context);assert.equal(requests.length,count);}
});
test('brief tag alignment remains visible as history while current state is approach',()=>{
 const d={...base,ms:2000,patrolStatus:{action:'APPROACH_TAG'},logs:[{seq:1,ms:1800,source:'DECISION',line:'PATROL TAG_ALIGN'},{seq:2,ms:1890,source:'DECISION',line:'PATROL WAIT_STOP'},{seq:3,ms:1900,source:'DECISION',line:'PATROL APPROACH_TAG'}]};
 vm.runInContext(`syncPatrolTransitions(${JSON.stringify(d)});syncPatrolStatus(${JSON.stringify(d)})`,context);assert.match(element('patrolTransitions').textContent,/历史.*对准标签（原地转向） \[TAG_ALIGN\]/s);assert.match(element('patrolProgress').textContent,/标签已找到/);
});

test('cat settings preserve drafts, validate relative widths and round trip every timeout',()=>{
  const requests=[];context.fetch=async(url,options)=>{requests.push(JSON.parse(options.body));return {ok:true};};
  const config={catCheckMaxFrames:3,escapeMinDistanceCm:30,escapeMaxDistanceCm:80,touchFlashPercent:20,catConfirmWaitSeconds:5,catConfirmPreyPercent:20,catConfirmExplorePercent:20,catFarWidth:.12,catNearWidth:.35,catInteractionIdleSeconds:12,catObserveTimeoutSeconds:10,escapePreyAfterRounds:5,escapePreyPercent:20,explorePreyMinimumSeconds:30,explorePreyPercent:20,catLeaveSeconds:5,captureWaitMaxSeconds:15};
  const render=patch=>{const d={...base,config,...patch};sync(d);vm.runInContext(`syncCatSettings(${JSON.stringify(d)})`,context);};
  render({});assert.equal(element('catCfg_catNearWidth').value,'35.0');
  element('catCfg_explorePreyMinimumSeconds').value='45';element('catCfg_captureWaitMaxSeconds').value='25';render({ms:1100});

  assert.equal(element('catCfg_explorePreyMinimumSeconds').value,'45');
  vm.runInContext('saveCatSettings()',context);assert.equal(requests.at(-1).config.explorePreyMinimumSeconds,45);assert.equal(requests.at(-1).config.captureWaitMaxSeconds,25);assert.equal(requests.at(-1).config.catNearWidth,.35);
  assert.equal(requests.at(-1).config.catConfirmWaitSeconds,undefined);assert.equal(requests.at(-1).config.touchFlashPercent,20);
  assert.equal(requests.at(-1).config.escapeMinDistanceCm,30);assert.equal(requests.at(-1).config.escapeMaxDistanceCm,80);const distanceRequests=requests.length;element('catCfg_escapeMinDistanceCm').value='90';vm.runInContext('saveCatSettings()',context);assert.equal(requests.length,distanceRequests);element('catCfg_escapeMinDistanceCm').value='30';
  const probabilityRequests=requests.length;element('catCfg_catConfirmPreyPercent').value='90';element('catCfg_catConfirmExplorePercent').value='20';vm.runInContext('saveCatSettings()',context);assert.equal(requests.length,probabilityRequests);element('catCfg_catConfirmPreyPercent').value='20';
  const n=requests.length;element('catCfg_catFarWidth').value='40';vm.runInContext('saveCatSettings()',context);assert.equal(requests.length,n);
  render({config:{...config,explorePreyMinimumSeconds:45,captureWaitMaxSeconds:25}});assert.match(element('catConfigStatus').textContent,/45/);
  for(const patch of [{phase:'INTERACTION'},{stopUnconfirmed:true},{stopPendingId:1},{debugBusy:true}]){render(patch);vm.runInContext('saveCatSettings()',context);assert.equal(requests.length,n);}
});

test('cat progress names chase and displays separate rounds and time',()=>{
 vm.runInContext(`syncCatProgress(${JSON.stringify({...base,config:{escapePreyAfterRounds:5},patrolStatus:{action:'WAIT_CHASE',interactionId:2,escapeRounds:3,exploreRounds:4,exploreElapsedMs:31000,exploreRemainingMs:0,remainingWaitMs:2000,escapeTargetCm:55}})})`,context);
 assert.match(element('catProgress').textContent,/观察猫，等待互动/);assert.match(element('catProgress').textContent,/逃跑完成 3 轮/);assert.match(element('catProgress').textContent,/31.0 秒/);assert.match(element('catProgress').textContent,/55.0 厘米/);
});

test('cat save failure is reported from board logs rather than assumed successful',()=>{
 const config={catCheckMaxFrames:3,escapeMinDistanceCm:30,escapeMaxDistanceCm:80,touchFlashPercent:20,catConfirmWaitSeconds:5,catConfirmPreyPercent:20,catConfirmExplorePercent:20,catFarWidth:.12,catNearWidth:.35,catInteractionIdleSeconds:12,catObserveTimeoutSeconds:10,escapePreyAfterRounds:5,escapePreyPercent:20,explorePreyMinimumSeconds:30,explorePreyPercent:20,catLeaveSeconds:5,captureWaitMaxSeconds:15};
 const render=logs=>{const d={...base,config,logs};sync(d);vm.runInContext(`syncCatSettings(${JSON.stringify(d)})`,context);};
 render([{seq:9,source:'CONFIG',line:'saved'}]);
 for(const [k,v] of Object.entries(config))element('catCfg_'+k).value=String(k.includes('Width')?v*100:v);
 element('catCfg_captureWaitMaxSeconds').value='25';vm.runInContext('saveCatSettings()',context);
 render([{seq:10,source:'CONFIG_ERROR',line:'confirmed stop required first'}]);
 assert.match(element('catConfigStatus').textContent,/保存失败：confirmed stop/);
});

test('scan wait preserves drafts, saves safely and extends timeout only when needed',()=>{
  const requests=[];context.fetch=async(url,options)=>{requests.push(JSON.parse(options.body));return {ok:true};};
  const render=patch=>{const d={...base,config:{scanMinimumMs:200,scanQuietMs:100,scanSettleTimeoutMs:3000},...patch};sync(d);vm.runInContext(`syncScanWait(${JSON.stringify(d)})`,context);};
  render({});assert.equal(Number(element('scanMinimumMs').value),200);
  element('scanMinimumMs').value='1000';render({ms:1100});assert.equal(element('scanMinimumMs').value,'1000');
  vm.runInContext('saveScanWait()',context);assert.deepEqual(requests.at(-1),{op:'config',config:{scanMinimumMs:1000,scanSettleTimeoutMs:3000}});
  element('scanMinimumMs').value='3000';vm.runInContext('saveScanWait()',context);assert.equal(requests.at(-1).config.scanSettleTimeoutMs,4000);
  const count=requests.length;
  for(const value of ['','49','3001','1000.5','abc']){element('scanMinimumMs').value=value;vm.runInContext('saveScanWait()',context);}assert.equal(requests.length,count);
  for(const patch of [{phase:'PATROL'},{stopPendingId:2},{stopUnconfirmed:true},{debugBusy:true},{config:{}}]){render(patch);assert.equal(element('saveScanWait').disabled,true);vm.runInContext('saveScanWait()',context);}assert.equal(requests.length,count);
  render({config:{scanMinimumMs:1000,scanQuietMs:100,scanSettleTimeoutMs:3000}});assert.equal(Number(element('scanMinimumMs').value),1000);
});

test('minimum PWM saves an integer, shows actual board readback and retains a draft while polling',()=>{
  const requests=[];context.fetch=async(url,options)=>{requests.push(JSON.parse(options.body));return {ok:true};};
  const render=d=>{sync(d);vm.runInContext(`syncMinimumDrivePwm(${JSON.stringify(d)})`,context);};
  render({...base,config:{minimumDrivePwm:140}});
  assert.equal(Number(element('minimumDrivePwm').value),140);
  assert.equal(element('saveMinimumDrivePwm').disabled,false);
  assert.match(element('minimumDrivePwmStatus').textContent,/已保存下限：140/);
  element('settings').value='{"minimumDrivePwm":140,"route":[9]}';
  element('minimumDrivePwm').value='115';
  render({...base,ms:1100,config:{minimumDrivePwm:140}});
  assert.equal(element('minimumDrivePwm').value,'115');
  vm.runInContext('saveMinimumDrivePwm()',context);
  assert.deepEqual(requests.at(-1),{op:'config',config:{minimumDrivePwm:115}});
  assert.deepEqual(JSON.parse(element('settings').value),{minimumDrivePwm:115,route:[9]});
  assert.match(element('minimumDrivePwmStatus').textContent,/已保存下限：140/); // no optimistic fake readback
  render({...base,ms:1200,config:{minimumDrivePwm:115}});
  assert.match(element('minimumDrivePwmStatus').textContent,/已保存下限：115/);
  const before=requests.length;
  for(const value of ['','0','256','140.5','abc']){element('minimumDrivePwm').value=value;vm.runInContext('saveMinimumDrivePwm()',context);}
  assert.equal(requests.length,before);
  for(const value of ['1','255']){element('minimumDrivePwm').value=value;vm.runInContext('saveMinimumDrivePwm()',context);assert.equal(requests.at(-1).config.minimumDrivePwm,Number(value));}
});

test('minimum PWM is locked until confirmed stop, while debugging, and on unsupported firmware',()=>{
  const requests=[];context.fetch=async(url,options)=>{requests.push(JSON.parse(options.body));return {ok:true};};
  for(const patch of [{phase:'PATROL'},{stopPendingId:12},{stopUnconfirmed:true},{debugBusy:true},{config:{}}]){
    vm.runInContext(`syncMinimumDrivePwm(${JSON.stringify({...base,config:{minimumDrivePwm:140},...patch})})`,context);
    assert.equal(element('minimumDrivePwm').disabled,true);
    assert.equal(element('saveMinimumDrivePwm').disabled,true);
    vm.runInContext('saveMinimumDrivePwm()',context);
  }
  assert.equal(requests.length,0);
});

test('policy display distinguishes requested PWM from the floored command',()=>{
  vm.runInContext(`syncPatrolStatus(${JSON.stringify({...base,patrolStatus:{motorId:42,leftPwm:140,rightPwm:-140,requestedLeftPwm:45,requestedRightPwm:-45,minimumDrivePwm:140}})})`,context);
  assert.match(element('patrolProgress').textContent,/#42：140\/-140/);
  assert.match(element('patrolProgress').textContent,/策略请求 45\/-45，最低 PWM 140/);
});

test('zero current command never displays historical PWM and cat failure survives patrol',()=>{
  vm.runInContext(`syncPatrolStatus(${JSON.stringify({...base,patrolStatus:{action:'SCAN_TAG',motorId:0,leftPwm:95,rightPwm:95,catFailure:{message:'找猫失败',reason:'two turns',at:500,angle:718.9}}})})`,context);
  assert.match(element('patrolProgress').textContent,/#0：0\/0/);
  assert.match(element('patrolProgress').textContent,/找猫失败.*two turns/);
});
test('parking width is read back, percent converted and locked until STOP confirmation',()=>{
  const requests=[];context.fetch=async(url,options)=>{requests.push(JSON.parse(options.body));return {ok:true};};
  vm.runInContext(`syncTagStopWidth(${JSON.stringify({...base,config:{tagStopWidth:.17}})})`,context);
  assert.match(element('tagWidthStatus').textContent,/17.0%/);
  element('tagStopPercent').value='23.5';vm.runInContext('saveTagStopWidth()',context);
  assert.deepEqual(requests.at(-1),{op:'config',config:{tagStopWidth:.235}});
  vm.runInContext(`syncTagStopWidth(${JSON.stringify({...base,stopPendingId:12,config:{tagStopWidth:.17}})})`,context);
  assert.equal(element('saveTagStopWidth').disabled,true);
});

test('Arduino panel accepts a 1 cm threshold without rounding it to the old minimum',()=>{
  const requests=[];context.fetch=async(url,options)=>{requests.push(JSON.parse(options.body));return {ok:true};};
  element('safeLeft').value='1';element('safeRight').value='1';element('unoWheel').value='67.5';element('unoTrack').value='120';
  vm.runInContext('saveUnoConfig()',context);
  assert.deepEqual(requests.at(-1),{op:'uno_config',leftCm:1,rightCm:1,wheelMm:67.5,trackMm:120});
});

test('an unconfirmed stop is visible even when the policy phase says STOPPED',()=>{
  const render=d=>vm.runInContext(`syncPatrolStatus(${JSON.stringify(d)})`,context);
  const patrolStatus={action:'FAULT',motorId:0};
  render({...base,patrolStatus,stopPendingId:123,stopUnconfirmed:false});
  assert.match(element('patrolProgress').textContent,/等待 Uno 确认停车/);
  render({...base,patrolStatus,stopPendingId:123,stopUnconfirmed:true});
  assert.match(element('patrolProgress').textContent,/停车未确认.*电机命令已阻止/);
  render({...base,patrolStatus,stopPendingId:0,stopUnconfirmed:false});
  assert.doesNotMatch(element('patrolProgress').textContent,/停车未确认|等待 Uno 确认停车/);
});

test('patrol failure remains visible even after the rolling log no longer contains it',()=>{
  const render=d=>vm.runInContext(`syncPatrolStatus(${JSON.stringify(d)})`,context);
  render({...base,logs:[],patrolStatus:{action:'FAULT',lastFault:'geometry not confirmed in current GET_CONFIG reply',motorId:0}});
  assert.match(element('patrolProgress').textContent,/geometry not confirmed/);
  render({...base,patrolStatus:{action:'SCAN_TAG',lastMotionFailure:'SIDE_LEFT_OBSTACLE_OR_SENSOR_INVALID',motorId:42,leftPwm:45,rightPwm:-45,motorConfirmed:true}});
  assert.match(element('patrolProgress').textContent,/SIDE_LEFT_OBSTACLE_OR_SENSOR_INVALID/);
  assert.match(element('patrolProgress').textContent,/#42：45\/-45，Uno 已确认/);
});

test('front avoidance switch uses saved board state and is locked while running',()=>{
  const requests=[];context.fetch=async(url,options)=>{requests.push(JSON.parse(options.body));return {ok:true};};
  const render=d=>{sync(d);vm.runInContext(`syncFrontAvoidance(${JSON.stringify(d)})`,context);};
  render({...base,config:{frontAvoidanceEnabled:true,route:[586,9,9,0]}});
  assert.equal(element('frontAvoidance').disabled,false);
  vm.runInContext('toggleFrontAvoidance()',context);
  assert.deepEqual(requests.at(-1),{op:'config',config:{frontAvoidanceEnabled:false}});
  render({...base,phase:'PATROL',config:{frontAvoidanceEnabled:false,route:[586,9,9,0]},vision:{policy:{targetIndex:0}},patrolStatus:{targetIndex:2}});
  assert.equal(element('frontAvoidance').disabled,true);
  assert.match(element('frontAvoidanceStatus').textContent,/前向拦截已关闭（含标签／猫接近与试探）/);
  assert.match(element('savedRoute').textContent,/\[586,9,9,0\].*当前目标 ID：9/);
  render(base);assert.equal(element('frontAvoidance').disabled,true);
});

test('user route preserves count, order and repeated IDs when saved',()=>{
  const requests=[];context.fetch=async(url,options)=>{requests.push(JSON.parse(options.body));return {ok:true};};
  element('route').value='[586,9,9,0]';element('settings').value='{"route":[4],"frontAvoidanceEnabled":false}';
  vm.runInContext('saveRoute()',context);
  assert.deepEqual(requests.at(-1),{op:'config',config:{route:[586,9,9,0]}});
  assert.deepEqual(JSON.parse(element('settings').value),{route:[586,9,9,0],frontAvoidanceEnabled:false});
  const count=requests.length;for(const invalid of ['[]','[587]','[1.5]']){element('route').value=invalid;vm.runInContext('saveRoute()',context);}
  assert.equal(requests.length,count);
});

test('diagnostic log decodes hexadecimal heartbeat and reset breadcrumbs',()=>{
  const describe=line=>vm.runInContext(`describeDiag(${JSON.stringify(line)})`,context);
  const heartbeat=describe('EVENT DIAG H 3e8 a 14 10 6 3 2 1');
  assert.match(heartbeat,/Uno 1000 ms/);assert.match(heartbeat,/最长 20 ms/);
  assert.match(heartbeat,/遥测 16\/6/);assert.match(heartbeat,/超声波 TX 空间/);
  assert.match(heartbeat,/RX 溢出 2，丢弃命令 1/);
  assert.match(describe('EVENT DIAG S 36 99'),/遥测右超声波，命令 99/);
  assert.match(describe('EVENT DIAG_BOOT PREV_STAGE=21 CMD=99'),/校验帧发送/);
  assert.match(describe('EVENT DIAG_BOOT STAGE=21 CMD=99'),/校验帧发送/);
  assert.equal(describe('EVENT DIAG H malformed'),'');
  assert.match(describe('EVENT DIAG M 640 620 0 0 0 0'),/SRAM 当前余量 640 B，采样最低 620 B/);
  assert.match(describe('EVENT DIAG M 610 590 2 0 -2048 1'),/电机 0\/-2048；异常锁定，需复位/);
  assert.match(describe('EVENT DIAG_BOOT PREV_STAGE=73 CMD=3154'),/蜂鸣器引脚写入/);
  assert.equal(describe('EVENT DIAG M malformed'),'');
});

test('page reports ESP active-low LED control separately from Uno buzzer',()=>{
  sync({...base,hostLed:{pin:'onboard/GPIO21',on:true,mode:'ON',gpioLevel:0}});
  assert.match(element('hostLed').textContent,/板载 LED：ON，亮/);
  assert.match(element('hostLed').textContent,/控制电平 LOW/);
  assert.match(element('hostLed').textContent,/蜂鸣器输出在 Uno D13/);
  sync({...base,mode:'SIM_UNO',hostLed:{on:false,mode:'OFF',gpioLevel:1}});
  assert.match(element('hostLed').textContent,/板载 LED：OFF，灭/);
});
test('compiled page bundles the shared telemetry parser without imports',()=>{
  assert.ok(html.includes('class HardwareTelemetry'));assert.ok(!html.includes('/* HARDWARE_TELEMETRY */'));
  assert.ok(html.includes('Real UART'));assert.ok(html.includes('Simulated UART'));
});
test('wheel controls send signed-test choices with validated duration and PWM',()=>{
  const requests=[];context.fetch=async(url,options)=>{requests.push(JSON.parse(options.body));return {ok:true};};
  element('testPwm').value='150';element('testDuration').value='300';
  for(const action of ['left_forward','left_reverse','right_forward','right_reverse']){
    assert.ok(html.includes(`manual('${action}')`));
    vm.runInContext(`manual('${action}')`,context);
    assert.deepEqual(requests.at(-1),{op:'manual_test',action,pwm:150,durationMs:300});
  }
  element('testDuration').value='2001';const count=requests.length;
  vm.runInContext("manual('left_forward')",context);assert.equal(requests.length,count);
});
test('bypass button uses board readback and visibly restores avoidance',()=>{
  const requests=[];context.fetch=async(url,options)=>{requests.push(JSON.parse(options.body));return {ok:true};};
  sync({...base,benchBypass:false});
  vm.runInContext('toggleBench()',context);assert.deepEqual(requests.at(-1),{op:'bench_safety',bypass:true});
  sync({...base,benchBypass:true});assert.equal(element('benchBypass').attrs['aria-pressed'],'true');
  assert.match(element('benchStatus').textContent,/全部避障已绕过/);
  vm.runInContext('toggleBench()',context);assert.deepEqual(requests.at(-1),{op:'bench_safety',bypass:false});
  sync({...base,mode:'SIM_UNO',benchBypass:false});assert.equal(element('benchBypass').attrs['aria-pressed'],'false');
});
test('mode selection uses board readback; physical reception is separately reported',()=>{
  sync(base);assert.equal(element('realMode').attrs['aria-pressed'],'true');assert.match(element('connection').textContent,/尚无近期 Uno/);
  sync({...base,lastUnoAgeMs:25});assert.match(element('connection').textContent,/已收到 Uno/);
  sync({...base,mode:'SIM_UNO'});assert.equal(element('simMode').attrs['aria-pressed'],'true');assert.match(element('testMode').textContent,/不会驱动真实/);
});
test('partial telemetry does not refresh old sensor rows and changing mode clears configuration',()=>{
  sync({...base,ms:4000,unoTelemetry:[{line:'DATA 1 HW SL 1 1 0 32 32 P',at:3900},{line:'DATA 1 HW ML 150 150 150 1 1 0',at:1000}],unoConfig:{line:'DATA 1 CONFIG WHEEL_MM=67.5 TRACK_MM=120.0 SLOTS_PER_REV=20 SAFE_LEFT_CM=5.0 SAFE_RIGHT_CM=6.0',at:3900,source:'UNO_RX'}});
  assert.match(element('sensor_SL').textContent,/物理测距 32 cm/);assert.match(element('sensor_ML').textContent,/过期/);assert.equal(element('safeRight').value,'6.0');
  sync({...base,ms:4100,mode:'SIM_UNO'});assert.equal(element('safeRight').value,'');assert.match(element('sensor_SL').textContent,/等待 Uno/);
});

test('exploration shows independent light and sound combination and playback state',()=>{
 vm.runInContext(`syncCatProgress(${JSON.stringify({...base,patrolStatus:{action:'SIGNAL',responseName:'双闪＋静音',responseActive:true}})})`,context);
 assert.match(element('catProgress').textContent,/探索声光 双闪＋静音（播放中）/);
 vm.runInContext(`syncCatProgress(${JSON.stringify({...base,patrolStatus:{action:'FOLLOW_WAIT',responseName:'不亮＋长响',responseActive:false}})})`,context);
 assert.match(element('catProgress').textContent,/不亮＋长响（等待新按压）/);
});

test('local ESP LED controls work without Uno link or stop confirmation',()=>{
 sync({...base,mode:'SIM_UNO',stopUnconfirmed:true});assert.equal(element('localLedOn').disabled,false);
 sync({...base,phase:'INTERACTION'});assert.equal(element('localLedOn').disabled,true);
 const source=readFileSync(new URL('uno_debug.h',root),'utf8');
 const local=source.slice(source.indexOf('if(op=="manual_test"&&(action=="led_on"'),source.indexOf('if(op=="manual_test"&&action=="stop")'));
 assert.match(local,/hostLedManual/);assert.ok(!local.includes('sendUno'));assert.ok(!source.includes('command="LED ON"'));assert.ok(!source.includes('command="LED OFF"'));
});

test('lightweight flow updates preserve transient history against a slower old telemetry reply',()=>{
 const d={ms:5000,phase:'PATROL',action:'TAG_ALIGN',motorId:7,leftPwm:-255,rightPwm:255,motorConfirmed:true,logs:[{ms:4990,source:'DECISION',line:'PATROL TAG_ALIGN'}]};
 vm.runInContext(`syncFlow(${JSON.stringify(d)})`,context);assert.match(element('patrolLive').textContent,/对准标签.*#7：-255\/255/);
 vm.runInContext('syncPatrolTransitions({ms:4900,logs:[]})',context);assert.match(element('patrolTransitions').textContent,/TAG_ALIGN/);
 vm.runInContext('syncFlow({ms:100,phase:"IDLE",action:"IDLE",motorId:0,leftPwm:0,rightPwm:0,logs:[]})',context);assert.match(element('patrolLive').textContent,/IDLE/);
});

test('invitation controls round-trip durations and widths and show fresh cat evidence',()=>{
 const requests=[];context.fetch=async(url,options)=>{requests.push(JSON.parse(options.body));return {ok:true};};
 const config=Object.fromEntries(vm.runInContext('Object.entries(robotFields).map(([k,v])=>[k,v[1]/(v[3]||1)])',context));
 Object.assign(config,{robotActionLimit:5,robotFastApproachPwm:180,slowApproachPwm:95,minimumDrivePwm:255,catFarWidth:.12,catNearWidth:.35,initialLookSeconds:8,signalFlashMs:2500,signalBeepMs:500,robotFarApproachPercent:85,robotFarProbePercent:15,robotNearSignalPercent:20,robotNearObservePercent:40,robotNearStayPercent:40,visionMaxAgeMs:1500});
 const state={...base,ms:2000,config,logs:[],patrolStatus:{robotAction:'SLOW_APPROACH',robotActionsCompleted:2,robotActionLimit:5,catDistance:0},cameraVision:{at:1950,cat:{valid:true,detected:true,width:.18}}};
 const render=d=>vm.runInContext(`syncRobotSettings(${JSON.stringify(d)})`,context);render(state);
 assert.match(element('robotLiveStatus').textContent,/2\/5.*18.0%.*远≤12.0%.*近≥35.0%/);assert.match(element('robotLiveStatus').textContent,/PWM至少 255/);
 element('robotCfg_catFarWidth').value='15';element('robotCfg_initialLookSeconds').value='9';element('robotCfg_signalFlashMs').value='2800';element('robotCfg_signalBeepMs').value='600';
 vm.runInContext('saveRobotSettings()',context);assert.equal(requests.at(-1).config.catFarWidth,.15);assert.equal(requests.at(-1).config.initialLookSeconds,9);assert.equal(requests.at(-1).config.signalFlashMs,2800);assert.equal(requests.at(-1).config.signalBeepMs,600);
 const count=requests.length;element('robotCfg_robotActionLimit').value='5.5';vm.runInContext('saveRobotSettings()',context);assert.equal(requests.length,count);
 render({...state,ms:5000});assert.match(element('robotLiveStatus').textContent,/无有效新鲜检测/);assert.doesNotMatch(element('robotLiveStatus').textContent,/18.0%/);
});

test('tag stopped attempts save integer config and wait for board readback',()=>{
 const requests=[];context.send=x=>requests.push(JSON.parse(JSON.stringify(x)));
 const d={...base,config:{tagScanMaxFrames:3},logs:[]};
 const draw=d=>vm.runInContext('syncTagScanChecks('+JSON.stringify(d)+')',context);
 draw(d);assert.equal(Number(element('tagScanMaxFrames').value),3);
 element('tagScanMaxFrames').value='5';draw(d);assert.equal(element('tagScanMaxFrames').value,'5');
 vm.runInContext('saveTagScanChecks()',context);assert.deepEqual(requests.at(-1),{op:'config',config:{tagScanMaxFrames:5}});
 draw(d);assert.match(element('tagScanStatus').textContent,/等待板上保存/);
 draw({...d,config:{tagScanMaxFrames:5}});assert.doesNotMatch(element('tagScanStatus').textContent,/等待板上保存/);
 for(const value of ['0','11','2.5','']){element('tagScanMaxFrames').value=value;vm.runInContext('saveTagScanChecks()',context);}assert.equal(requests.length,1);
 draw({...d,stopUnconfirmed:true});assert.equal(element('saveTagScanChecks').disabled,true);
 draw({...d,phase:'PATROL'});assert.equal(element('tagScanMaxFrames').disabled,true);
});

test('shared target recovery shows cat side, actual search direction and remaining budget',()=>{
 const d={phase:'INTERACTION',config:{tagLossGraceMs:1000,tagRecoveryAngleDeg:60},patrolStatus:{tagRecoveryStage:'NONE',catRecoveryStage:'LOCAL_REVERSE',catMemoryDirection:1,catRecoveryDirection:-1,catRecoveryRemainingDegrees:500}};
 vm.runInContext(`syncTagRecovery(${JSON.stringify(d)})`,context);
 assert.match(element('catRecoveryLive').textContent,/猫恢复：反向局部寻找/);
 assert.match(element('catRecoveryLive').textContent,/记忆方向：右/);
 assert.match(element('catRecoveryLive').textContent,/当前转向 左/);
 assert.match(element('catRecoveryLive').textContent,/500.0°/);
 assert.equal(element('saveTagRecovery').disabled,true);
 assert.match(element('tagRecoveryStatus').textContent,/猫与标签首次局部转向 60°/);
});
