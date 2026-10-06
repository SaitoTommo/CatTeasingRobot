import fs from 'node:fs';
const input = process.argv[2];
const data = JSON.parse(fs.readFileSync(input, 'utf8'));
const bootId = data.lastState.bootId;
const logs = data.logs.filter(x => x.bootId === bootId && Number.isInteger(x.seq));
const scans = new Map(), steps = [];
const anglePerSumPulse = (data.lastState.config.wheelMm / data.lastState.config.trackMm) * 180 / 20;
for (const row of logs) {
  let m;
  if (row.source === 'UNO_TX' && (m = row.line.match(/^C (\d+) SR (\d+) (-?\d+) (\d+) (\d+) /))) {
    scans.set(+m[1], {id:+m[1], increment:+m[3], pwm:+m[4], previousHalf:0, pending:null, last:null});
  }
  if (row.source !== 'UNO_RX') continue;
  if ((m = row.line.match(/^MS (\d+) (\d+) (\d+)$/)) && scans.has(+m[1])) {
    const scan = scans.get(+m[1]);
    if (scan.last) {
      scan.last.nextStartMs = row.ms;
      scan.last.betweenDoneAndNextLeft = +m[2] - scan.last.baseLeft - scan.last.doneLeft;
      scan.last.betweenDoneAndNextRight = +m[3] - scan.last.baseRight - scan.last.doneRight;
    }
    scan.pending = {scanId:scan.id,pwm:scan.pwm,startSeq:row.seq,startMs:row.ms,baseLeft:+m[2],baseRight:+m[3]};
  }
  if ((m = row.line.match(/^MD (\d+) (\d+) (\d+)$/)) && scans.has(+m[1])) {
    const step = scans.get(+m[1]).pending;
    if (step) Object.assign(step,{doneSeq:row.seq,doneMs:row.ms,doneLeft:+m[2],doneRight:+m[3],durationMs:row.ms-step.startMs});
  }
  if ((m = row.line.match(/^VQ (\d+) (\d+) (\d+) (\d+) (\d+)$/)) && scans.has(+m[1])) {
    const scan = scans.get(+m[1]), step = scan.pending;
    if (!step || step.doneMs === undefined) continue;
    const sumDelta = +m[5] - scan.previousHalf;
    Object.assign(step,{step:+m[2],requestSeq:row.seq,requestMs:row.ms,sumDelta,extraAfterDoneBeforeRequest:sumDelta-step.doneLeft-step.doneRight,estimatedDegrees:sumDelta*anglePerSumPulse,settleMs:row.ms-step.doneMs});
    scan.previousHalf=+m[5];scan.last=step;scan.pending=null;steps.push(step);
  }
}
const summary = {input,bootId,deviceRows:logs.length,anglePerSumPulse,completedSampleSteps:steps.length,
  pwmValues:[...new Set(steps.map(s=>s.pwm))],
  durationMs:{min:Math.min(...steps.map(s=>s.durationMs)),max:Math.max(...steps.map(s=>s.durationMs))},
  anomalies:steps.filter(s=>s.extraAfterDoneBeforeRequest>=10 || Math.max(s.betweenDoneAndNextLeft||0,s.betweenDoneAndNextRight||0)>=10),steps};
console.log(JSON.stringify(summary,null,2));
