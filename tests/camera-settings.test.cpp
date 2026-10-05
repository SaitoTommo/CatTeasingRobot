#include <Arduino.h>
#include <ArduinoJson.h>
#include <cassert>
#include <cstdio>
enum gainceiling_t { GAIN0,GAIN1,GAIN2,GAIN3,GAIN4,GAIN5,GAIN6 };
struct sensor_t {
 struct {int PID=3660;} id;
 struct { int quality=0; int brightness=0; int contrast=0; int saturation=0; int aec_value=0; int ae_level=0; int aec2=0; int agc_gain=0; int awb_gain=0; int wb_mode=0; int raw_gma=0; int lenc=0; int hmirror=0; int vflip=0; int awb=0; int aec=0; int agc=0; int gainceiling=0; } status;
 int (*set_quality)(sensor_t *,int)=nullptr;
 int (*set_brightness)(sensor_t *,int)=nullptr;
 int (*set_contrast)(sensor_t *,int)=nullptr;
 int (*set_saturation)(sensor_t *,int)=nullptr;
 int (*set_aec_value)(sensor_t *,int)=nullptr;
 int (*set_ae_level)(sensor_t *,int)=nullptr;
 int (*set_aec2)(sensor_t *,int)=nullptr;
 int (*set_agc_gain)(sensor_t *,int)=nullptr;
 int (*set_awb_gain)(sensor_t *,int)=nullptr;
 int (*set_wb_mode)(sensor_t *,int)=nullptr;
 int (*set_raw_gma)(sensor_t *,int)=nullptr;
 int (*set_lenc)(sensor_t *,int)=nullptr;
 int (*set_hmirror)(sensor_t *,int)=nullptr;
 int (*set_vflip)(sensor_t *,int)=nullptr;
 int (*set_whitebal)(sensor_t *,int)=nullptr;
 int (*set_exposure_ctrl)(sensor_t *,int)=nullptr;
 int (*set_gain_ctrl)(sensor_t *,int)=nullptr;
 int (*set_gainceiling)(sensor_t *,gainceiling_t)=nullptr;
 int (*get_reg)(sensor_t *,int,int)=nullptr;
 int (*set_reg)(sensor_t *,int,int,int)=nullptr;
 int rawCeiling=1016;
};
#include "../esp32-host/ESP32CatHost/camera_settings.h"
unsigned checks=0;int failQuality=0,failContrast=0;
#define CHECK(x) do {++checks;assert(x);} while(0)
sensor_t sensor(){sensor_t s;
 s.set_quality=[](sensor_t *p,int v){p->status.quality=v;if(failQuality){--failQuality;return -1;}return 0;};
 s.set_brightness=[](sensor_t *p,int v){p->status.brightness=v;return 0;};
 s.set_contrast=[](sensor_t *p,int v){p->status.contrast=v;if(failContrast){--failContrast;return -1;}return 0;};
 s.set_saturation=[](sensor_t *p,int v){p->status.saturation=v;return 0;};
 s.set_aec_value=[](sensor_t *p,int v){p->status.aec_value=v;p->status.aec_value=v/16*16;p->status.aec=0;return 0;};
 s.set_ae_level=[](sensor_t *p,int v){p->status.ae_level=v;return 0;};
 s.set_aec2=[](sensor_t *p,int v){p->status.aec2=v;return 0;};
 s.set_agc_gain=[](sensor_t *p,int v){p->status.agc_gain=v;p->status.agc=0;return 0;};
 s.set_awb_gain=[](sensor_t *p,int v){p->status.awb_gain=v;return 0;};
 s.set_wb_mode=[](sensor_t *p,int v){p->status.wb_mode=v;return 0;};
 s.set_raw_gma=[](sensor_t *p,int v){p->status.raw_gma=v;return 0;};
 s.set_lenc=[](sensor_t *p,int v){p->status.lenc=v;return 0;};
 s.set_hmirror=[](sensor_t *p,int v){p->status.hmirror=v;return 0;};
 s.set_vflip=[](sensor_t *p,int v){p->status.vflip=v;return 0;};
 s.set_whitebal=[](sensor_t *p,int v){p->status.awb=v;return 0;};
 s.set_exposure_ctrl=[](sensor_t *p,int v){p->status.aec=v;return 0;};
 s.set_gain_ctrl=[](sensor_t *p,int v){p->status.agc=v;return 0;};
 s.set_gainceiling=[](sensor_t *p,gainceiling_t v){p->status.gainceiling=v;return 0;};
 s.status.quality=12;s.status.aec=1;s.status.agc=1;return s;
}
JsonDocument request(const char *json){JsonDocument d;CHECK(!deserializeJson(d,json));return d;}
int main(){
 auto s=sensor();JsonDocument actual;String error;
 auto d=request(R"({"quality":8,"brightness":2,"aec_value":333,"agc_gain":14})");
 CHECK(cameraApplySettings(&s,d.as<JsonObjectConst>(),actual,error));CHECK(actual["quality"].as<int>()==8);
 CHECK(actual["aec_value"].as<int>()==320);CHECK(actual["agc_gain"].as<int>()==14);
 CHECK(actual["aec"].as<int>()==1);CHECK(actual["agc"].as<int>()==1);
 // Serialized driver state is the same object restored on the next boot.
 String saved;serializeJson(actual,saved);JsonDocument restored;CHECK(!deserializeJson(restored,saved));auto second=sensor();
 CHECK(cameraApplySettings(&second,restored.as<JsonObjectConst>(),actual,error));CHECK(second.status.quality==8);CHECK(second.status.aec_value==320);
 d=request(R"({"aec":0,"agc":0,"aec_value":656,"agc_gain":10})");CHECK(cameraApplySettings(&s,d.as<JsonObjectConst>(),actual,error));CHECK(s.status.aec==0);CHECK(s.status.agc==0);CHECK(s.status.aec_value==656);
 const char *bad[]={R"({"quality":64})",R"({"brightness":-3})",R"({"aec":2})",R"({"aec_value":1201})",R"({"gainceiling":7})",R"({"contrast":1.5})",R"({"quality":"8"})",R"({"aec":true})",R"({"framesize":4})",R"({})"};
 for(auto json:bad){d=request(json);CHECK(!cameraApplySettings(&s,d.as<JsonObjectConst>(),actual,error));CHECK(s.status.quality==8);CHECK(s.status.aec_value==656);}
 s.set_lenc=nullptr;d=request(R"({"lenc":1})");CHECK(!cameraApplySettings(&s,d.as<JsonObjectConst>(),actual,error));CHECK(error.indexOf("unsupported")>=0);
 d=request(R"({"quality":20,"contrast":2})");failContrast=1;CHECK(!cameraApplySettings(&s,d.as<JsonObjectConst>(),actual,error));CHECK(s.status.quality==8);CHECK(s.status.contrast==0);CHECK(actual["quality"].as<int>()==8);
 failQuality=2;CHECK(!cameraApplySettings(&s,d.as<JsonObjectConst>(),actual,error));CHECK(error.indexOf("rollback failed")>=0);
 CHECK(!cameraApplySettings(nullptr,d.as<JsonObjectConst>(),actual,error));
 // Real OV3660 startup exposed uint8_t 248 while its register can be 1016.
 auto raw=sensor();raw.id.PID=0x3660;raw.status.gainceiling=248;
 raw.get_reg=[](sensor_t *p,int reg,int mask){CHECK(reg==0x3A18&&mask==0x3FF);return p->rawCeiling;};
 raw.set_reg=[](sensor_t *p,int reg,int mask,int value){CHECK(reg==0x3A18&&mask==0x3FF);p->rawCeiling=value;return 0;};
 cameraReadSettings(&raw,actual);CHECK(actual["gainceiling"].as<int>()==1016);CHECK(cameraGainCeilingMaximum(&raw)==1023);
 d=request(R"({"quality":8,"gainceiling":512})");CHECK(cameraApplySettings(&raw,d.as<JsonObjectConst>(),actual,error));CHECK(raw.rawCeiling==512);CHECK(actual["gainceiling"].as<int>()==512);
 d=request(R"({"gainceiling":1024})");CHECK(!cameraApplySettings(&raw,d.as<JsonObjectConst>(),actual,error));CHECK(raw.rawCeiling==512);
 d=request(R"({"gainceiling":1016})");CHECK(cameraApplySettings(&raw,d.as<JsonObjectConst>(),actual,error));CHECK(raw.rawCeiling==1016);
 raw.get_reg=[](sensor_t *,int,int){return -1;};CHECK(!cameraApplySettings(&raw,d.as<JsonObjectConst>(),actual,error));CHECK(error.indexOf("read failed")>=0);
 printf("camera driver settings: %u checks passed\n",checks);
 return 0;
}
