let benchRows=[],benchSeen=0,benchUiEpoch=-1,benchMissed=0,benchActive=false,benchFrameId='',benchDraft=false;
function benchStats(values){if(!values.length)return null;const sorted=[...values].sort((a,b)=>a-b),n=sorted.length;return {mean:values.reduce((a,b)=>a+b,0)/n,median:n%2?sorted[(n-1)/2]:(sorted[n/2-1]+sorted[n/2])/2,p95:sorted[Math.ceil(.95*n)-1],min:sorted[0],max:sorted[n-1]};}
function describeBenchStats(s){return s?`平均 ${s.mean.toFixed(1)} / 中位数 ${s.median.toFixed(1)} / P95 ${s.p95.toFixed(1)} / 最低 ${s.min} / 最高 ${s.max} ms`:'尚无样本';}
function mergeBench(d){
 if(benchUiEpoch!==d.epoch){benchRows=[];benchSeen=0;benchMissed=0;benchUiEpoch=d.epoch;benchFrameId='';}
 for(const row of d.samples){if(row[0]<=benchSeen)continue;benchMissed+=Math.max(0,row[0]-benchSeen-1);benchRows.push(row);benchSeen=row[0];}
 if(benchRows.length>20000)benchRows.splice(0,benchRows.length-20000);
 if(benchActive!==d.enabled){$('benchCanvas').style.display=d.enabled?'block':'none';}
 benchActive=d.enabled;
 if(!benchDraft){$('benchGap').value=d.gapMs;$('benchCaptureGap').value=d.captureGapMs;}
 $('benchMetrics').textContent=`${d.enabled?'测试中':'测试已停止'}；设备完成 ${d.count} 次，统计窗口 ${benchRows.length} 次；未收到 ${benchMissed} 次（设备仅缓冲最近 128 次）\n整次处理：${describeBenchStats(benchStats(benchRows.map(r=>r[4])))}\n模型推理：${describeBenchStats(benchStats(benchRows.map(r=>r[3])))}\n识别频率：${(()=>{const periods=benchRows.filter(r=>r[5]>0).map(r=>r[5]);return periods.length?(1000/(periods.reduce((a,b)=>a+b,0)/periods.length)).toFixed(2)+' 次/秒':'待测';})()}；统计最多保留最近 20000 次。P95 使用升序第 ceil(0.95×N) 项。`;
 $('benchTimes').textContent='编号 / 帧号 / 解码 / 推理 / 整次处理 / 启动间隔 / 实际空隙 / 图像到完成（ms） / 同帧含标签识别（0/1）\n'+benchRows.slice(-20).map(r=>r.join(' / ')).join('\n');
}
async function setBench(enabled,reset=false){
 const gap=Number($('benchGap').value),capture=Number($('benchCaptureGap').value);
 if(!Number.isInteger(gap)||gap<0||gap>5000||!Number.isInteger(capture)||capture<0||capture>2000){alert('识别间隔为 0–5000ms，采集间隔为 0–2000ms，均需整数');return;}
 await send({op:'vision_bench',enabled,gapMs:gap,captureGapMs:capture,reset});benchDraft=false;
}
function exportBench(){const csv='id,frame,decodeMs,modelMs,totalMs,startPeriodMs,idleMs,captureToFinishMs,withTags\n'+benchRows.map(r=>r.join(',')).join('\n');const url=URL.createObjectURL(new Blob([csv],{type:'text/csv'}));const a=document.createElement('a');a.href=url;a.download='cat-vision-bench.csv';a.click();URL.revokeObjectURL(url);}
async function benchPoll(){
 try{const r=await fetch('/api/vision-bench',{signal:AbortSignal.timeout(4000)});if(!r.ok)throw Error(r.status);mergeBench(await r.json());}
 catch(e){$('benchMetrics').textContent='帧率测试读回失败：'+e.message;}
 setTimeout(benchPoll,benchActive?250:1000);
}
async function benchFramePoll(){
 try{if(benchActive){const r=await fetch('/vision-bench-frame',{signal:AbortSignal.timeout(4000)});if(r.ok){const meta=JSON.parse(r.headers.get('X-Vision-Result')),id=meta.epoch+':'+meta.id;
 if(id!==benchFrameId&&meta.epoch===benchUiEpoch){const bitmap=await createImageBitmap(await r.blob());if(meta.epoch!==benchUiEpoch||!benchActive){bitmap.close();throw Error("结果已换轮，等待新帧");}const canvas=$('benchCanvas');canvas.width=meta.width;canvas.height=meta.height;const ctx=canvas.getContext('2d');ctx.drawImage(bitmap,0,0);bitmap.close();const cat=meta.cat||{};
 if(cat.detected&&cat.box){const [x1,y1,x2,y2]=cat.box;ctx.strokeStyle='#00ff55';ctx.lineWidth=2;ctx.strokeRect(x1,y1,x2-x1,y2-y1);ctx.fillStyle='#00ff55';ctx.font='14px sans-serif';ctx.fillText('CAT '+Number(cat.score).toFixed(2),Math.max(0,x1),Math.max(15,y1-3));}
 $('benchFrame').textContent=`识别原图帧 ${meta.frameSeq}，结果 #${meta.id}；${cat.detected?'检测到猫，框 '+cat.box.join(', '):'未检测到猫'}`;benchFrameId=id;}
 }}}
 catch(e){$('benchFrame').textContent='识别原图读取失败：'+e.message;}
 setTimeout(benchFramePoll,100);
}
benchPoll();benchFramePoll();
