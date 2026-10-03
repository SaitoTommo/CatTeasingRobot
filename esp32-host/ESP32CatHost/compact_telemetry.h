#pragma once
// A snapshot is published only after all bounded frames and a checked DONE.
struct CompactTelemetry {
  uint8_t bytes[50]={}; uint16_t id=0; uint8_t parts=0; uint32_t at=0;
  void reset(){id=0;parts=0;at=0;}
  uint16_t u16(int o){return bytes[o]|(uint16_t(bytes[o+1])<<8);}
  int16_t i16(int o){return int16_t(u16(o));}
  uint32_t u32(int o){return uint32_t(u16(o))|(uint32_t(u16(o+2))<<16);}
  float rpm(int o){uint32_t n=u32(o);float f;memcpy(&f,&n,4);return f;}
  int flag(int o,int b){return (bytes[o]>>b)&1;}
  int io(int b){return (u16(6)>>b)&1;}
  bool valid(){return bytes[0]==1&&bytes[5]<=15&&u16(6)<=16383&&bytes[16]<=15&&bytes[17]<=3&&bytes[42]<=3&&bytes[43]<=3&&bytes[44]<=15&&bytes[45]<=15&&bytes[46]<=15&&bytes[47]<=7&&isfinite(rpm(26))&&isfinite(rpm(30))&&rpm(26)>=0&&rpm(30)>=0;}
  bool add(const String &line,uint32_t now){
    if(id&&now-at>5000)reset();
    unsigned n,p;char hex[35],extra;
    if(sscanf(line.c_str(),"TB %u %u %34s %c",&n,&p,hex,&extra)!=3||!n||n>65535||p>2||strlen(hex)!=(p==2?32:34)){reset();return false;}
    for(size_t j=0;j<strlen(hex);j++)if(!isxdigit(hex[j])){reset();return false;}
    if(n!=id){reset();id=n;at=now;}
    uint8_t data[17];int len=strlen(hex)/2;
    for(int j=0;j<len;j++){char pair[3]={hex[j*2],hex[j*2+1],0};data[j]=strtoul(pair,nullptr,16);}
    if((parts&(1<<p))&&memcmp(bytes+p*17,data,len)){reset();return false;}
    memcpy(bytes+p*17,data,len);parts|=1<<p;return true;
  }
  template<typename F> bool finish(const String &line,uint32_t now,F emit){
    if(!id||line!="DONE "+String(id)+" TELEMETRY"||now-at>5000||parts!=7||!valid()){reset();return false;}
    String prefix="DATA "+String(id)+" HW ";
    auto row=[&](const String &s){emit(prefix+s);};
    row("STATE "+String(u32(1))+" "+(flag(5,0)?"STOPPED":flag(5,1)?"MOVING":"IDLE")+" "+String(flag(5,2))+" "+String(flag(5,3))+" "+String(u16(6)));
    for(int s=0;s<2;s++)row(String(s?"MR ":"ML ")+String(i16(8+s*2))+" "+String(i16(12+s*2))+" "+String(abs(i16(12+s*2)))+" "+String(io(s))+" "+String(flag(16,s*2))+" "+String(flag(16,s*2+1)));
    for(int s=0;s<2;s++)row(String(s?"ER ":"EL ")+String(io(8+s))+" "+String(io(8+s)?flag(17,s):-1)+" "+String(u32(18+s*4))+" "+String(rpm(26+s*4),1)+" "+(io(8+s)?"P":flag(5,3)&&!io(s)?"V":"O"));
    for(int s=0;s<2;s++)row(String(s?"SR ":"SL ")+String(io(4+s))+" "+String(io(6+s))+" "+String(io(6+s)?flag(42,s):-1)+" "+String(i16(34+s*4)/10.0f,1)+" "+String(i16(36+s*4)/10.0f,1)+" "+(flag(43,s)?"I":io(4+s)&&io(6+s)?"P":"O"));
    const char *names[]={"TOP","LEFT","RIGHT","REAR"};
    for(int s=0;s<4;s++)row("T "+String(names[s])+" "+String(io(10+s))+" "+String(io(10+s)?flag(44,s):-1)+" "+String(flag(45,s))+" "+(flag(46,s)?"I":io(10+s)?"P":"O"));
    row("OUT "+String(io(2))+" "+String(flag(47,0))+" -1 "+String(io(3))+" "+String(flag(47,1))+" "+String(flag(47,2)));
    row("FRONT "+String(i16(48)/10.0f,1)+" "+(flag(5,3)?"TEST":"HOST"));
    reset();return true;
  }
};
