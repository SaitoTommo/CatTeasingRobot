const remoteClient='web-'+Date.now().toString(36)+'-'+Math.random().toString(36).slice(2);
let remoteState=null,remoteSeq=0,remoteDirection='',remotePointer=null,beepPointer=null,remoteBeep=false,remoteBusy=false,routeChoice=null,routeKey='',routeNodes=[],remoteTimer=null;
function remoteOwned(){return remoteState?.active&&remoteState.client===remoteClient;}
function remoteSend(action,extra={}){
 if(!remoteOwned())return Promise.resolve(false);
 return send({op:'remote',action,client:remoteClient,session:remoteState.session,seq:++remoteSeq,expectedMode:pageState.mode,...extra});
}
async function enterRemote(){
 remoteRelease();$('remoteStatus').textContent='请求接管，等待停车、协议检查与会话确认…';
 try{await send({op:'remote',action:'enter',client:remoteClient,expectedMode:pageState.mode});}catch(e){remoteOffline();}
}
function remotePress(e,direction){
 if(!remoteOwned()||remotePointer!==null)return;e.preventDefault?.();remotePointer=e.pointerId;remoteDirection=direction;e.currentTarget?.setPointerCapture?.(e.pointerId);remoteBeat();
}
function beepPress(e){
 if(!remoteOwned()||beepPointer!==null)return;e.preventDefault?.();beepPointer=e.pointerId;remoteBeep=true;e.currentTarget?.setPointerCapture?.(e.pointerId);remoteSend('beep_hold',{held:true}).catch(remoteOffline);
}
function remoteRelease(e){
 if(!e||remotePointer===e.pointerId){const moving=remoteDirection!=='';remoteDirection='';remotePointer=null;if(moving)remoteSend('move_stop').catch(remoteOffline);}
 if(!e||beepPointer===e.pointerId){const ringing=remoteBeep;remoteBeep=false;beepPointer=null;if(ringing)remoteSend('beep_hold',{held:false}).catch(remoteOffline);}
}
async function remoteBeat(){
 if(remoteBusy||!remoteOwned())return;remoteBusy=true;
 try{
  await remoteSend('heartbeat');
  if(remoteDirection&&remoteOwned())await remoteSend('move',{direction:remoteDirection,pwm:Number($('remoteSpeed').value)});
  if(remoteBeep&&remoteOwned())await remoteSend('beep_hold',{held:true});
 }catch(e){remoteOffline();}finally{remoteBusy=false;}
}
function remoteLoop(){remoteBeat();remoteTimer=setTimeout(remoteLoop,150);}
function remoteOffline(){
 remoteDirection='';remotePointer=null;remoteBeep=false;beepPointer=null;remoteState=null;
 document.querySelectorAll('.remote-action').forEach(b=>b.disabled=true);$('enterRemote').disabled=true;
 document.querySelectorAll('.patrol-action').forEach(b=>b.disabled=true);
 for(const id of ['remoteStatus','patrolControlStatus'])$(id).textContent='连接中断，控制不可用；机器人将在续期超时后停车。';
 for(const name of ['patrol','remote'])$(name+'SaveRoute').disabled=true;
 for(const id of ['tagDetectionStatus','avoidanceRuntime'])$(id).textContent='连接中断，当前数据不可用';
}
function routeSelect(index){routeChoice=Number(index);for(const name of ['patrol','remote'])$(name+'Target').value=String(routeChoice);syncResumeLabels();}
function routeFilter(text){
 for(const name of ['patrol','remote'])$(name+'TagInput').value=text;
 drawRouteOptions(text);
}
function drawRouteOptions(filter=''){
 const matches=routeNodes.map((id,index)=>({id,index})).filter(x=>!filter||String(x.id).includes(filter.trim()));
 if(!matches.some(x=>x.index===routeChoice))routeChoice=matches[0]?.index??null;
 for(const name of ['patrol','remote']){
  const select=$(name+'Target');select.replaceChildren?.();select.innerHTML='';
  for(const {id,index} of matches){const option=document.createElement('option');option.value=String(index);option.textContent=`第 ${index+1} 个节点 · 标签 ${id}`;select.append(option);}
  select.value=routeChoice===null?'':String(routeChoice);
 }
 syncResumeLabels();
}
function syncResumeLabels(){
 const selected=Number.isInteger(routeChoice)&&routeChoice>=0&&routeChoice<routeNodes.length;
 const available=deviceOnline&&!pageState?.stopPendingId&&!pageState?.stopUnconfirmed&&(!remoteState?.busy||remoteState?.active);
 for(const name of ['patrol','remote']){const b=$(name+'Resume');b.disabled=!selected||!available;b.textContent=selected?`续寻：寻找标签 ${routeNodes[routeChoice]}（节点 ${routeChoice+1}）`:'续寻：请选择路线节点';}
}
async function resumeRoute(){
 if(!Number.isInteger(routeChoice)||routeChoice<0||routeChoice>=routeNodes.length)return;
 remoteRelease();$('patrolControlStatus').textContent='请求续寻；等待停车确认后寻找所选标签…';
 try{await send({op:'control',action:'resume_from',index:routeChoice,route:routeNodes.slice(),expectedMode:pageState.mode});}catch(e){remoteOffline();}
}
function syncRemote(d){
 const previous=remoteState;remoteState=d.remote||null;
 if(previous?.session!==remoteState?.session||!remoteState?.busy){remoteDirection='';remotePointer=null;remoteBeep=false;beepPointer=null;}
 const owned=remoteOwned();$('enterRemote').disabled=!deviceOnline||remoteState?.busy||d.stopPendingId||d.stopUnconfirmed||d.debugBusy;
 document.querySelectorAll('.remote-action').forEach(b=>b.disabled=!owned);
 const available=deviceOnline&&!d.stopPendingId&&!d.stopUnconfirmed&&(!remoteState?.busy||remoteState?.active);
 document.querySelectorAll('.patrol-action').forEach(b=>b.disabled=!available);
 $('remoteSpeedValue').textContent=$('remoteSpeed').value+' PWM';
 const lights=['不亮','常亮','慢闪','双闪'],sounds=['静音','长响','两声短响','三声短响'];
 $('remoteStatus').textContent=(owned?'遥控中：障碍与触碰保护已绕过；松手停车。':remoteState?.active?'其他页面正在遥控。':remoteState?.busy?'正在等待停车或会话确认…':'遥控未开启。')+(remoteState?.error?' '+remoteState.error:'')+(remoteState?.capable===false?' 配套 Uno 需支持 REMOTE_V1（V6.13）。':'');
 $('remoteOutputs').textContent=`请求 PWM ${remoteState?.leftPwm||0}/${remoteState?.rightPwm||0}；${remoteState?.motorConfirmed?'Uno 已确认':'等待动作确认/静止'}。灯光：${lights[remoteState?.lastLight]||'未选择'}；声音：${remoteState?.beepHeld?'按住鸣叫':sounds[remoteState?.sound||0]}。实际电机输出见传感器回报。`;
 $('patrolControlStatus').textContent=`${d.phase==='REMOTE'?'遥控接管':d.phase==='PATROL'?'正在巡游':d.phase==='STARTING'?'正在启动巡游':'当前：'+d.phase}；目标节点 ${(d.patrolStatus?.targetIndex??0)+1}，标签 ${d.patrolStatus?.targetId??'未设置'}。`+(remoteState?.error?' '+remoteState.error:'');
 $('avoidanceRuntime').textContent=d.phase==='REMOTE'?'遥控接管：前向、左右及触碰保护绕过；以下仍显示原始观测。':'自主运动按既有避障规则执行。';
 $('firmwareVersion').textContent='板上固件：'+d.firmware;
 const route=d.config?.route||[],key=JSON.stringify(route);
 if(key!==routeKey){routeKey=key;routeNodes=route.slice();routeChoice=Math.min(d.patrolStatus?.targetIndex||0,route.length-1);for(const name of ['patrol','remote'])$(name+'TagInput').value='';drawRouteOptions();try{const settings=JSON.parse($('settings').value);settings.route=route.slice();$('settings').value=JSON.stringify(settings,null,2);}catch{}}
 syncResumeLabels();syncRouteEditor(d);
 const v=Number.isFinite(d.cameraVision?.at)?d.cameraVision:d.vision;
 const age=visionAge(d,v),limit=d.config?.visionMaxAgeMs||1500;
 $('tagDetectionStatus').textContent=!v||!Number.isFinite(v.at)?'未开启识别或等待首帧':age>limit?'结果已过期，不能确认当前标签':v.tagsValid!==true?'标签观测无效/本帧未识别标签':v.tags?.length?'检测到标签：'+v.tags.map(t=>t.id).join('、'):'有效识别：未检测到标签';
 $('tagDetectionStatus').textContent+=Number.isFinite(age)?`；${age} ms 前 · ${v.source||'来源未知'}`:'';
 const match=/SAFE_LEFT_CM=([\d.]+) SAFE_RIGHT_CM=([\d.]+)/.exec(d.unoConfig?.line||'');
 $('sideAvoidanceThresholds').textContent=match?`Uno 回读阈值：左 ${match[1]} cm / 右 ${match[2]} cm。测距过期或无效不表示畅通。`:'侧向阈值等待读取 Uno 配置。';
 $('saveAvoidanceUno').disabled=$('saveUno').disabled;
}
document.addEventListener?.('pointerup',remoteRelease);
document.addEventListener?.('pointercancel',remoteRelease);
document.addEventListener?.('visibilitychange',()=>{if(document.hidden){remoteRelease();remoteSend('exit').catch(remoteOffline);remoteOffline();}});
if(typeof window!=='undefined'){window.addEventListener('blur',()=>{remoteRelease();});window.addEventListener('pagehide',()=>{remoteRelease();remoteSend('exit').catch(remoteOffline);remoteOffline();});}
remoteLoop();

