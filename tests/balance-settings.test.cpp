#define main originalPolicyMain
#include "policy-core.test.cpp"
#undef main
struct FakePrefs {bool fail=false,mismatch=false;uint16_t value=0;size_t putUShort(const char *,uint16_t v){if(fail)return 0;value=v;return 2;}uint16_t getUShort(const char *,uint16_t){return mismatch?0:value;}}prefs;
#include "../esp32-host/ESP32CatHost/balance_settings.h"
int main(){
 fixture();String error;config["straightBalanceIntervalMs"]=200;
 BalanceSettings::restore();CHECK(config["straightBalanceIntervalMs"].as<int>()==200);
 for(uint16_t v:{20,50,100,200,1000}){CHECK(BalanceSettings::persist(v,error));config["straightBalanceIntervalMs"]=200;BalanceSettings::restore();CHECK(config["straightBalanceIntervalMs"].as<int>()==v);}
 CHECK(!BalanceSettings::persist(19,error));CHECK(!BalanceSettings::persist(1001,error));
 prefs.fail=true;CHECK(!BalanceSettings::persist(100,error));prefs.fail=false;prefs.mismatch=true;CHECK(!BalanceSettings::persist(100,error));prefs.mismatch=false;
 prefs.value=1001;config["straightBalanceIntervalMs"]=200;BalanceSettings::restore();CHECK(config["straightBalanceIntervalMs"].as<int>()==200);
 printf("balance settings persistence: %u checks passed\n",checks);
}
