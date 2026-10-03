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

test('diagnostic log decodes hexadecimal heartbeat and reset breadcrumbs',()=>{
  const describe=line=>vm.runInContext(`describeDiag(${JSON.stringify(line)})`,context);
  const heartbeat=describe('EVENT DIAG H 3e8 a 14 10 6 3 2 1');
  assert.match(heartbeat,/Uno 1000 ms/);assert.match(heartbeat,/最长 20 ms/);
  assert.match(heartbeat,/遥测 16\/6/);assert.match(heartbeat,/超声波 TX 空间/);
  assert.match(heartbeat,/RX 溢出 2，丢弃命令 1/);
  assert.match(describe('EVENT DIAG S 36 99'),/遥测右超声波，命令 99/);
  assert.match(describe('EVENT DIAG_BOOT PREV_STAGE=21 CMD=99'),/校验帧发送/);
  assert.equal(describe('EVENT DIAG H malformed'),'');
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