let routeDraftDirty=false,routeSavePending=null,routeSaveMessage='';
function parseCustomRoute(text){
 const trimmed=text.trim();let route;
 if(trimmed.startsWith('['))route=JSON.parse(trimmed);
 else {const tokens=trimmed.split(/[,，\s→>]+/).filter(Boolean);if(tokens.some(t=>!/^\d+$/.test(t)))throw Error('标签 ID 必须是 0–586 的整数');route=tokens.map(Number);}
 if(!Array.isArray(route)||!route.length||route.length>128||route.some(n=>!Number.isInteger(n)||n<0||n>586))throw Error('请输入 1–128 个节点，标签 ID 为 0–586 的整数');
 return route;
}
function editRouteDraft(text){routeDraftDirty=true;routeSaveMessage='路线草稿未保存；续巡仍使用板上已保存路线。';for(const name of ['patrol','remote'])$(name+'RouteDraft').value=text;}
async function saveCustomRoute(){
 try{
  if(!deviceOnline||!['IDLE','STOPPED'].includes(pageState?.phase)||pageState.stopPendingId||pageState.stopUnconfirmed||remoteState?.busy)throw Error('请先停止巡游或退出遥控，等待停车确认后保存路线');
  const route=parseCustomRoute($('patrolRouteDraft').value);
  const requestId=remoteClient+'-route-'+Date.now()+'-'+Math.random().toString(36).slice(2);
  routeSavePending={requestId,route,bootId:pageState.bootId,at:Date.now()};routeSaveMessage='正在保存，等待 ESP 写入确认…';syncRouteEditor(pageState);
  const accepted=await send({op:'route_save',requestId,route,expectedMode:pageState.mode});
  if(!accepted)throw Error('保存请求未被接受，请重试');
 }catch(e){routeSavePending=null;routeSaveMessage=e.message;syncRouteEditor(pageState);}
}
function syncRouteEditor(d){
 if(!d)return;
 if(routeSavePending){
  const result=d.routeSave;
  if(d.bootId!==routeSavePending.bootId){routeSaveMessage='设备已重启，请核对板上路线后重新保存';routeSavePending=null;}
  else if(result?.requestId===routeSavePending.requestId){
   if(result.status==='saved'&&JSON.stringify(d.config?.route)===JSON.stringify(routeSavePending.route)){routeDraftDirty=false;routeSaveMessage='巡游路线已保存到 ESP；可选择任意节点开始续巡。';}
   else routeSaveMessage=result.error||'路线回读不一致，请核对后重试';
   routeSavePending=null;
  }else if(Date.now()-routeSavePending.at>8000){routeSavePending=null;routeSaveMessage='未收到保存确认，请核对板上路线后重试';}
 }
 const route=d.config?.route||[];
 if(!routeDraftDirty&&!routeSavePending)for(const name of ['patrol','remote'])$(name+'RouteDraft').value=route.join(', ');
 const stopped=deviceOnline&&['IDLE','STOPPED'].includes(d.phase)&&!d.stopPendingId&&!d.stopUnconfirmed&&!d.debugBusy&&!remoteState?.busy;
 for(const name of ['patrol','remote']){
  $(name+'SaveRoute').disabled=!stopped||!!routeSavePending;
  $(name+'RouteDraft').disabled=!!routeSavePending;
  $(name+'RouteSaveStatus').textContent=(routeSaveMessage||'可编辑路线，保存后再续巡。')+' 板上路线：'+(route.length?route.join(' → '):'尚未设置')+(stopped?'':'；请先停车再保存。');
 }
}
