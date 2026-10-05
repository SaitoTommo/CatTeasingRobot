import test from 'node:test';
import assert from 'node:assert/strict';
import {SimHttpTransport} from './sim-http-transport.mjs';
test('state and writes serialize through body completion, and failed requests release the queue',async()=>{
 const order=[];let release;
 const transport=new SimHttpTransport(async url=>{order.push(url);if(url==='state')return {ok:true,json:()=>new Promise(r=>release=r)};if(url==='bad')throw Error('reset');return {ok:true,json:async()=>({ok:true})};});
 const state=transport.request('state'),write=transport.request('uno');await Promise.resolve();assert.deepEqual(order,['state']);release({ms:1});await Promise.all([state,write]);assert.deepEqual(order,['state','uno']);
 await assert.rejects(transport.request('bad'));await transport.request('after');assert.equal(order.at(-1),'after');
});
test('queued vision is replaced by the newest frame while telemetry and commands remain intact',async()=>{
 const received=[];let release;
 const transport=new SimHttpTransport(async(url,options)=>{received.push([url,options.body]);if(url==='state')await new Promise(r=>release=r);return {ok:true,json:async()=>({ok:true})};});
 const state=transport.request('state'),old=transport.request('vision',{body:'frame1'},true),latest=transport.request('vision',{body:'frame2'},true),reply=transport.request('uno',{body:'TB'});
 assert.equal(old,latest);release();await Promise.all([state,latest,reply]);assert.deepEqual(received,[['state',undefined],['vision','frame2'],['uno','TB']]);
});
