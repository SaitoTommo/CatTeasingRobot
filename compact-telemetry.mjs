// TB v1: 50 little-endian bytes, split into three bounded CRC-protected frames.
export function compactCrc(text) {
  let crc = 0;
  for (const c of text) { crc ^= c.charCodeAt(0); for (let i=0;i<8;i++) crc=((crc<<1)^(crc&128?7:0))&255; }
  return crc.toString(16).padStart(2,'0').toUpperCase();
}
export function expandSnapshot(bytes, id) {
  const v=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  if(bytes.length!==50||bytes[0]!==1||bytes[5]>15||v.getUint16(6,true)>16383||bytes[16]>15||bytes[17]>3||bytes[42]>3||bytes[43]>3||bytes[44]>15||bytes[45]>15||bytes[46]>15||bytes[47]>7) return null;
  const bit=(x,b)=>(x>>b)&1, io=b=>bit(v.getUint16(6,true),b), i16=o=>v.getInt16(o,true), u32=o=>v.getUint32(o,true), dist=o=>(i16(o)/10).toFixed(1);
  if (![26,30].every(o=>Number.isFinite(v.getFloat32(o,true))&&v.getFloat32(o,true)>=0)) return null;
  const rows=[], row=(kind,...a)=>rows.push(`DATA ${id} HW ${kind} ${a.join(' ')}`);
  row('STATE',u32(1),bit(bytes[5],0)?'STOPPED':bit(bytes[5],1)?'MOVING':'IDLE',bit(bytes[5],2),bit(bytes[5],3),v.getUint16(6,true));
  for(let s=0;s<2;s++) row(s?'MR':'ML',i16(8+s*2),i16(12+s*2),Math.abs(i16(12+s*2)),io(s),bit(bytes[16],s*2),bit(bytes[16],s*2+1));
  for(let s=0;s<2;s++) row(s?'ER':'EL',io(8+s),io(8+s)?bit(bytes[17],s):-1,u32(18+s*4),v.getFloat32(26+s*4,true).toFixed(1),io(8+s)?'P':bit(bytes[5],3)&&!io(s)?'V':'O');
  for(let s=0;s<2;s++) row(s?'SR':'SL',io(4+s),io(6+s),io(6+s)?bit(bytes[42],s):-1,dist(34+s*4),dist(36+s*4),bit(bytes[43],s)?'I':io(4+s)&&io(6+s)?'P':'O');
  for(let s=0;s<4;s++) row('T',['TOP','LEFT','RIGHT','REAR'][s],io(10+s),io(10+s)?bit(bytes[44],s):-1,bit(bytes[45],s),bit(bytes[46],s)?'I':io(10+s)?'P':'O');
  row('OUT',io(2),bit(bytes[47],0),-1,io(3),bit(bytes[47],1),bit(bytes[47],2));
  row('FRONT',dist(48),bit(bytes[5],3)?'TEST':'HOST');
  return rows;
}
export class CompactTelemetry {
  reset(){this.id=0;this.parts=[];this.at=0;}
  constructor(){this.reset();}
  accept(raw,now){
    const match=/^(TB (\d+) ([012]) ([0-9A-F]+)|DONE (\d+) TELEMETRY)\*([0-9A-Fa-f]{2})$/.exec(raw.trim());
    if(!match) return null;
    if(compactCrc(match[1])!==match[6].toUpperCase()){this.reset();return null;}
    if(this.id&&now-this.at>5000)this.reset();
    if(match[5]){
      if(Number(match[5])!==this.id||this.parts.filter(Boolean).length!==3){this.reset();return null;}
      const bytes=Uint8Array.from(this.parts.join('').match(/../g),x=>parseInt(x,16)),id=this.id;
      this.reset();return expandSnapshot(bytes,id);
    }
    const id=Number(match[2]),part=Number(match[3]),hex=match[4];
    if(!id||id>65535||hex.length!==(part===2?32:34)){this.reset();return null;}
    if(id!==this.id){this.reset();this.id=id;this.at=now;}
    if(this.parts[part]&&this.parts[part]!==hex){this.reset();return null;}
    this.parts[part]=hex;return null;
  }
}
