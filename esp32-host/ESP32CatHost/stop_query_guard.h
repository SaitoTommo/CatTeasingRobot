#pragma once
#include <stdio.h>
#include <string.h>
// Queries remain useful after a failed stop; they must not release the stop gate.
inline bool stopDiagnosticQuery(const char *wire){
  unsigned id=0;char command[20]={},checksum[3]={},extra=0;
  if(sscanf(wire,"C %u %19[^*]*%2[0123456789abcdefABCDEF]%c",&id,command,checksum,&extra)!=3||!id||id>65535||strlen(checksum)!=2)return false;
  return !strcmp(command,"GET_TELEMETRY")||!strcmp(command,"GET_CONFIG")||!strcmp(command,"PING");
}
// Only bounded output commands may pass a pending (not failed) STOP.
inline bool stopInteractionOutput(const char *wire){
  unsigned id=0;char command[64]={},checksum[3]={},extra=0;
  if(sscanf(wire,"C %u %63[^*]*%2[0123456789abcdefABCDEF]%c",&id,command,checksum,&extra)!=3||!id||id>65535||strlen(checksum)!=2)return false;
  if(!strcmp(command,"LED OFF"))return true;
  unsigned interval=0,duration=0;
  if(sscanf(command,"LED FLASH %u %u %c",&interval,&duration,&extra)==2)return interval>=20&&interval<=5000&&duration>=1&&duration<=10000;
  if(sscanf(command,"BEEP %u %c",&duration,&extra)==1)return duration<=10000;
  return false;
}
