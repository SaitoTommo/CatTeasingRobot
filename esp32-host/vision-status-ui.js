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
 $('groundCalibrationLive').textContent=`地面参考：${calibrated?'已标定':'未标定'}；前向避障：${c.frontAvoidanceEnabled===false?'关闭（判断不用于避障）':'开启'}；最新判断：${result}${current?ageText(tagAge):''}；地面差异值 ${difference} / 阈值 ${threshold}。差异值不是厘米距离。`+(simulatedVision?'（模拟输入）':'');
}
function visionMeasurementsOffline(){
 for(const [id,name] of [['catWidthLive','猫框宽度'],['tagWidthLive','目标标签宽度'],['groundCalibrationLive','地面标定状态']])$(id).textContent=name+'：连接中断，当前数据不可用';
}
