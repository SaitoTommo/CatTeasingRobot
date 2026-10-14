import fs from 'node:fs';
import crypto from 'node:crypto';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root=path.dirname(fileURLToPath(import.meta.url));
const inputs=process.argv.slice(2);
const stats=a=>{if(!a.length)return null;const s=[...a].sort((a,b)=>a-b),n=s.length;return {n,min:s[0],median:n%2?s[(n-1)/2]:(s[n/2-1]+s[n/2])/2,p95:s[Math.ceil(n*.95)-1],max:s[n-1]};};
function analyze(file){
  const raw=fs.readFileSync(file),d=JSON.parse(raw),groups=new Map();
  for(const e of d.logs??d.events??[]){if(!Number.isFinite(e.ms))continue;const b=e.bootId??'single';if(!groups.has(b))groups.set(b,[]);groups.get(b).push(e);}
  const transactions=[],modes=[],runs=[],bootStats=[];
  for(const [boot,rows]of groups){
    const bootFirmware=rows.find(e=>e.source==='LOCAL_BOOT')?.firmware??d.firmware??d.samples?.[0]?.firmware??null;
    const seen=new Set(),events=rows.filter(e=>{const k=e.seq!=null?`${e.seq}`:`${e.ms}/${e.source}/${e.line}`;if(seen.has(k))return false;seen.add(k);return true;}).sort((a,b)=>a.ms-b.ms||(a.seq??0)-(b.seq??0));
    const pending=new Map(),grants=new Map();let currentMode=null,run=[],lastSeq=null,gaps=0;const sources={};
    const endRun=()=>{if(run.length)runs.push({boot,firmware:bootFirmware,n:run.length,firstTxMs:run[0].txMs,lastDoneMs:run.at(-1).doneMs,txToAck:stats(run.map(t=>t.ackMs).filter(Number.isFinite)),txToDone:stats(run.map(t=>t.doneMs-t.txMs)),grantToDone:stats(run.map(t=>t.grantMs==null?null:t.doneMs-t.grantMs).filter(Number.isFinite))});run=[];};
    for(const e of events){
      sources[e.source]=(sources[e.source]??0)+1;
      if(Number.isFinite(e.seq)){if(lastSeq!=null&&e.seq>lastSeq+1){gaps+=e.seq-lastSeq-1;endRun();}lastSeq=Math.max(lastSeq??e.seq,e.seq);}
      if(e.source==='MODE'){currentMode=e.line;modes.push({boot,ms:e.ms,mode:e.line});endRun();}
      if(['CRC_ERROR','UART_BAD_FRAME','TELEMETRY_TIMEOUT','TELEMETRY_INCOMPLETE','STOP_UNCONFIRMED','READY','LOCAL_GAP'].includes(e.source))endRun();
      let m;
      if(e.source==='UART_AVAILABLE'&&(m=/^RX (\d+)$/.exec(e.line)))grants.set(m[1],e.ms);
      if(e.source==='UART_WINDOW_TX'&&(m=/^R (\d+) C (\d+) (.+)\*[0-9A-F]{2}$/.exec(e.line))){pending.set(m[2],{boot,id:Number(m[2]),command:m[3],txMs:e.ms,grantMs:grants.get(m[1])??null,parts:{},mode:currentMode});}
      if(e.source==='UNO_TX'&&(m=/^C (\d+) (.+)\*[0-9A-F]{2}$/.exec(e.line))){const t=pending.get(m[1]);if(!t||t.txMs<e.ms-2)pending.set(m[1],{boot,id:Number(m[1]),command:m[2],txMs:e.ms,grantMs:null,parts:{},mode:currentMode});}
      if(e.source!=='UNO_RX')continue;
      const line=e.line.replace(/\*[0-9A-F]{2}$/,'');
      if(!(m=/^(AK|TB|DONE|ERROR) (\d+)(?: (.*))?$/.exec(line)))continue;
      const t=pending.get(m[2]);if(!t||e.ms<t.txMs)continue;
      if(m[1]==='AK'&&t.ackMs==null)t.ackMs=e.ms-t.txMs;
      if(m[1]==='TB'){const p=/^(\d+) ([0-9A-Fa-f]+)$/.exec(m[3]??'');if(p)t.parts[p[1]]=p[2];}
      if(m[1]==='DONE'||m[1]==='ERROR'){
        t.doneMs=e.ms;t.result=line;t.complete=m[1]==='DONE'&&(t.command!=='GET_TELEMETRY'||[0,1,2].every(p=>t.parts[p]));
        if(t.complete&&t.command==='GET_TELEMETRY'){
          const b=Buffer.from([0,1,2].map(p=>t.parts[p]).join(''),'hex');
          t.stationary=b.length===50&&b[0]===1&&!(b[5]&2)&&[8,10,12,14].every(o=>b.readInt16LE(o)===0);
          t.unoUptimeMs=b.length===50?b.readUInt32LE(1):null;
          if(t.stationary)run.push(t);else endRun();
        }else if(t.command==='GET_TELEMETRY')endRun();
        transactions.push(t);pending.delete(m[2]);
      }
    }
    endRun();for(const t of pending.values())transactions.push({...t,complete:false});bootStats.push({boot,firmware:bootFirmware,eventCount:events.length,missingSequenceCount:gaps,sources});
  }
  const tele=transactions.filter(t=>t.command==='GET_TELEMETRY'),complete=tele.filter(t=>t.complete),stationary=complete.filter(t=>t.stationary);
  const out={file,sha256:crypto.createHash('sha256').update(raw).digest('hex'),firmware:d.firmware??d.lastState?.firmware??null,boots:bootStats,telemetry:{sent:tele.length,complete:complete.length,stationaryComplete:stationary.length,allCompleteTxToDone:stats(complete.map(t=>t.doneMs-t.txMs)),stationaryTxToAck:stats(stationary.map(t=>t.ackMs).filter(Number.isFinite)),stationaryTxToDone:stats(stationary.map(t=>t.doneMs-t.txMs)),stationaryGrantToDone:stats(stationary.map(t=>t.grantMs==null?null:t.doneMs-t.grantMs).filter(Number.isFinite))},largestStationaryRuns:runs.sort((a,b)=>b.n-a.n).slice(0,3),modeConfigResponses:[]};
  for(const mode of modes.filter(m=>m.mode==='UART')){const t=transactions.find(t=>t.boot===mode.boot&&t.command==='GET_CONFIG'&&t.txMs>=mode.ms&&t.txMs-mode.ms<2000);if(t){const relevant=groups.get(mode.boot).filter(e=>e.ms>=mode.ms&&e.ms<=(t.doneMs??t.txMs));out.modeConfigResponses.push({boot:mode.boot,modeMs:mode.ms,id:t.id,triggerToTxMs:t.txMs-mode.ms,triggerToAckMs:t.ackMs==null?null:t.txMs+t.ackMs-mode.ms,triggerToDoneMs:t.complete?t.doneMs-mode.ms:null,stopRetryCount:relevant.filter(e=>e.source==='STOP_RETRY').length,windowExpiredCount:relevant.filter(e=>e.source==='UNO_RX'&&e.line.includes('RX_WINDOW_EXPIRED')).length,result:t.result??null});}}
  return out;
}
const results=inputs.map(analyze);
fs.writeFileSync(path.join(root,'healthy-history-comparison.json'),JSON.stringify({analysisDate:'2026-10-14',notes:['Stationary means compact reported movement flag clear and both commanded/output PWMs zero. It is not physical stop confirmation.','Missing sequence numbers are capture gaps, not UART packet-loss measurements.','Runs end on sequence capture gaps, telemetry failure, stationary flag/PWM change, MODE, READY or captured link errors. Other traffic may remain.','MODE-to-GET_CONFIG is trigger proxy only for ordinary mode handler, not precise per-command enqueue time.'],results},null,2)+'\n');
for(const r of results)console.log(JSON.stringify({file:path.basename(r.file),firmware:r.firmware,telemetry:r.telemetry,runs:r.largestStationaryRuns,modeConfig:r.modeConfigResponses}));
