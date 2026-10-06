let centerReadback=null,pendingCenterSave=null,tagCenterReadback=null,pendingTagCenterSave=null;
function patrolActionName(action){const names={TAG_ALIGN:'对准标签（原地转向）',APPROACH_TAG:'标签已找到：判断对准或靠近',SCAN_TAG:'准备搜索标签',SCAN_TAG_ACTIVE:'分步旋转寻找标签',WAIT_STOP:'等待停车确认',TAG_ARRIVAL:'已到达标签',TAG_REARM:'等待重复标签重新进入',TAG_SETTLE:'标签丢失：等待编码器停稳',TAG_RECHECK:'标签丢失：停车多帧复核',FAULT:'故障停车'};return (names[action]||action||'IDLE')+(names[action]?' ['+action+']':'');}
function syncPatrolTransitions(d){if(d.ms<flowLastMs&&flowLastMs-d.ms<10000)return;const rows=(d.logs||[]).filter(x=>x.source==='DECISION'&&x.line.startsWith('PATROL ')).slice(-8);$('patrolTransitions').textContent=rows.length?'最近巡游状态变化（历史，不代表当前正在执行）：\n'+rows.map(x=>`${Math.max(0,(d.ms-x.ms)/1000).toFixed(1)} 秒前：${patrolActionName(x.line.slice(7))}`).join('\n'):'最近巡游状态变化：暂无记录';}
function centerPercentFromAngles(fov,angle){if(!Number.isFinite(fov)||!Number.isFinite(angle)||fov<=0||fov>=180||angle<=0||angle>=fov/2)return NaN;return 50*Math.tan(angle*Math.PI/180)/Math.tan(fov*Math.PI/360);}
function syncCenterTolerance(d){
 const v=d.config?.centerTolerancePercent,available=Number.isFinite(v)&&v>=1&&v<=20,stopped=['IDLE','STOPPED'].includes(d.phase)&&!d.stopPendingId&&!d.stopUnconfirmed&&!d.debugBusy;
 for(const id of ['centerPercent','saveCenterTolerance'])$(id).disabled=true;
 for(const id of ['centerFov','centerAngle','convertCenterAngle'])$(id).disabled=!stopped;
 if(available&&centerReadback!==v){$('centerPercent').value=v;centerReadback=v;}
 if(pendingCenterSave){const error=(d.logs||[]).find(x=>x.seq>pendingCenterSave.seq&&x.source==='CONFIG_ERROR');if(error)pendingCenterSave.error=error.line;else if(v===pendingCenterSave.value)pendingCenterSave=null;}
 $('centerStatus').textContent='找猫不再检测居中；旧保存值保留，不参与控制。';
 if(pendingCenterSave)$('centerStatus').textContent+=' '+(pendingCenterSave.error?'保存失败：'+pendingCenterSave.error:'等待板上保存回读。');
 const tag=d.config?.tagCenterTolerancePercent,tagAvailable=Number.isFinite(tag)&&tag>=1&&tag<=20;
 for(const id of ['tagCenterPercent','saveTagCenterTolerance'])$(id).disabled=!tagAvailable||!stopped;
 $('centerTarget').value='TAG';$('centerTarget').disabled=!tagAvailable||!stopped;
 if(tagAvailable&&tagCenterReadback!==tag){$('tagCenterPercent').value=tag;tagCenterReadback=tag;}
 if(pendingTagCenterSave){const error=(d.logs||[]).find(x=>x.seq>pendingTagCenterSave.seq&&x.source==='CONFIG_ERROR');if(error)pendingTagCenterSave.error=error.line;else if(tag===pendingTagCenterSave.value)pendingTagCenterSave=null;}
 $('tagCenterStatus').textContent=tagAvailable?`板上已保存：标签中心左右各 ${tag.toFixed(1)}%；允许中心位置 ${(50-tag).toFixed(1)}%–${(50+tag).toFixed(1)}%。${stopped?'可修改并保存。':'请先停止并等待停车确认。'}`:'等待读取标签居中容差';
 if(pendingTagCenterSave)$('tagCenterStatus').textContent+=' '+(pendingTagCenterSave.error?'保存失败：'+pendingTagCenterSave.error:'等待板上保存回读。');
}
function saveCenterTolerance(){if($('saveCenterTolerance').disabled)return;const value=Number($('centerPercent').value);if(!$('centerPercent').value||!Number.isFinite(value)||value<1||value>20){alert('居中容差请输入 1–20%');return;}pendingCenterSave={value,seq:pageState?.logs?.at(-1)?.seq||0};send({op:'config',config:{centerTolerancePercent:value}});try{const c=JSON.parse($('settings').value);c.centerTolerancePercent=value;$('settings').value=JSON.stringify(c,null,2);}catch{}}
function saveTagCenterTolerance(){if($('saveTagCenterTolerance').disabled)return;const value=Number($('tagCenterPercent').value);if(!$('tagCenterPercent').value||!Number.isFinite(value)||value<1||value>20){alert('标签居中容差请输入 1–20%');return;}pendingTagCenterSave={value,seq:pageState?.logs?.at(-1)?.seq||0};send({op:'config',config:{tagCenterTolerancePercent:value}});try{const c=JSON.parse($('settings').value);c.tagCenterTolerancePercent=value;$('settings').value=JSON.stringify(c,null,2);}catch{}}
function convertCenterAngle(){if($('convertCenterAngle').disabled)return;const fov=Number($('centerFov').value),angle=Number($('centerAngle').value),percent=centerPercentFromAngles(fov,angle);if(!Number.isFinite(percent)||percent<1||percent>20){alert('请填写有效水平视场角和允许偏角；换算后的居中容差必须在 1–20% 内');return;}const tag=$('centerTarget').value==='TAG',id=tag?'tagCenterPercent':'centerPercent';$(id).value=Math.round(percent*10)/10;$('centerConversion').textContent=`针孔模型近似：水平视场角 ${fov}°、允许左右偏离 ${angle}° → ${tag?'标签':'猫'}容差 ±${$(id).value}%。尚未保存，请点击对应的保存居中容差。`;}
let flowLastMs=0;
function syncFlow(d){flowLastMs=d.ms;$('patrolLive').textContent=`即时流程：${d.phase} · ${patrolActionName(d.action)}；电机命令 #${d.motorId||0}：${d.leftPwm||0}/${d.rightPwm||0}，${d.motorConfirmed?'Uno 已确认':'尚未确认'}。`+(d.stopUnconfirmed?'停车未确认。':d.stopPendingId?'等待停车确认。':'');syncPatrolTransitions(d);}
async function flowPoll(){let delay=100;try{const d=await withPageRead(async()=>{const r=await fetch('/api/flow',{signal:AbortSignal.timeout(2000)});if(!r.ok)throw Error(r.status);return await r.json();});syncFlow(d);}catch(e){$('patrolLive').textContent='即时流程连接中断：'+e.message;delay=1000;}setTimeout(flowPoll,delay);}
flowPoll();

