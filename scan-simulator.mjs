// Browser counterpart of UnoCatController/scan_machine.h. No physical I/O.
export class ScanSimulator {
  constructor(){this.cfg={minimum:200,quiet:100,vision:3000,total:300000,settle:3000};this.phase='IDLE';this.id=0;this.generation=0;}
  get active(){return !['IDLE','TERMINAL'].includes(this.phase);}
  begin(id,generation,increment,pwm,budget,flags,now){
    if(!id||!generation||!increment||!pwm||!budget||budget>1000||flags>1)return false;
    if(this.id===id&&this.generation===generation)return true;
    if(this.active)return false;
    Object.assign(this,{id,generation,increment,pwm,budget,flags,used:0,usedHalf:0,step:0,lastFrame:0,began:now,phase:'NEXT',terminalSent:0,result:0});return true;
  }
  get stepPulses(){return Math.min(Math.abs(this.increment),this.budget-this.used);}
  end(result){this.result=result;this.phase='TERMINAL';this.terminalSent=0;}
  motionStarted(l,r){this.baseL=l;this.baseR=r;this.phase='TURN';}
  motionStopped(now,l,r){if(this.phase!=='TURN')return;Object.assign(this,{stopped:now,quietAt:now,quietL:l,quietR:r,phase:'SETTLE'});}
  reply(id,step,generation,result,frame,now){
    if(this.phase!=='WAIT_VISION'||id!==this.id||step!==this.step||generation!==this.generation||result>2)return false;
    if(result!==2&&(!frame||(this.lastFrame&&frame<=this.lastFrame)))return false;
    if(now-this.requested>=this.cfg.vision)return false;
    this.lastFrame=frame;if(result!==0||this.used>=this.budget)this.end(result);else this.phase='NEXT';return true;
  }
  tick(now,l,r){
    if(this.active&&now-this.began>=this.cfg.total)this.end(3);
    if(this.phase==='TERMINAL'&&this.terminalSent<5&&(!this.terminalSent||now-this.sent>=500)){this.terminalSent++;this.sent=now;return 'COMPLETE';}
    if(this.phase==='NEXT'){if(this.used>=this.budget){this.end(0);return this.tick(now,l,r);}this.step++;this.phase='TURN';return 'START';}
    if(this.phase==='SETTLE'){
      if(now<this.stopped)return;
      if(l!==this.quietL||r!==this.quietR){this.quietL=l;this.quietR=r;this.quietAt=now;}
      if(now-this.stopped>=this.cfg.settle){this.end(5);return this.tick(now,l,r);}
      if(now-this.stopped>=this.cfg.minimum&&now-this.quietAt>=this.cfg.quiet){this.usedHalf=Math.min(this.budget*2,this.usedHalf+((l-this.baseL)>>>0)+((r-this.baseR)>>>0));this.used=Math.floor(this.usedHalf/2);this.requested=this.sent=now;this.phase='WAIT_VISION';return 'REQUEST';}
    }
    if(this.phase==='WAIT_VISION'){
      if(now-this.requested>=this.cfg.vision){this.end(3);return this.tick(now,l,r);}
      if(now-this.sent>=500){this.sent=now;return 'REQUEST';}
    }
  }
}
