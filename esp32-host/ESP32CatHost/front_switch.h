#pragma once
// A small independent record avoids rewriting the large ground-reference config.
namespace FrontSwitch {
bool persist(bool enabled,String &error){
 String value=enabled?"1":"0";
 if(prefs.putString("front",value)!=value.length()||prefs.getString("front","")!=value){error="front switch NVS write/readback failed; not applied";return false;}
 return true;
}
void restore(){
 String value=prefs.getString("front","");if(!value.length())return;
 if(value=="0"||value=="1")config["frontAvoidanceEnabled"]=value=="1";
 else logLine("CONFIG_ERROR","invalid saved front switch; keeping legacy config");
}
}
