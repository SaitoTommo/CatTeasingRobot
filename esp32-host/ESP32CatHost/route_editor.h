#pragma once
namespace RouteEditor {
String requestId,status,error;
bool valid(JsonArray route){
 if(!route.size()||route.size()>128)return false;
 for(JsonVariant v:route)if(!v.is<int>()||v.as<int>()<0||v.as<int>()>586)return false;
 return true;
}
bool persist(JsonArray route,String &reason){
 String saved;serializeJson(route,saved);
 if(prefs.putString("route",saved)!=saved.length()||prefs.getString("route","")!=saved){reason="路线写入或回读失败，未确认保存成功";return false;}
 return true;
}
void restore(){
 String saved=prefs.getString("route","");if(!saved.length())return;
 JsonDocument d;
 if(deserializeJson(d,saved)||!d.is<JsonArray>()||!valid(d.as<JsonArray>())){logLine("CONFIG_ERROR","invalid saved route; retaining legacy route");return;}
 config["route"]=d.as<JsonArray>();
}
void input(JsonDocument &d){
  String id=d["requestId"]|"";
  if(!id.length()||id.length()>96)return;
  requestId=id;status="error";error="";
  if((phase!="IDLE"&&phase!="STOPPED")||!stopReady()||debugId||manualUntil||configStep>=0){error="请先停止巡游或退出遥控，等待停车确认后保存路线";return;}
  if(!d["route"].is<JsonArray>()){error="路线必须是标签数组";return;}
  auto route=d["route"].as<JsonArray>();
  if(!route.size()||route.size()>128){error="请输入 1–128 个路线节点";return;}
  for(JsonVariant v:route)if(!v.is<int>()||v.as<int>()<0||v.as<int>()>586){error="标签 ID 必须是 0–586 的整数";return;}
  // The large config string can exhaust contiguous NVS space; persist only the route.
  if(!persist(route,error)){logLine("CONFIG_ERROR",error);return;}
  config["route"]=route;policyResetTagRecovery();status="saved";logLine("ROUTE","saved "+id);
}
void snapshot(JsonDocument &d){auto result=d["routeSave"].to<JsonObject>();result["requestId"]=requestId;result["status"]=status;result["error"]=error;}
}
