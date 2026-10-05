let previewFrameId='',previewHasFrame=false,previewBoot=0,previewLastMs=0,previewMeta=null,previewNow=0;
let previewAvailable=null,previewLatestId=null,previewTimingRows=[],previewTimingSeen=0,previewTimingMissed=0;
function mergePreviewTimings(samples){
 for(const row of samples||[]){if(row[0]<=previewTimingSeen)continue;previewTimingMissed+=Math.max(0,row[0]-previewTimingSeen-1);previewTimingSeen=row[0];previewTimingRows.push(row);}
 if(previewTimingRows.length>500)previewTimingRows.splice(0,previewTimingRows.length-500);
 $('catPreviewTimes').textContent='编号 / 帧 / 解码 / 模型推理 / 总处理 / 启动间隔（ms）\n'+previewTimingRows.slice(-10).map(row=>row.join(' / ')).join('\n')+(previewTimingMissed?'\n未收到 '+previewTimingMissed+' 条耗时记录':'');
}
function describePreviewPerformance(meta){
 $('catPreviewPerformance').textContent=meta?`最近一次：解码 ${meta.decodeMs} ms · 模型 ${meta.modelMs} ms · 总处理 ${meta.latencyMs} ms；${meta.startPeriodMs?'距上次识别启动 '+meta.startPeriodMs+' ms':'首次识别'}。`:'等待猫识别…';
}
function previewPreference(key,fallback){try{return localStorage.getItem(key)??fallback;}catch{return fallback;}}
$('showPreview').checked=previewPreference('cat-preview-visible','true')==='true';
$('showCatBox').checked=previewPreference('cat-preview-box','true')==='true';
$('previewSource').value=previewPreference('cat-preview-source','processed');
function updatePreviewVisibility(){
 const visible=$('showPreview').checked,processed=$('previewSource').value==='processed'&&previewHasFrame;
 $('video').style.display=visible&&!processed?'block':'none';
 $('catPreviewCanvas').style.display=visible&&processed?'block':'none';
 const online=typeof deviceOnline==='undefined'||deviceOnline;
 const url=visible&&!processed&&online?'http://'+location.hostname+'/stream':'';
 if($('video').getAttribute('src')!==url)$('video').src=url;
 $('previewVisibility').textContent=visible?(processed?'显示：最近一次猫识别原图':'显示：实时视频（检测框显示在识别原图中）'):'预览已关闭；识别状态继续更新';
}
function previewOptionsChanged(){
 try{localStorage.setItem('cat-preview-visible',String($('showPreview').checked));localStorage.setItem('cat-preview-box',String($('showCatBox').checked));localStorage.setItem('cat-preview-source',$('previewSource').value);}catch{}
 // Redraw the same matched JPEG when changing the box visibility.
 previewFrameId='';updatePreviewVisibility();
}
function syncCameraPreview(d){
 if(d.ms<previewLastMs){++previewBoot;previewHasFrame=false;previewFrameId='';previewMeta=null;previewTimingRows=[];previewTimingSeen=previewTimingMissed=0;}
 previewAvailable=d.catPreview?.available??null;previewLatestId=d.catPreview?.id!=null?String(d.catPreview.id):null;
 previewLastMs=d.ms;previewNow=d.ms;if(d.catPreview?.id)previewMeta=d.catPreview;
 mergePreviewTimings(d.catPreview?.samples);describePreviewPerformance(previewMeta);
 const meta=previewMeta,cat=meta?.cat,age=meta?Math.max(0,d.ms-meta.at):0;
 $('catDetected').textContent=meta?`最近一次猫识别：${cat?.valid!==true?'结果无效':cat.detected?'检测到猫':'未检测到猫'}${cat?.detected&&Number.isFinite(cat.score)?' · 置信度 '+(cat.score*100).toFixed(1)+'%':''}；帧 ${meta.frameSeq}，${(age/1000).toFixed(1)} 秒前${age>(d.config?.visionMaxAgeMs||1500)?'（历史结果）':''}`:'尚无猫识别结果，等待找猫流程或开启猫识别';
 $('catDetected').className=cat?.valid&&cat.detected&&age<=(d.config?.visionMaxAgeMs||1500)?'detected':'muted';
 updatePreviewVisibility();
}
async function cameraPreviewPoll(){
 try{await (typeof withPageRead==='function'?withPageRead:async read=>read())(async()=>{
  if((typeof deviceOnline==='undefined'||deviceOnline)&&$('showPreview').checked&&$('previewSource').value==='processed'&&previewAvailable!==false&&(previewLatestId===null||previewLatestId!==previewFrameId)){
   const boot=previewBoot,r=await fetch('/cat-preview-frame?after='+encodeURIComponent(previewFrameId),{signal:AbortSignal.timeout(4000)});
   if(r.ok&&r.status!==204){
    const meta=JSON.parse(r.headers.get('X-Vision-Result')),id=String(meta.id);
    if(id!==previewFrameId){
     const bitmap=await createImageBitmap(await r.blob());
     if(boot!==previewBoot||(typeof deviceOnline!=='undefined'&&!deviceOnline)||!$('showPreview').checked||$('previewSource').value!=='processed'){bitmap.close();return;}
     const canvas=$('catPreviewCanvas');canvas.width=meta.width;canvas.height=meta.height;
     const ctx=canvas.getContext('2d');ctx.drawImage(bitmap,0,0);bitmap.close();const cat=meta.cat||{};
     if($('showCatBox').checked&&cat.valid&&cat.detected&&Array.isArray(cat.box)&&cat.box.length===4&&cat.box.every(Number.isFinite)){
      const [x1,y1,x2,y2]=cat.box;ctx.strokeStyle='#00ff55';ctx.lineWidth=2;ctx.strokeRect(x1,y1,x2-x1,y2-y1);ctx.fillStyle='#00ff55';ctx.font='14px sans-serif';ctx.fillText('CAT '+Number(cat.score).toFixed(2),Math.max(0,x1),Math.max(15,y1-3));
     }
     previewFrameId=id;previewHasFrame=true;previewMeta=meta;
     describePreviewPerformance(meta);
     $('previewFrame').textContent=`识别原图帧 ${meta.frameSeq} · ${cat.valid&&cat.detected?'检测到猫':'未检测到猫'} · 处理 ${meta.latencyMs} ms；每次新识别刷新，未检测到时清除旧框。`;
     updatePreviewVisibility();
    }
   }else if(r.status===503){$('previewFrame').textContent='尚无识别原图，暂时显示实时视频。';}
   else if(!r.ok)throw Error(r.status);
  }
 });}catch(e){$('previewFrame').textContent='识别原图读取失败：'+e.message;}
 finally{setTimeout(cameraPreviewPoll,350);}
}
updatePreviewVisibility();cameraPreviewPoll();
