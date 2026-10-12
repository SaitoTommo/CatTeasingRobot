#define main originalPolicyMain
#include "policy-core.test.cpp"
#undef main
uint32_t debugId=0,manualUntil=0;int configStep=-1;
struct FakePrefs {bool fail=false;String value,key;unsigned writes=0;size_t putString(const char *k,const String &s){writes++;if(fail)return 0;key=k;value=s;return s.length();}String getString(const char *,const char *){return value;}} prefs;
#include "../esp32-host/ESP32CatHost/route_editor.h"
JsonDocument routeRequest(){JsonDocument d;d["requestId"]="test-route";auto a=d["route"].to<JsonArray>();a.add(43);a.add(46);a.add(51);return d;}
int main(){
 fixture();phase="STOPPED";stopGate=true;auto d=routeRequest();RouteEditor::input(d);CHECK(RouteEditor::status=="saved");CHECK(config["route"].size()==3&&config["route"][1].as<int>()==46&&config["route"][2].as<int>()==51);CHECK(prefs.writes==1);JsonDocument saved;deserializeJson(saved,prefs.value);CHECK(saved[1].as<int>()==46);CHECK(prefs.key=="route"&&prefs.value.length()<32);
 auto before=config["route"][1].as<int>();prefs.fail=true;d["route"][1]=99;RouteEditor::input(d);CHECK(RouteEditor::status=="error"&&config["route"][1].as<int>()==before);prefs.fail=false;
 for(auto state:{"PATROL","REMOTE","STARTING"}){phase=state;unsigned n=prefs.writes;RouteEditor::input(d);CHECK(RouteEditor::status=="error"&&prefs.writes==n);}phase="STOPPED";
 stopGate=false;unsigned n=prefs.writes;RouteEditor::input(d);CHECK(RouteEditor::status=="error"&&prefs.writes==n);stopGate=true;
 for(int bad:{-1,587}){d=routeRequest();d["route"][1]=bad;RouteEditor::input(d);CHECK(RouteEditor::status=="error");}
 d=routeRequest();d["route"][1]=1.5;RouteEditor::input(d);CHECK(RouteEditor::status=="error");d["route"].to<JsonArray>();RouteEditor::input(d);CHECK(RouteEditor::status=="error");
 d=routeRequest();auto route=d["route"].to<JsonArray>();for(int i=0;i<129;i++)route.add(1);RouteEditor::input(d);CHECK(RouteEditor::status=="error");
 d=routeRequest();RouteEditor::input(d);JsonDocument snap;RouteEditor::snapshot(snap);CHECK(snap["routeSave"]["requestId"]=="test-route"&&snap["routeSave"]["status"]=="saved");
 config["route"][2]=0;RouteEditor::restore();CHECK(config["route"][2].as<int>()==51);auto previous=prefs.value;prefs.value="[587]";RouteEditor::restore();CHECK(config["route"][2].as<int>()==51);prefs.value=previous;
 policyResumeFrom(2);CHECK(routeIndex==2&&phase=="STARTING"&&config["route"][routeIndex].as<int>()==51);
 printf("actual custom route persistence and resume: %u checks passed\n",checks);
}
