// Reuse the fake sensor, exercising the actual firmware orchestration header.
#define main cameraDriverChecks
#include "camera-settings.test.cpp"
#undef main
#include <map>
#include <vector>
#include "../esp32-host/ESP32CatHost/vision_gate.h"
constexpr int portMAX_DELAY=0;int cameraSensorLock=1,frameLock=2;bool sensorBusy=false;
int pdMS_TO_TICKS(int n){return n;}
int xSemaphoreTake(int,int){return sensorBusy?0:1;}
constexpr int pdTRUE=1;void xSemaphoreGive(int){}
uint64_t esp_timer_get_time(){return 1000000;}
sensor_t testSensor=sensor();sensor_t *esp_camera_sensor_get(){return &testSensor;}
JsonDocument config,realVision,observation;
String mode="UART",phase="STOPPED",catPreviewResult="old",benchResult="old";
bool cameraOk=true,visionBench=false,stopPending=false;uint32_t debugId=0,manualUntil=0;int configStep=-1;
size_t jpegLen=30,catPreviewLen=30,benchJpegLen=30;uint32_t catPreviewPreviousStart=10;
uint32_t catPreviewCount=5,catPreviewFirstSample=1;
VisionGate visionGate;void cancelVision(){visionGate.cancel();}
bool stopReady(){return !stopPending;}
std::vector<std::string> logged,writes;
void logLine(const char *source,const String &line){logged.push_back(std::string(source)+":"+line.c_str());}
struct FakePrefs{
 std::map<std::string,String> values;String failingKey;
 String getString(const char *key,const char *fallback){auto i=values.find(key);return i==values.end()?String(fallback):i->second;}
 size_t putString(const char *key,const String &value){writes.push_back(key);if(failingKey==key)return 0;values[key]=value;return value.length();}
}prefs;
#include "../esp32-host/ESP32CatHost/camera_settings_runtime.h"
void baseline(){
 testSensor=sensor();cameraOk=true;phase="STOPPED";mode="UART";stopPending=false;debugId=manualUntil=0;configStep=-1;visionBench=false;sensorBusy=false;
 config.clear();config["frontCalibrated"]=true;config["floorReference"].to<JsonArray>().add(42);
 realVision["cat"]["detected"]=true;observation.set(realVision);jpegLen=catPreviewLen=benchJpegLen=30;catPreviewResult="old";
 cameraReadSettings(&testSensor,cameraDefaults);cameraActual.set(cameraDefaults);cameraPersisted=false;cameraRevision=0;
 prefs.values.clear();prefs.failingKey="";writes.clear();logged.clear();failQuality=failContrast=0;
}
int main(){
 baseline();auto d=request(R"({"requestId":"one","settings":{"quality":8,"aec":0,"aec_value":333}})");
 auto previousEpoch=visionGate.serial;cameraSettingsInput(d,false);
 CHECK(cameraError.length()==0);CHECK(cameraPersisted);CHECK(cameraRequestId=="one");CHECK(cameraRevision==1);
 CHECK(writes.size()==2);CHECK(writes[0]=="config");CHECK(writes[1]=="camera");
 CHECK(!config["frontCalibrated"].as<bool>());CHECK(config["floorReference"].size()==0);
 CHECK(jpegLen==0&&catPreviewLen==0&&benchJpegLen==0);CHECK(realVision.size()==0&&observation.size()==0);
 CHECK(visionGate.serial>previousEpoch);CHECK(cameraSettingsBarrier==1000000);CHECK(cameraDiscardFrames==2);
 CHECK(catPreviewFirstSample==catPreviewCount+1);
 auto saved=request(prefs.values["camera"].c_str());CHECK(saved["quality"].as<int>()==8);CHECK(saved["aec_value"].as<int>()==320);
 testSensor=sensor();cameraSettingsRestore(&testSensor);CHECK(testSensor.status.quality==8);CHECK(cameraPersisted);
 JsonDocument out;cameraSettingsSnapshot(out);CHECK(out["cameraSettings"]["actual"]["aec_value"].as<int>()==320);
 cameraSettingsInput(d,true);CHECK(testSensor.status.quality==12);CHECK(cameraPersisted);
 // Neither motion, unconfirmed stop, concurrent debug/benchmark, nor busy sensor applies settings.
 for(int reason=0;reason<7;reason++){
  baseline();switch(reason){case 0:phase="TAG_SEARCH";break;case 1:stopPending=true;break;case 2:debugId=1;break;case 3:manualUntil=1;break;case 4:configStep=0;break;case 5:visionBench=true;break;case 6:sensorBusy=true;break;}
  cameraSettingsInput(d,false);CHECK(cameraError.length());CHECK(writes.empty());CHECK(testSensor.status.quality==12);CHECK(cameraRevision==0);
 }
 baseline();prefs.failingKey="camera";cameraSettingsInput(d,false);CHECK(cameraError.indexOf("NVS")>=0);CHECK(!cameraPersisted);CHECK(testSensor.status.quality==12);CHECK(cameraActual["quality"].as<int>()==12);CHECK(cameraRevision==1);
 baseline();prefs.failingKey="config";cameraSettingsInput(d,false);CHECK(cameraError.indexOf("calibration")>=0);CHECK(testSensor.status.quality==12);CHECK(prefs.values.find("camera")==prefs.values.end());
 baseline();failContrast=1;cameraSettingsInput(d,false);CHECK(cameraError.indexOf("driver rejected")>=0);CHECK(!cameraPersisted);CHECK(testSensor.status.quality==12);CHECK(prefs.values.find("camera")==prefs.values.end());CHECK(jpegLen==0);
 printf("camera firmware runtime: %u checks passed\n",checks);
}