let tagScanReadback=null,pendingTagScanSave=null;
function syncTagScanChecks(d){
 const value=d.config?.tagScanMaxFrames,available=Number.isInteger(value)&&value>=1&&value<=10;
 const stopped=['IDLE','STOPPED'].includes(d.phase)&&!d.stopPendingId&&!d.stopUnconfirmed&&!d.debugBusy;
 for(const id of ['tagScanMaxFrames','saveTagScanChecks'])$(id).disabled=!available||!stopped;
 if(available&&tagScanReadback!==value){$('tagScanMaxFrames').value=value;tagScanReadback=value;}
 if(pendingTagScanSave){const error=(d.logs||[]).find(x=>x.seq>pendingTagScanSave.seq&&x.source==='CONFIG_ERROR');if(error)pendingTagScanSave.error=error.line;else if(value===pendingTagScanSave.value)pendingTagScanSave=null;}
 $('tagScanStatus').textContent=available?'板上已保存：每次停稳最多 '+value+' 次。'+(stopped?'可修改并保存。':'请先停止并等待停车确认。'):'等待板上识别次数回读';
 if(pendingTagScanSave)$('tagScanStatus').textContent+=' '+(pendingTagScanSave.error?'保存失败：'+pendingTagScanSave.error:'等待板上保存回读。');
}
function saveTagScanChecks(){
 if($('saveTagScanChecks').disabled)return;
 const value=Number($('tagScanMaxFrames').value);
 if(!$('tagScanMaxFrames').value||!Number.isInteger(value)||value<1||value>10){alert('标签识别次数请输入 1–10 的整数');return;}
 pendingTagScanSave={value,seq:pageState?.logs?.at(-1)?.seq||0};send({op:'config',config:{tagScanMaxFrames:value}});
 try{const c=JSON.parse($('settings').value);c.tagScanMaxFrames=value;$('settings').value=JSON.stringify(c,null,2);}catch{}
}

