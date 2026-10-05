const cameraFields={
 quality:['JPEG 质量数值（越小画质越高）',0,63],brightness:['亮度',-2,2],contrast:['对比度',-2,2],saturation:['饱和度',-2,2],
 aec:['自动曝光',0,1],ae_level:['自动曝光亮度目标',-2,2],aec2:['AEC2',0,1],aec_value:['手动曝光值（驱动刻度）',0,1200],
 agc:['自动增益',0,1],agc_gain:['手动增益',0,30],gainceiling:['自动增益上限（0–6）',0,6],
 awb:['自动白平衡',0,1],awb_gain:['白平衡增益',0,1],wb_mode:['白平衡模式（0 自动 / 1 晴 / 2 阴 / 3 办公室 / 4 家居）',0,4],
 raw_gma:['伽马校正',0,1],lenc:['镜头校正',0,1],hmirror:['水平镜像',0,1],vflip:['垂直翻转',0,1]
};
let cameraBoardState=null,cameraDraftRevision=null,cameraPending=null;
for(const [key,[label,min,max]] of Object.entries(cameraFields)){
 const row=document.createElement('label'),input=document.createElement('input');row.textContent=label;
 input.id='cameraCfg_'+key;input.type='number';input.min=min;input.max=max;input.step=1;input.disabled=true;
 row.append(input);$('cameraSettingsFields').append(row);
}
function cameraControlsReady(d){return d.cameraOk&&d.cameraSettings?.available&&['IDLE','STOPPED'].includes(d.phase)&&!d.stopPendingId&&!d.stopUnconfirmed&&!d.debugBusy&&!d.visionBench?.enabled;}
function syncCameraSettings(d){
 cameraBoardState=d;const c=d.cameraSettings,ready=!!cameraControlsReady(d),actual=c?.actual||{};
 for(const key of Object.keys(cameraFields)){
  const input=$('cameraCfg_'+key);input.disabled=!ready||!Number.isInteger(actual[key]);
  if(c&&cameraDraftRevision!==d.bootId+'/'+c.revision&&Number.isInteger(actual[key]))input.value=actual[key];
 }
 if(c)cameraDraftRevision=d.bootId+'/'+c.revision;
 // Automatic modes own these values. Their configured values remain visible.
 if(ready){$('cameraCfg_aec_value').disabled=Number($('cameraCfg_aec').value)!==0;$('cameraCfg_agc_gain').disabled=Number($('cameraCfg_agc').value)!==0;}
 $('applyCameraSettings').disabled=$('resetCameraSettings').disabled=!ready||!!cameraPending;
 $('cameraReadback').textContent=c?JSON.stringify(c,null,2):'当前固件未提供摄像头参数回读';
 let message=c?(c.error?'板上操作失败：'+c.error:`摄像头 PID ${c.sensorPid}；驱动回读版本 ${c.revision}；${c.persisted?'参数已保存到 ESP，重启后恢复':'当前驱动配置尚未确认持久化'}。`):'等待兼容固件回读';
 if(cameraPending){if(c?.requestId===cameraPending.id){message=c.error?'保存失败：'+c.error:'已应用并保存，驱动实际回读见下方。';cameraPending=null;$('applyCameraSettings').disabled=$('resetCameraSettings').disabled=!ready;}else if(Date.now()-cameraPending.at>10000){message='未收到操作结果，请查看板上回读后重试。';cameraPending=null;}else message='等待板上应用、保存与回读…';}
 $('cameraSettingsStatus').textContent=message+(ready?'':' 请先停止并等待停车确认、关闭识别性能实验。');
}
function cameraOffline(){cameraBoardState=null;for(const key of Object.keys(cameraFields))$('cameraCfg_'+key).disabled=true;$('applyCameraSettings').disabled=$('resetCameraSettings').disabled=true;$('cameraSettingsStatus').textContent='ESP32 已断线；保留上次回读，无法应用新参数。';}
async function applyCameraSettings(reset=false){
 if(!cameraBoardState||!cameraControlsReady(cameraBoardState)||cameraPending)return;
 const settings={},actual=cameraBoardState.cameraSettings.actual;
 try {
  if(!reset)for(const [key,[label,min,max]] of Object.entries(cameraFields))if(Number.isInteger(actual[key])){
   const raw=$('cameraCfg_'+key).value,value=Number(raw);if(raw===''||!Number.isInteger(value)||value<min||value>max)throw Error(label+'超出范围或不是整数');settings[key]=value;
  }
  const id='camera-'+Date.now();cameraPending={id,at:Date.now()};$('applyCameraSettings').disabled=$('resetCameraSettings').disabled=true;
  const r=await fetch('/api/input',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({op:reset?'camera_reset':'camera_config',requestId:id,settings,expectedMode:cameraBoardState.mode})});
  if(!r.ok)throw Error(await r.text());$('cameraSettingsStatus').textContent='等待板上应用、保存与回读…';
 }catch(e){cameraPending=null;$('cameraSettingsStatus').textContent='保存失败：'+e.message;}
}
