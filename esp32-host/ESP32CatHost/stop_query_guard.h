#pragma once
#include <stdio.h>
#include <string.h>
// Queries remain useful after a failed stop; they must not release the stop gate.
inline bool stopDiagnosticQuery(const char *wire){
  unsigned id=0;char command[20]={},checksum[3]={},extra=0;
  if(sscanf(wire,"C %u %19[^*]*%2[0123456789abcdefABCDEF]%c",&id,command,checksum,&extra)!=3||!id||id>65535||strlen(checksum)!=2)return false;
  return !strcmp(command,"GET_TELEMETRY")||!strcmp(command,"GET_CONFIG")||!strcmp(command,"PING");
}
