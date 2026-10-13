#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
inline uint8_t wireCrc8(const char *text,size_t length){
  uint8_t c=0;for(size_t n=0;n<length;n++){c^=uint8_t(text[n]);for(int j=0;j<8;j++)c=(c<<1)^((c&128)?7:0);}return c;
}
inline bool protectedReply(const char *line,bool physical,bool telemetryPending){
  const char *prefixes[]={"RX ","TB ","MP ","MS ","MD ","MA ","AK ","VQ ","SD "};
  for(auto p:prefixes)if(!strncmp(line,p,strlen(p)))return true;
  if(strncmp(line,"DONE ",5))return false;
  return physical||(telemetryPending&&strstr(line," TELEMETRY"));
}

inline bool telemetryCompleted(const char *line,uint32_t pending){
  unsigned id=0;char result[16]={},extra=0;
  return pending&&sscanf(line,"DONE %u %15s %c",&id,result,&extra)==2&&id==pending&&!strcmp(result,"TELEMETRY");
}
