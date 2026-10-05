let centerReadback=null,pendingCenterSave=null,tagCenterReadback=null,pendingTagCenterSave=null;
function patrolActionName(action){const names={TAG_ALIGN:'对准标签（原地转向）',APPROACH_TAG:'标签已找到：判断对准或靠近',SCAN_TAG:'准备搜索标签',SCAN_TAG_ACTIVE:'分步旋转寻找标签',WAIT_STOP:'等待停车确认',TAG_ARRIVAL:'已到达标签',TAG_REARM:'等待重复标签重新进入',FAULT:'故障停车'};return (names[action]||action||'IDLE')+(names[action]?' ['+action+']':'');}
function syncPatrolTransitions(d){if(d.ms<flowLastMs&&flowLastMs-d.ms<10000)return;const rows=(d.logs||[]).filter(x=>x.source==='DECISION'&&x.line.startsWith('PATROL ')).slice(-8);$('patrolTransitions').textContent=rows.length?'最近巡游状态变化（历史，不代表当前正在执行）：\n'+rows.map(x=>`${Math.max(0,(d.ms-x.ms)/1000).toFixed(1)} 秒前：${patrolActionName(x.line.slice(7))}`).join('\n'):'最近巡游状态变化：暂无记录';}
function centerPercentFromAngles(fov,angle){if(!Number.isFinite(fov)||!Number.isFinite(angle)||fov<=0||fov>=180||angle<=0||angle>=fov/2)return NaN;return 50*Math.tan(angle*Math.PI/180)/Math.tan(fov*Math.PI/360);}
function syncCenterTolerance(d){
 const v=d.config?.centerTolerancePercent,available=Number.isFinite(v)&&v>=1&&v<=20,stopped=['IDLE','STOPPED'].includes(d.phase)&&!d.stopPendingId&&!d.stopUnconfirmed&&!d.debugBusy;
 for(const id of ['centerPercent','saveCenterTolerance','centerFov','centerAngle','convertCenterAngle'])$(id).disabled=!available||!stopped;
 if(available&&centerReadback!==v){$('centerPercent').value=v;centerReadback=v;}
 if(pendingCenterSave){const error=(d.logs||[]).find(x=>x.seq>pendingCenterSave.seq&&x.source==='CONFIG_ERROR');if(error)pendingCenterSave.error=error.line;else if(v===pendingCenterSave.value)pendingCenterSave=null;}
 $('centerStatus').textContent=available?`板上已保存：猫框中心左右各 ${v.toFixed(1)}%；允许中心位置 ${(50-v).toFixed(1)}%–${(50+v).toFixed(1)}%。${stopped?'可修改并保存。':'请先停止并等待停车确认。'}`:'等待读取猫居中容差';
 if(pendingCenterSave)$('centerStatus').textContent+=' '+(pendingCenterSave.error?'保存失败：'+pendingCenterSave.error:'等待板上保存回读。');
 const tag=d.config?.tagCenterTolerancePercent,tagAvailable=Number.isFinite(tag)&&tag>=1&&tag<=20;
 for(const id of ['tagCenterPercent','saveTagCenterTolerance'])$(id).disabled=!tagAvailable||!stopped;
 $('centerTarget').disabled=!available||!tagAvailable||!stopped;
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
async function flowPoll(){let delay=100;try{const r=await fetch('/api/flow',{signal:AbortSignal.timeout(2000)});if(!r.ok)throw Error(r.status);syncFlow(await r.json());}catch(e){$('patrolLive').textContent='即时流程连接中断：'+e.message;delay=1000;}setTimeout(flowPoll,delay);}
flowPoll();
