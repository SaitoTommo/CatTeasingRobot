// Read-only measurements: never request inference or send control commands.
function visionAge(d,v){return Number.isFinite(v?.at)?Math.max(0,(d.ms-v.at)>>>0):Infinity;}
function widthPercent(width){return Number.isFinite(width)&&width>=0&&width<=1?(width*100).toFixed(1)+'%':null;}
function syncVisionMeasurements(d){
 const limit=d.config?.visionMaxAgeMs||1500;
 const v=Number.isFinite(d.cameraVision?.at)?d.cameraVision:d.mode==='SIM_UNO'?d.vision:d.cameraVision;
 const simulatedVision=v===d.vision&&d.mode==='SIM_UNO';
 const catFrame=Number.isFinite(d.catPreview?.at)?d.catPreview:d.mode==='SIM_UNO'?d.vision:d.catPreview;
 const simulatedCat=catFrame===d.vision&&d.mode==='SIM_UNO';
 const cat=catFrame?.cat,catAge=visionAge(d,catFrame);
 const ageText=age=>Number.isFinite(age)?` · ${(age/1000).toFixed(1)} 秒前`:'';
 let catText='等待识别';
 if(catFrame&&Number.isFinite(catFrame.at))catText=cat?.valid!==true?'检测无效':!cat.detected?'未检测到猫':widthPercent(cat.width)||'宽度无效';
 $('catWidthLive').textContent='猫框宽度：'+catText+ageText(catAge)+(catAge>limit&&Number.isFinite(catAge)?'（已过期）':'')+(simulatedCat?'（模拟输入）':'');
 const target=d.patrolStatus?.targetId,route=d.config?.route;
 const hasTarget=Array.isArray(route)&&route.length>0&&Number.isInteger(target)&&target>=0;
 const tagAge=visionAge(d,v),tag=v?.tags?.find(t=>t.id===target);
 let tagText=!hasTarget?'未设置路线':!v||!Number.isFinite(v.at)?'等待识别':v.tagsValid!==true?'检测无效':!tag?'未检测到目标标签':widthPercent(tag.width)||'宽度无效';
 $('tagWidthLive').textContent=(hasTarget?`目标标签 ID ${target} 宽度：`:'目标标签宽度：')+tagText+(hasTarget?ageText(tagAge):'')+(hasTarget&&tagAge>limit&&Number.isFinite(tagAge)?'（已过期）':'')+(simulatedVision?'（模拟输入）':'');
 const c=d.config||{},calibrated=c.frontCalibrated===true&&Array.isArray(c.floorReference)&&c.floorReference.length===72&&c.floorReference.every(x=>Number.isFinite(x)&&x>=0&&x<=255);
 const changed=d.groundReferenceChangedAt||0;
 const current=v&&Number.isFinite(v.at)&&((v.at-changed)|0)>0&&v.frontCalibrated===true;
 let result=!calibrated?'未标定，请重新采集地面参考':!current?'等待标定后的新画面':tagAge>limit?'结果已过期':({CLEAR:'前方畅通',BLOCKED:'发现障碍',INVALID:'结果无效'}[v.front]||'结果无效');
 const difference=current&&Number.isFinite(v.floorDifference)?v.floorDifference.toFixed(1):'—';
 const threshold=Number.isFinite(c.floorDifferenceThreshold)?c.floorDifferenceThreshold.toFixed(1):'—';
 $('groundRoiDescription').textContent=Number.isFinite(c.floorRoiTop)?`当前采集区域：横向从画面宽度的 25% 到 75%；纵向从画面高度的 ${(c.floorRoiTop*100).toFixed(1)}% 到底边。这个区域必须全部是无障碍地面。`:'采集区域：等待读取板上设置。';
 $('groundCalibrationLive').textContent=`地面参考：${calibrated?'已标定':'未标定'}；前向避障：${c.frontAvoidanceEnabled===false?'关闭（判断不用于避障）':'开启'}；最新判断：${result}${current?ageText(tagAge):''}；地面差异值 ${difference} / 阈值 ${threshold}。差异值不是厘米距离。`+(simulatedVision?'（模拟输入）':'');
}
function visionMeasurementsOffline(){
 for(const [id,name] of [['catWidthLive','猫框宽度'],['tagWidthLive','目标标签宽度'],['groundCalibrationLive','地面标定状态']])$(id).textContent=name+'：连接中断，当前数据不可用';
}
let frontControlState=null,frontOperation=null,frontOperationMessage='';
function frontStopReason(d){
 if(!['IDLE','STOPPED'].includes(d.phase))return '请先停止机器人';
 if(d.stopUnconfirmed)return '尚未收到 Uno 的停车确认，请检查真实串口连接；不能采集或切换';
 if(d.stopPendingId)return '正在等待 Uno 确认停车';
 if(d.debugBusy)return '请等待手动测试或配置操作结束';
 return '';
}
function syncFrontControls(d){
 frontControlState=d;
 if(frontOperation){
  const error=(d.logs||[]).find(l=>l.seq>frontOperation.seq&&l.source==='CONFIG_ERROR');
  if(frontOperation.kind==='calibrate'&&d.frontCalibration?.status==='ERROR'&&d.frontCalibration.startedAt!==frontOperation.captureStarted){frontOperationMessage='采集失败：'+d.frontCalibration.error;frontOperation=null;}
  else if(error){frontOperationMessage='操作失败：'+(error.line==='confirmed stop required first'?'需要先收到 Uno 的停车确认':error.line==='no fresh camera signature'?'没有新的地面数据，请先开启只识别标签，再重试':error.line);frontOperation=null;}
  else if(frontOperation.kind==='toggle'&&d.config?.frontAvoidanceEnabled===frontOperation.value){frontOperationMessage='已切换：前向避障'+(frontOperation.value?'开启':'关闭');frontOperation=null;}
  else if(frontOperation.kind==='calibrate'&&d.config?.frontCalibrated===true&&d.config?.floorReference?.length===72&&d.groundReferenceChangedAt!==frontOperation.changed&&!d.frontCalibration?.pending){frontOperationMessage=d.frontCalibration?.status==='READY'?'空地参考采集完成，已用新画面确认':'空地参考采集完成；等待标定后的新画面判断';frontOperation=null;}
  else if(Date.now()-frontOperation.started>10000){frontOperationMessage='未收到操作结果，请检查连接后重试';frontOperation=null;}
 }
 const enabled=d.config?.frontAvoidanceEnabled,available=typeof enabled==='boolean',reason=frontStopReason(d);
 $('frontAvoidance').disabled=!available||!!reason||!!frontOperation||!!d.frontCalibration?.pending;
 $('frontAvoidance').setAttribute('aria-pressed',String(enabled===true));
 $('frontAvoidance').textContent=available?`前向避障：${enabled?'开启（点击关闭）':'关闭（点击开启）'}`:'当前固件不支持前向避障开关';
 $('frontAvoidanceStatus').textContent=(available?(enabled?'前向避障开启；需要有效地面校准。':'前向避障已关闭，无需地面校准。左右超声波、触碰、失速及通信停车保留。'):'等待读取开关状态')+(reason?' '+reason+'。':'');
 $('calibrateFront').disabled=!!reason||!!frontOperation||!!d.frontCalibration?.pending||!d.cameraOk;
 $('frontOperationStatus').textContent=frontOperation?(frontOperation.kind==='calibrate'?'正在请求采集空地参考，等待板子确认…':'正在切换，等待板子回读…'):reason||frontOperationMessage||'请按下方步骤准备空地参考。';
}
function startFrontOperation(kind){
 const d=frontControlState;if(!d||frontOperation)return;
 const reason=frontStopReason(d);if(reason){frontOperationMessage=reason;$('frontOperationStatus').textContent=reason;return;}
 if(kind==='calibrate'&&!d.cameraOk){$('frontOperationStatus').textContent='摄像头不可用，无法采集';return;}
 if(kind==='toggle'&&typeof d.config?.frontAvoidanceEnabled!=='boolean')return;
 frontOperation={kind,value:!d.config?.frontAvoidanceEnabled,seq:d.logSeq??d.logs?.at(-1)?.seq??0,changed:d.groundReferenceChangedAt||0,captureStarted:d.frontCalibration?.startedAt,started:Date.now()};
 const data=kind==='toggle'?{op:'config',config:{frontAvoidanceEnabled:frontOperation.value}}:{op:'calibrate_front'};
 const operation=frontOperation;syncFrontControls(d);
 let pending;try{pending=send(data);}catch(e){pending=Promise.reject(e);}
 Promise.resolve(pending).then(ok=>{if(ok===false&&frontOperation===operation){frontOperation=null;frontOperationMessage='请求发送失败，请检查连接后重试';syncFrontControls(frontControlState);}}).catch(e=>{if(frontOperation===operation){frontOperation=null;frontOperationMessage='请求发送失败：'+e.message;syncFrontControls(frontControlState);}});
}
function frontControlsOffline(){frontOperation=null;frontOperationMessage='连接中断，操作结果未确认';$('frontAvoidance').disabled=$('calibrateFront').disabled=true;$('frontOperationStatus').textContent=frontOperationMessage;}
