import {MockUno,TOUCH_AREAS} from './core.mjs';
import {decodeHostCommand,compactReply} from './esp32-link.mjs';
import {ScanSimulator} from './scan-simulator.mjs';
import {IO_CHANNELS} from './hardware-telemetry.mjs';

export const SIM_PROTOCOL='SIM_UNO_TB_V1_SCAN_V1';
const integer=(v,min,max)=>/^[-+]?\d+$/.test(String(v))&&Number(v)>=min&&Number(v)<=max;
export class FirmwareUnoSimulator {
  constructor(world,emit){this.emit=emit;this.scan=new ScanSimulator();this.motionBase=[0,0];this.now=0;this.bootAt=0;this.lastProgress=0;this.rpm=[0,0];this.ioMask=0;this.model=new MockUno(world,line=>this.modelEvent(line));this.model.trackWidthMm=120;}
  get active(){return this.model.active;}
  get encoder(){return this.model.encoder;}
  counts(){return [Math.floor(this.encoder.left),Math.floor(this.encoder.right)];}
  pulses(){return this.counts().map((n,i)=>Math.max(0,n-this.motionBase[i]));}
  out(line){this.emit(compactReply(line));}
  ack(id){this.out(`AK ${id}`);}
  done(id,text=''){this.out(`DONE ${id}${text?' '+text:''}`);}
  error(id,text){this.out(`ERROR ${id} ${text}`);}
  boot(now){this.now=this.bootAt=this.lastProgress=now;this.scan=new ScanSimulator();this.ioMask=0;this.rpm=[0,0];this.motionBase=[0,0];this.model.reset(now);this.model.trackWidthMm=120;this.out('EVENT CAPABILITIES SCAN_V1');}
  modelEvent(line){
    let m;
    if(line.startsWith('READY ')){this.out(`READY ${SIM_PROTOCOL}`);return;}
    if((m=/^ACK (\d+)$/.exec(line))){this.ack(m[1]);return;}
    if((m=/^EVENT MOTION_STARTED (\d+)/.exec(line))){this.motionBase=this.counts();this.lastProgress=this.now;this.out(`MS ${m[1]} ${this.motionBase.join(' ')}`);return;}
    if((m=/^EVENT MOTION_(DONE|ABORTED) (\d+)(?: (\S+))?/.exec(line))){
      const id=Number(m[2]),p=this.pulses();
      this.out(m[1]==='DONE'?`MD ${id} ${p.join(' ')}`:`MA ${id} ${m[3]||'ABORTED'} ${p.join(' ')}`);
      if(this.scan.active){if(m[1]==='DONE'&&this.scan.id===id)this.scan.motionStopped(this.now,...this.counts());else this.scan.end(4);}return;
    }
    this.out(line);
  }
  telemetry(id){
    const m=this.model,bytes=new Uint8Array(50),v=new DataView(bytes.buffer),counts=this.counts();
    bytes[0]=1;v.setUint32(1,Math.max(0,Math.round(this.now-this.bootAt)),true);
    bytes[5]=Number(m.sessionStopped)|(Number(!!m.active)<<1)|(Number(m.debugEnabled)<<2)|(Number(m.dryRunMotors)<<3);v.setUint16(6,this.ioMask,true);
    for(let s=0;s<2;s++){
      const requested=m.active?Math.round((s?m.active.right:m.active.left)*255):0,enabled=(this.ioMask>>s)&1,applied=enabled&&!m.dryRunMotors?requested:0;
      v.setInt16(8+s*2,requested,true);v.setInt16(12+s*2,applied,true);
      if(enabled)bytes[16]|=(applied>0?1:applied<0?2:0)<<(s*2);
      v.setUint32(18+s*4,counts[s],true);v.setFloat32(26+s*4,this.rpm[s],true);
      const d=m.sideDistanceCm(s?'RIGHT':'LEFT');v.setInt16(34+s*4,-10,true);v.setInt16(36+s*4,d==null?-10:Math.round(d*10),true);
    }
    bytes[43]=3;bytes[46]=15;
    TOUCH_AREAS.forEach((area,s)=>{if(m.touch[area])bytes[45]|=1<<s;else bytes[44]|=1<<s;});
    const led=m.led==='ON'||m.led==='FLASH'&&Math.floor((this.now-m.ledFlashStartedAt)/m.ledInterval)%2===0;
    bytes[47]=Number(led)|(Number(this.now<m.buzzerUntil)<<1);
    const front=m.distanceCm();v.setInt16(48,front==null?-10:Math.round(front*10),true);
    this.ack(id);
    for(let part=0;part<3;part++){const hex=[...bytes.slice(part*17,part*17+17)].map(n=>n.toString(16).padStart(2,'0')).join('').toUpperCase();this.out(`TB ${id} ${part} ${hex}`);}
    this.done(id,'TELEMETRY');
  }
  receive(wire,now){
    this.now=now;let decoded;
    try{decoded=decodeHostCommand(wire);}catch{this.error(0,'BAD_CHECKSUM');return;}
    const match=/^CMD (\d+) (.+)$/.exec(decoded);if(!match||!integer(match[1],1,65535)){this.error(0,'BAD_ENVELOPE');return;}
    const id=Number(match[1]),words=match[2].split(/\s+/),verb=words[0],a=words.slice(1),n=a.map(Number),m=this.model;
    if(['GET_TELEMETRY','GET_STATUS','GET_DISTANCE','GET_ENCODER'].includes(verb)&&!a.length){this.telemetry(id);return;}
    if(verb==='PING'&&!a.length){this.ack(id);this.done(id,'PONG');this.out('EVENT CAPABILITIES SCAN_V1');return;}
    if(['SC','SCAN_CONFIG'].includes(verb)){
      const ranges=[[50,3000],[50,3000],[500,10000],[1000,600000],[100,10000]];
      if(a.length!==5||a.some((x,i)=>!integer(x,...ranges[i]))){this.error(id,'INVALID_SCAN_FRAME');return;}
      if(this.scan.active||m.active){this.error(id,'BUSY');return;}
      if(n[4]<=n[0]||n[4]<=n[1]){this.error(id,'INVALID_ARGUMENT');return;}
      this.scan.cfg={minimum:n[0],quiet:n[1],vision:n[2],total:n[3],settle:n[4]};this.ack(id);this.done(id,'SCAN_CONFIGURED');return;
    }
    if(['SR','SCAN_ROTATE'].includes(verb)){
      const ranges=[[1,2147483647],[-64,64],[1,255],[1,1000],[0,1]];
      if(a.length!==5||a.some((x,i)=>!integer(x,...ranges[i]))||n[1]===0){this.error(id,'INVALID_SCAN_FRAME');return;}
      if(m.sessionStopped||Object.values(m.touch).some(Boolean)){this.error(id,'SESSION_OR_TOUCH_LOCK');return;}
      if(this.scan.id===id&&this.scan.generation===n[0]){this.ack(id);if(this.scan.phase==='TERMINAL')this.scan.terminalSent=0;return;}
      if(m.active){this.error(id,'BUSY');return;}
      if(n[4]&&!['LEFT','RIGHT'].every(s=>m.sideDistanceCm(s)!=null&&m.sideDistanceCm(s)<m.safeSideCm[s])){this.error(id,'BOTH_SIDES_REQUIRED');return;}
      if(!this.scan.begin(id,...n,now)){this.error(id,'INVALID_SCAN');return;}this.ack(id);return;
    }
    if(['VR','VISION_RESULT'].includes(verb)){
      if(a.length!==5||!integer(a[0],1,65535)||!integer(a[1],1,1000)||!integer(a[2],1,2147483647)||!integer(a[3],0,2)||!integer(a[4],0,2147483647)){this.error(id,'INVALID_SCAN_FRAME');return;}
      if(!this.scan.reply(...n,now)){this.error(id,'STALE_VISION');return;}this.ack(id);this.done(id,'VISION_ACCEPTED');return;
    }
    if(['STOP','SESSION_STOP'].includes(verb)&&this.scan.active)this.scan.end(4);
    if(verb==='TEST_MOTOR'){
      if(a.length!==4||!integer(a[0],-255,255)||!integer(a[1],-255,255)||(!n[0]&&!n[1])||!integer(a[2],50,2000)||!['SAFE','BYPASS'].includes(a[3])){this.error(id,'INVALID_ARGUMENT');return;}
      this.bypass=a[3]==='BYPASS';const oldTouch={...m.touch},safety=m.safetyReason;
      if(this.bypass){m.touch=Object.fromEntries(TOUCH_AREAS.map(s=>[s,false]));m.safetyReason=()=>null;}
      m.startMotion(id,n[0]/255,n[1]/255,n[2],'TEST_MOTOR',now);m.touch=oldTouch;m.safetyReason=safety;return;
    }
    if(['DRIVE','ROTATE'].includes(verb)&&a.length===2&&Number.isFinite(n[0])&&n[0]!==0&&Math.abs(n[0])<=(verb==='DRIVE'?100:360)&&integer(a[1],1,255)){
      const target=Math.max(1,Math.round((verb==='DRIVE'?Math.abs(n[0])*10:Math.PI*m.trackWidthMm*Math.abs(n[0])/360)/(Math.PI*m.wheelDiameterMm)*20));
      const speed=Math.sign(n[0])*n[1]/255;m.startMotion(id,speed,verb==='DRIVE'?speed:-speed,10000,verb,now,true);
      if(m.active?.id===id){m.active.targetPulses=target;m.active.endAt=Infinity;m.active.deadline=now+10000;}return;
    }
    if(verb==='BEEP'&&a.length===1&&n[0]===0){m.buzzerUntil=0;this.ack(id);this.done(id,'BEEP_STARTED');return;}
    if(verb==='GET_IO'&&!a.length){this.ack(id);IO_CHANNELS.forEach((k,i)=>this.out(`DATA ${id} IO ${k} ${(this.ioMask>>i)&1?'ON':'OFF'}`));this.done(id);return;}
    if(verb==='DEBUG_OUTPUTS'&&a[0]==='OFF'&&a.length===1){m.stopMotion('IO_DISABLED');this.ioMask=0;m.led='OFF';m.buzzerUntil=0;this.ack(id);this.done(id,'IO_SET');return;}
    if(verb==='DEBUG_IO'&&a.length===2&&IO_CHANNELS.includes(a[0])&&['ON','OFF'].includes(a[1])){const bit=1<<IO_CHANNELS.indexOf(a[0]);this.ioMask=a[1]==='ON'?this.ioMask|bit:this.ioMask&~bit;this.ack(id);this.done(id,'IO_SET');return;}
    if(verb==='DEBUG_DIAG'&&a.length===1&&integer(a[0],0,2)){this.ack(id);this.done(id,'DIAG_SET');return;}
    // Legacy scene mechanics remain shared with the standalone test bench.
    const expanded=decoded.replace(/^(CMD \d+) DI S L /,'$1 DEBUG_INJECT SONAR LEFT ').replace(/^(CMD \d+) DI S R /,'$1 DEBUG_INJECT SONAR RIGHT ');
    m.receiveWire(expanded,now);
  }
  injectTouch(area,phase,now,source='SIM_MANUAL'){
    if(phase==='PRESS'&&!this.bypass&&this.scan.active)this.scan.end(4);
    this.model.injectTouch(area,phase,now,source);
  }
  advance(now){
    // Background tabs may receive timers only once per second. Preserve scene
    // time without turning one late browser callback into a huge physics step.
    const from=Math.max(this.model.lastStep,now-5000);
    for(let t=from+20;t<now;t+=20)this.step(t);
    this.step(now);
  }
  step(now){
    this.now=now;const m=this.model,previous=this.counts(),dt=Math.min(.05,Math.max(0,(now-m.lastStep)/1000));
    const oldSafety=m.safetyReason,oldTouch=m.updateAutomaticTouch;
    if(this.bypass&&m.active?.kind==='TEST_MOTOR'){m.safetyReason=()=>null;m.updateAutomaticTouch=()=>{};}
    else if(this.scan.active&&this.scan.flags)m.safetyReason=()=>null;
    m.step(now);m.safetyReason=oldSafety;m.updateAutomaticTouch=oldTouch;if(!m.active)this.bypass=false;
    this.rpm=this.counts().map((v,i)=>dt?(v-previous[i])/20/dt*60:0);
    if(m.active){
      const active=m.active,p=this.pulses();
      if(active.targetPulses&&p[0]>=active.targetPulses&&p[1]>=active.targetPulses){m.active=null;m.state='IDLE';this.modelEvent(`EVENT MOTION_DONE ${active.id}`);}
      else if(active.deadline&&now>=active.deadline)m.stopMotion('MOTION_TIMEOUT');
      else if(now-this.lastProgress>=100){this.lastProgress=now;this.out(`MP ${active.id} ${p.join(' ')}`);}
    }
    if(Object.values(m.touch).some(Boolean)&&this.scan.active&&!this.bypass)this.scan.end(4);
    const event=this.scan.tick(now,...this.counts());
    if(event==='START'){
      const p=Math.sign(this.scan.increment)*this.scan.pwm;this.scan.motionStarted(...this.counts());
      const safety=m.safetyReason;if(this.scan.flags)m.safetyReason=()=>null;
      m.startMotion(this.scan.id,p/255,-p/255,Infinity,'SCAN_ROTATE',now,true);m.safetyReason=safety;
      if(m.active?.id===this.scan.id){m.active.targetPulses=this.scan.stepPulses;m.active.deadline=now+10000;}else this.scan.end(2);
    }else if(event==='REQUEST')this.out(`VQ ${this.scan.id} ${this.scan.step} ${this.scan.generation} ${this.scan.used}`);
    else if(event==='COMPLETE'){if(m.active?.id===this.scan.id)m.stopMotion('SCAN_ENDED');this.out(`SD ${this.scan.id} ${this.scan.generation} ${this.scan.result}`);}
  }
}
