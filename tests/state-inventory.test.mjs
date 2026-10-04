import {readFileSync} from 'node:fs';
import assert from 'node:assert/strict';
import test from 'node:test';
const root=new URL('../',import.meta.url);
const policy=readFileSync(new URL('esp32-host/ESP32CatHost/policy.cpp',root),'utf8');
const doc=readFileSync(new URL('猫互动状态机现状-20261004.md',root),'utf8').split('## 基线 ESP 0.17')[0];
test('current inventory covers literal state entrances and conditional scan state',()=>{
 const states=new Set([...policy.matchAll(/(?:action==|state\(|hold\(|awaitStop\(|waitCommand\()\s*"([A-Z_]+)"/g)].map(m=>m[1]));
 for(const state of ['SCAN_TAG_ACTIVE','CAT_DRIVE','PROBE_MOVE','ESCAPE_MOVE','ALIGN_CAT','ESCAPE_TURN'])states.add(state);
 for(const state of states)assert.match(doc,new RegExp(`\\b${state}\\b`),`missing current state ${state}`);
 assert.ok(states.size>=40);
});
test('Uno enum states are covered separately from ESP and baseline',()=>{
 const scan=readFileSync(new URL('firmware/UnoCatController/scan_machine.h',root),'utf8');
 const ino=readFileSync(new URL('firmware/UnoCatController/UnoCatController.ino',root),'utf8');
 for(const [code,name] of [[scan,'Phase'],[ino,'MotionKind'],[ino,'SonarPhase']]){
  const body=code.match(new RegExp(`enum ${name}[^\\{]*\\{([^}]*)`))[1];
  for(const state of body.match(/[A-Z][A-Z_]+/g))assert.match(doc,new RegExp(`\\b${state}\\b`));
 }
});
