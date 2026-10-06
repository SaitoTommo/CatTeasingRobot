#pragma once
#include <stdint.h>
#include <math.h>

// Target-local observations only: remembered geometry never proves arrival.
struct TargetRecoveryMemory {
  int target=-1;
  uint32_t frame=0,at=0,lostAt=0,previousFrame=0,previousAt=0;
  float center=.5f,previous=.5f,width=0,previousWidth=0;
  bool hasPrevious=false,losing=false;
  void clear(){*this=TargetRecoveryMemory{};}
  void observe(int id,uint32_t seq,uint32_t time,float x,float w){
    if(target!=id)clear();
    if(!seq||!isfinite(x)||!isfinite(w)||x<0||x>1||w<=0||w>1)return;
    if(frame&&int32_t(seq-frame)<=0)return;
    previous=center;previousWidth=width;previousFrame=frame;previousAt=at;hasPrevious=frame!=0;
    center=x;width=w;target=id;frame=seq;at=time;losing=false;lostAt=0;
  }
  int direction(int id,uint32_t time,int fallback) const {
    if(target!=id||!frame||uint32_t(time-at)>5000)return fallback;
    float error=center-.5f;
    if(fabsf(error)>=.03f)return error<0?-1:1;
    if(hasPrevious&&uint32_t(time-previousAt)<=5000&&fabsf(center-previous)>=.03f)return center<previous?-1:1;
    return fallback;
  }
  void miss(uint32_t time){if(!losing){losing=true;lostAt=time;}}
  bool expired(uint32_t time,uint32_t grace) const {return losing&&uint32_t(time-lostAt)>=grace;}
};

using TagRecoveryMemory=TargetRecoveryMemory;