let tagRecoveryReadback=null,pendingTagRecoverySave=null;
function syncTagRecovery(d){
 const c=d.config||{},grace=c.tagLossGraceMs,angle=c.tagRecoveryAngleDeg;
 const available=Number.isInteger(grace)&&grace>=0&&grace<=3000&&Number.isFinite(angle)&&angle>=20&&angle<=120;
 const stopped=['IDLE','STOPPED'].includes(d.phase)&&!d.stopPendingId&&!d.stopUnconfirmed&&!d.debugBusy;
 for(const id of ['tagLossGraceMs','tagRecoveryAngleDeg','saveTagRecovery'])$(id).disabled=!available||!stopped;
 const value=JSON.stringify([grace,angle]);
 if(available&&tagRecoveryReadback!==value){$('tagLossGraceMs').value=grace;$('tagRecoveryAngleDeg').value=angle;tagRecoveryReadback=value;}
 if(pendingTagRecoverySave){const error=(d.logs||[]).find(x=>x.seq>pendingTagRecoverySave.seq&&x.source==='CONFIG_ERROR');if(error)pendingTagRecoverySave.error=error.line;else if(value===pendingTagRecoverySave.value)pendingTagRecoverySave=null;}
 $('tagRecoveryStatus').textContent=available?`板上已保存：漏检宽限 ${grace} 毫秒，猫与标签首次局部转向 ${angle}°。${stopped?'可修改并保存。':'请先停止并等待停车确认。'}`:'等待板上目标恢复参数回读';
 if(pendingTagRecoverySave)$('tagRecoveryStatus').textContent+=' '+(pendingTagRecoverySave.error?'保存失败：'+pendingTagRecoverySave.error:'等待板上保存回读。');
 const p=d.patrolStatus||{},names={GRACE:'短暂漏检，保持直行',SETTLE:'等待停稳',RECHECK:'停车复核',LOCAL_FIRST:'按记忆方向局部寻找',LOCAL_REVERSE:'反向局部寻找',FULL:'使用剩余预算寻找',NONE:'无恢复动作'};
 const catDirection=p.catMemoryDirection<0?'左':p.catMemoryDirection>0?'右':'无有效方向';
 $('catRecoveryLive').textContent=`猫恢复：${names[p.catRecoveryStage]||'等待读取'}；记忆方向：${catDirection}`+(['LOCAL_FIRST','LOCAL_REVERSE','FULL'].includes(p.catRecoveryStage)?`；当前转向 ${p.catRecoveryDirection<0?'左':'右'}；预算剩余 ${Number(p.catRecoveryRemainingDegrees||0).toFixed(1)}°`:'');
 const direction=p.tagMemoryDirection<0?'左':p.tagMemoryDirection>0?'右':'无有效方向';
 $('tagRecoveryLive').textContent=`标签恢复：${names[p.tagRecoveryStage]||'等待读取'}；记忆方向：${direction}`+(p.tagRecoveryStage==='GRACE'?`；宽限剩余 ${p.tagLossRemainingMs||0} 毫秒`:'')+(['LOCAL_FIRST','LOCAL_REVERSE','FULL'].includes(p.tagRecoveryStage)?`；当前转向 ${p.tagRecoveryDirection<0?'左':'右'}；总预算剩余 ${Number(p.tagRecoveryRemainingDegrees||0).toFixed(1)}°`:'');
}
function saveTagRecovery(){
 if($('saveTagRecovery').disabled)return;
 const grace=Number($('tagLossGraceMs').value),angle=Number($('tagRecoveryAngleDeg').value);
 if($('tagLossGraceMs').value===''||!Number.isInteger(grace)||grace<0||grace>3000||$('tagRecoveryAngleDeg').value===''||!Number.isFinite(angle)||angle<20||angle>120){alert('漏检宽限请输入 0–3000 毫秒整数，局部转向请输入 20–120 度');return;}
 const config={tagLossGraceMs:grace,tagRecoveryAngleDeg:angle};pendingTagRecoverySave={value:JSON.stringify([grace,angle]),seq:pageState?.logs?.at(-1)?.seq||0};send({op:'config',config});
 try{const c=JSON.parse($('settings').value);Object.assign(c,config);$('settings').value=JSON.stringify(c,null,2);}catch{}
}
