#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include "../esp32-host/ESP32CatHost/vision_gate.h"
uint32_t tick=1000,groundReferenceChangedAt=0,frameNumber=10;
uint32_t millis(){return tick;}
String phase="STOPPED";bool stopped=true,cameraOk=true;
uint32_t debugId=0,manualUntil=0;int configStep=-1;
bool stopReady(){return stopped;}
VisionGate visionGate;JsonDocument config;
struct Prefs{int writes=0;size_t putString(const char *,const String &s){writes++;return s.length();}}prefs;
std::vector<String> logs;
void logLine(const char *,const String &s){logs.push_back(s);}
void cancelVision(){visionGate.cancel();}
void requestVision(uint32_t scan,uint16_t step,uint32_t generation,const char *mode){visionGate.request(scan,step,generation,mode,frameNumber,tick*1000ull);}
#include "../esp32-host/ESP32CatHost/ground_calibration_runtime.h"
unsigned checks=0;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL ground calibration line %d: %s\n",__LINE__,#x);return 1;}}while(0)
JsonDocument response(bool calibrated=false){JsonDocument d;d["ticket"]=groundCaptureToken;d["frontCalibrated"]=calibrated;for(int i=0;i<72;i++)d["floorSignature"].add(i);return d;}
int main(){
 // Works while normal preview inference is off: uses two explicit TAGS tickets.
 startGroundCalibration();CHECK(groundCapturePending);CHECK(visionGate.pending);CHECK(String(visionGate.ticket.mode)=="TAGS");
 auto first=visionGate.ticket;CHECK(!visionGate.finish(first,frameNumber,tick*1000ull));
 tick+=100;frameNumber++;CHECK(visionGate.finish(first,frameNumber,tick*1000ull));
 auto v=response();completeGroundCalibration(v.as<JsonObject>());
 CHECK(config["frontCalibrated"].as<bool>());CHECK(config["floorReference"].size()==72);CHECK(prefs.writes==1);
 CHECK(groundCapturePending&&groundCaptureVerifying&&groundCaptureStatus=="VERIFYING");CHECK(groundReferenceChangedAt==tick);
 CHECK(visionGate.pending&&groundCaptureToken!=first.token);
 completeGroundCalibration(v.as<JsonObject>());CHECK(groundCapturePending&&prefs.writes==1);
 auto second=visionGate.ticket;tick+=100;frameNumber++;CHECK(visionGate.finish(second,frameNumber,tick*1000ull));
 v=response(true);completeGroundCalibration(v.as<JsonObject>());CHECK(!groundCapturePending&&groundCaptureStatus=="READY");
 // Unconfirmed stop and unavailable camera do not request frames or write settings.
 stopped=false;auto serial=visionGate.serial;startGroundCalibration();CHECK(!groundCapturePending&&visionGate.serial==serial&&prefs.writes==1);
 stopped=true;cameraOk=false;startGroundCalibration();CHECK(!groundCapturePending&&visionGate.serial==serial);cameraOk=true;
 startGroundCalibration();tick+=8001;groundCalibrationTick();CHECK(!groundCapturePending&&groundCaptureStatus=="ERROR"&&!visionGate.pending);
 startGroundCalibration();phase="PATROL";groundCalibrationTick();CHECK(!groundCapturePending&&!visionGate.pending);phase="STOPPED";
 startGroundCalibration();visionGate.cancel();groundCalibrationTick();CHECK(!groundCapturePending&&groundCaptureError.indexOf("cancelled")>=0);
 startGroundCalibration();v=response();v["floorSignature"].clear();completeGroundCalibration(v.as<JsonObject>());CHECK(!groundCapturePending&&prefs.writes==1);
 JsonDocument snapshot;groundCalibrationSnapshot(snapshot);CHECK(!snapshot["frontCalibration"]["pending"].as<bool>());CHECK(snapshot["frontCalibration"]["error"].as<String>().length()>0);
 printf("ground calibration runtime: %u checks passed\n",checks);
}
