import { readFileSync } from 'node:fs';
import { gunzipSync } from 'node:zlib';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import test from 'node:test';

const root=new URL('./ESP32CatHost/',import.meta.url);
const header=readFileSync(new URL('debug_page.h',root),'utf8');
const html=gunzipSync(Buffer.from(header.split('{')[1].split('}')[0].match(/\d+/g).map(Number))).toString();
const elements=new Map();
function element(id){if(!elements.has(id))elements.set(id,{value:'',textContent:'',attrs:{},append(){},setAttribute(k,v){this.attrs[k]=v;}});return elements.get(id);}
const context=vm.createContext({document:{getElementById:element,createElement:()=>({append(){},setAttribute(){}}),querySelectorAll:()=>[]},location:{hostname:'localhost'},fetch:()=>new Promise(()=>{}),setTimeout(){},AbortSignal,TextEncoder,alert(){}});
vm.runInContext(html.match(/<script>([\s\S]*)<\/script>/)[1],context);
const base={ms:1000,mode:'UART',phase:'STOPPED',lastUnoAgeMs:999999,unoTelemetry:[],unoConfig:{},config:{}};
const sync=d=>vm.runInContext(`syncUno(${JSON.stringify(d)})`,context);

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
  assert.match(element('frontAvoidanceStatus').textContent,/无需地面校准/);
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
  assert.match(element('hostLed').textContent,/物理 LED 关闭/);
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
