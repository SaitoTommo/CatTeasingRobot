#define main originalPolicyMain
#include "policy-core.test.cpp"
#undef main
struct FakePrefs {bool fail=false,mismatch=false;String value;size_t putString(const char *,const String &s){if(fail)return 0;value=s;return s.length();}String getString(const char *,const char *){return mismatch?String("bad"):value;}}prefs;
#include "../esp32-host/ESP32CatHost/front_switch.h"
int main(){
 fixture();String error;CHECK(FrontSwitch::persist(false,error));config["frontAvoidanceEnabled"]=true;FrontSwitch::restore();CHECK(!config["frontAvoidanceEnabled"].as<bool>());
 CHECK(FrontSwitch::persist(true,error));config["frontAvoidanceEnabled"]=false;FrontSwitch::restore();CHECK(config["frontAvoidanceEnabled"].as<bool>());
 prefs.fail=true;CHECK(!FrontSwitch::persist(false,error));prefs.fail=false;prefs.mismatch=true;CHECK(!FrontSwitch::persist(false,error));prefs.mismatch=false;
 prefs.value="";config["frontAvoidanceEnabled"]=false;FrontSwitch::restore();CHECK(!config["frontAvoidanceEnabled"].as<bool>());
 prefs.value="invalid";FrontSwitch::restore();CHECK(!config["frontAvoidanceEnabled"].as<bool>());
 printf("front switch persistence: %u checks passed\n",checks);
}
