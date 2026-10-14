#pragma once
namespace BalanceSettings {
bool persist(uint16_t value,String &error){
 if(value<20||value>1000){error="invalid balance interval";return false;}
 if(prefs.putUShort("sbMs",value)!=sizeof(uint16_t)||prefs.getUShort("sbMs",0)!=value){error="balance interval NVS write/readback failed; not applied";return false;}
 return true;
}
void restore(){
 uint16_t value=prefs.getUShort("sbMs",0);if(!value)return;
 if(value>=20&&value<=1000)config["straightBalanceIntervalMs"]=value;
 else logLine("CONFIG_ERROR","invalid saved balance interval; keeping legacy config");
}
}
