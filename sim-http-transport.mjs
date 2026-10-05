// One request at a time, including response bodies. Only queued visual
// observations may be replaced: commands and telemetry are always preserved.
export class SimHttpTransport {
  constructor(fetcher=(...args)=>fetch(...args)){this.fetcher=fetcher;this.queue=[];this.active=false;this.vision=null;}
  request(url,options={},replaceVision=false){
    if(replaceVision&&this.vision){this.vision.options=options;return this.vision.promise;}
    const job={url,options};job.promise=new Promise((resolve,reject)=>Object.assign(job,{resolve,reject}));
    if(replaceVision)this.vision=job;
    this.queue.push(job);this.drain();return job.promise;
  }
  async drain(){
    if(this.active||!this.queue.length)return;
    this.active=true;const job=this.queue.shift();if(this.vision===job)this.vision=null;
    try{
      const r=await this.fetcher(job.url,{...job.options,signal:AbortSignal.timeout(4500)}),data=await r.json();
      if(!r.ok)throw Error(`SIM HTTP ${r.status}: ${data.error||'request failed'}`);
      job.resolve(data);
    }catch(e){job.reject(e);}
    finally{this.active=false;this.drain();}
  }
}
