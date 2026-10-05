const robotFields={
 catCheckMaxFrames:["CAT_CHECK 最多识别新画面数（任一次找到即通过）",1,3],
 robotActionLimit:["完成行为次数（次）",1,100],robotFastApproachCm:["远处快速接近距离上限（厘米）",1,50],robotFastApproachPwm:["快速接近请求 PWM",50,255],robotSlowFlashPercent:["慢接近并行闪灯概率（%）",0,100],
 catFarWidth:["远处阈值：猫框占画面宽度（%）",1,80,100],catNearWidth:["近处阈值：猫框占画面宽度（%）",2,90,100],initialLookSeconds:["观察／静止等待时长（秒）",0,30],decisionIntervalMs:["动作间等待（毫秒）",0,10000],
 robotFarApproachPercent:['远处：快速接近再慢靠近概率（%）',0,100],robotFarProbePercent:['远处：三段试探靠近概率（%）',0,100],
 robotNearSignalPercent:['近处／接近停下后：原地声光回应概率（%）',0,100],robotNearObservePercent:['近处／接近停下后：原地观察概率（%）',0,100],robotNearStayPercent:['近处／接近停下后：静止等待概率（%）',0,100],
 robotProbeCooldownSeconds:['试探靠近冷却时间（秒）',0,60],slowApproachCm:['慢靠近计划距离（厘米，实际扣除停车余量）',1,30],slowApproachPwm:['慢靠近请求 PWM',50,255],
 probePwm:['三段试探请求 PWM',50,255],probeStep1Cm:['试探第一段距离（厘米）',1,20],probeStep2Cm:['试探第二段距离（厘米）',1,20],probeStep3Cm:['试探第三段距离（厘米）',1,20],
 probePause1Ms:['试探第一段后暂停（毫秒）',0,3000],probePause2Ms:['试探第二段后暂停（毫秒）',0,3000],probePause3Ms:['试探第三段后暂停（毫秒）',0,3000],
 signalFlashMs:['原地声光：灯光持续（毫秒）',100,5000],signalBeepMs:['原地声光：蜂鸣持续（毫秒）',0,1000]
};
let robotReadback={},pendingRobotSave=null;
for(const [key,[name,min,max]] of Object.entries(robotFields)){const label=document.createElement('label'),input=document.createElement('input');label.textContent=name;input.id='robotCfg_'+key;input.type='number';input.min=min;input.max=max;input.step='any';label.append(input);$('robotConfigFields').append(label);}
function syncRobotSettings(d){
 const c=d.config||{},ready=Object.keys(robotFields).every(k=>Number.isFinite(c[k])),stopped=['IDLE','STOPPED'].includes(d.phase)&&!d.stopPendingId&&!d.stopUnconfirmed&&!d.debugBusy;
 $('saveRobotSettings').disabled=!ready||!stopped;
 for(const key of Object.keys(robotFields)){const input=$('robotCfg_'+key);input.disabled=!ready||!stopped;if(Number.isFinite(c[key])&&robotReadback[key]!==c[key]){input.value=c[key]*(robotFields[key][3]||1);robotReadback[key]=c[key];}}
 if(pendingRobotSave){const error=(d.logs||[]).find(x=>x.seq>pendingRobotSave.seq&&x.source==='CONFIG_ERROR');if(error)pendingRobotSave.error=error.line;else if(Object.entries(pendingRobotSave.config).every(([k,v])=>c[k]===v))pendingRobotSave=null;}
 $('robotConfigStatus').textContent=ready?`板上保存：CAT_CHECK 最多 ${c.catCheckMaxFrames} 张，任一次找到即通过；远处快→慢 ${c.robotFarApproachPercent}%／试探 ${c.robotFarProbePercent}%；近处声光 ${c.robotNearSignalPercent}%／观察 ${c.robotNearObservePercent}%／静止 ${c.robotNearStayPercent}%。完成 ${c.robotActionLimit} 个完整行为后退出；慢接近闪灯 ${c.robotSlowFlashPercent}%；观察 ${c.initialLookSeconds} 秒／灯光 ${c.signalFlashMs} 毫秒／蜂鸣 ${c.signalBeepMs} 毫秒。试探冷却 ${c.robotProbeCooldownSeconds} 秒；最低PWM ${c.minimumDrivePwm??'待读取'}，慢靠近实际至少 ${Math.max(c.slowApproachPwm,c.minimumDrivePwm||1)}，试探实际至少 ${Math.max(c.probePwm,c.minimumDrivePwm||1)}。${stopped?'可修改并保存。':'请先停止并等待停车确认。'}`:'正在读取主动找猫配置。';
 const p=d.patrolStatus||{},v=d.cameraVision||{},cat=v.cat||{};const age=Number.isFinite(v.at)?d.ms-v.at:Infinity,fresh=age>=0&&age<=(c.visionMaxAgeMs||1500);const valid=fresh&&cat.valid===true,detected=valid&&cat.detected===true;
 $('robotLiveStatus').textContent=`当前动作：${p.robotAction||p.action||'—'}；完成 ${p.robotActionsCompleted??0}/${p.robotActionLimit??c.robotActionLimit??'—'}。猫框宽度：${detected&&Number.isFinite(cat.width)?(cat.width*100).toFixed(1)+'%':'无有效新鲜检测'}；远≤${(c.catFarWidth*100).toFixed(1)}%／近≥${(c.catNearWidth*100).toFixed(1)}%；控制采用 ${p.catDistance===0?'远':p.catDistance===1?'近':'未分类'}（中间宽度沿用上一档）。实际快接近PWM至少 ${Math.max(c.robotFastApproachPwm||0,c.minimumDrivePwm||1)}／慢接近至少 ${Math.max(c.slowApproachPwm||0,c.minimumDrivePwm||1)}。`;
 if(pendingRobotSave)$('robotConfigStatus').textContent+=' '+(pendingRobotSave.error?'保存失败：'+pendingRobotSave.error:'等待板上保存回读。');
}
function saveRobotSettings(){
 if($('saveRobotSettings').disabled)return;const config={};
 for(const [key,[name,min,max,scale=1]] of Object.entries(robotFields)){const input=$('robotCfg_'+key),v=Number(input.value);if(input.value===''||!Number.isFinite(v)||v<min||v>max){alert(name+' 输入无效');return;}config[key]=v/scale;}
 for(const keys of [['robotFarApproachPercent','robotFarProbePercent'],['robotNearSignalPercent','robotNearObservePercent','robotNearStayPercent']])if(Math.abs(keys.reduce((n,k)=>n+config[k],0)-100)>.01){alert('同组行为概率之和必须为100%');return;}
 if(!Number.isInteger(config.catCheckMaxFrames)){alert("CAT_CHECK 识别次数必须为整数");return;}if(!Number.isInteger(config.robotActionLimit)){alert("行为次数必须为整数");return;}if(config.catFarWidth>=config.catNearWidth){alert("远处比例必须小于近处比例");return;}
 pendingRobotSave={config,seq:pageState?.logs?.at(-1)?.seq||0};send({op:'config',config});$('robotConfigStatus').textContent='已提交，等待板上回读确认。';try{const c=JSON.parse($('settings').value);Object.assign(c,config);$('settings').value=JSON.stringify(c,null,2);}catch{}
}
