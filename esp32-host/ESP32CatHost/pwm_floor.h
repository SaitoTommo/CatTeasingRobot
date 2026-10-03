#pragma once
#include <Arduino.h>
#include <stdlib.h>

// A floor for valid nonzero requests only. Do not repair malformed commands.
inline int drivePwmFloor(int requested,int minimum){
 if(requested==0||requested < -255||requested>255||minimum<1||minimum>255)return requested;
 int magnitude=requested<0?-requested:requested;
 if(magnitude<minimum)magnitude=minimum;
 return requested<0?-magnitude:magnitude;
}

// Shared by sendUno and native tests, before envelope/CRC generation.
// Preserve every non-PWM field, including scan keys, pulse budget and safety flags.
inline String applyDrivePwmFloor(const String &command,int minimum){
 if(minimum<1||minimum>255||command.length()>110)return command;
 unsigned start[8],end[8],count=0,pos=0;
 while(pos<command.length()){
  while(pos<command.length()&&(command[pos]==' '||command[pos]=='\t'))++pos;
  if(pos==command.length())break;
  if(count==8)return command;
  start[count]=pos;
  while(pos<command.length()&&command[pos]!=' '&&command[pos]!='\t')++pos;
  end[count++]=pos;
 }
 if(!count)return command;
 String verb=command.substring(start[0],end[0]);
 unsigned fields[2],number=1;
 bool signedPwm=false;
 if((verb=="MOTOR"&&count==3)||(verb=="TEST_MOTOR"&&count==5)){fields[0]=1;fields[1]=2;number=2;signedPwm=true;}
 else if((verb=="DRIVE"||verb=="ROTATE")&&count==3)fields[0]=2;
 else if((verb=="MOVE"||verb=="TURN")&&count==4)fields[0]=2;
 else if((verb=="SR"||verb=="SCAN_ROTATE")&&count==6)fields[0]=3;
 else return command;
 int values[2];bool changed=false;
 for(unsigned i=0;i<number;++i){
  String token=command.substring(start[fields[i]],end[fields[i]]);
  if(token.length()>4)return command;
  unsigned digit=(token[0]=='+'||token[0]=='-')?1:0;
  if(digit==token.length())return command;
  for(unsigned j=digit;j<token.length();++j)if(token[j]<'0'||token[j]>'9')return command;
  long value=strtol(token.c_str(),nullptr,10);
  if(value<(signedPwm?-255:1)||value>255)return command;
  values[i]=drivePwmFloor((int)value,minimum);changed|=values[i]!=value;
 }
 if(!changed)return command;
 String result;pos=0;
 for(unsigned i=0;i<number;++i){result+=command.substring(pos,start[fields[i]]);result+=String(values[i]);pos=end[fields[i]];}
 return result+command.substring(pos);
}
