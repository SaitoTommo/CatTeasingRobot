#pragma once
#include "camera_settings.h"
JsonDocument cameraActual,cameraDefaults;
uint32_t cameraRevision=0;bool cameraPersisted=false;
String cameraError,cameraRequestId;
uint64_t cameraSettingsBarrier=0;
uint8_t cameraDiscardFrames=0;
void cameraSettingsSnapshot(JsonDocument &d){
 auto c=d["cameraSettings"].to<JsonObject>();c["available"]=cameraOk;c["width"]=320;c["height"]=240;
 auto s=cameraOk?esp_camera_sensor_get():nullptr;c["sensorPid"]=s?s->id.PID:0;c["revision"]=cameraRevision;
 c["actual"]=cameraActual;c["persisted"]=cameraPersisted;c["error"]=cameraError;c["requestId"]=cameraRequestId;
 c["ranges"]["gainceiling"]["min"]=0;c["ranges"]["gainceiling"]["max"]=cameraGainCeilingMaximum(s);
 c["ranges"]["gainceiling"]["unit"]=cameraRawGainCeiling(s)?"REGISTER_10_BIT":"ENUM_0_6";
 d["visionBench"]["enabled"]=visionBench;
}
void cameraInvalidateFrames(){
 policyResetTagRecovery();cancelVision();realVision.clear();if(mode=="UART")observation.clear();
 xSemaphoreTake(frameLock,portMAX_DELAY);jpegLen=catPreviewLen=benchJpegLen=0;catPreviewResult=benchResult="";
 catPreviewPreviousStart=0;catPreviewFirstSample=catPreviewCount+1;xSemaphoreGive(frameLock);
 // Reject buffered old frames and frames already being inferred.
 cameraSettingsBarrier=esp_timer_get_time();cameraDiscardFrames=2;
}
void cameraSettingsInput(JsonDocument &d,bool reset){
 cameraRequestId=d["requestId"]|"";cameraError="";
 if(!cameraOk||(phase!="IDLE"&&phase!="STOPPED")||!stopReady()||debugId||manualUntil||configStep>=0||visionBench){
  cameraError="confirmed stop and benchmark off required";logLine("CAMERA_CONFIG_ERROR",cameraError);return;
 }
 if(xSemaphoreTake(cameraSensorLock,pdMS_TO_TICKS(250))!=pdTRUE){cameraError="camera busy; retry";logLine("CAMERA_CONFIG_ERROR",cameraError);return;}
 auto s=esp_camera_sensor_get();JsonDocument target;
 if(reset)target.set(cameraDefaults);else target.set(d["settings"]);
 if(!cameraValidateSettings(s,target.as<JsonObjectConst>(),cameraError)){
  xSemaphoreGive(cameraSensorLock);logLine("CAMERA_CONFIG_ERROR",cameraError);return;
 }
 JsonDocument before;before.set(cameraActual);bool previouslyPersisted=cameraPersisted;
 bool applied=cameraApplySettings(s,target.as<JsonObjectConst>(),cameraActual,cameraError);
 cameraPersisted=false;
 // Persist invalidation before the new camera profile: a power loss between
 // writes must never restore an old ground reference with new image settings.
 bool hadReference=config["frontCalibrated"].as<bool>()||config["floorReference"].size();
 config["frontCalibrated"]=false;config["floorReference"].to<JsonArray>();
 bool referenceSaved=true;
 if(hadReference){String saved;serializeJson(config,saved);referenceSaved=prefs.putString("config",saved)==saved.length();}
 if(!referenceSaved&&applied){cameraError="ground calibration invalidation save failed";String rollback;
  if(cameraApplySettings(s,before.as<JsonObjectConst>(),cameraActual,rollback))cameraPersisted=previouslyPersisted;
  else cameraError+="; rollback failed: "+rollback;
  applied=false;
 }
 if(applied){
  String saved;serializeJson(cameraActual,saved);
  if(prefs.putString("camera",saved)==saved.length())cameraPersisted=true;
  else {cameraError="NVS save failed";String rollback;
   if(cameraApplySettings(s,before.as<JsonObjectConst>(),cameraActual,rollback))cameraPersisted=previouslyPersisted;
   else cameraError+="; rollback failed: "+rollback;
  }
 }
 // A failed setter can still have changed sensor registers; invalidate in either case.
 cameraRevision++;cameraInvalidateFrames();
 xSemaphoreGive(cameraSensorLock);
 logLine(cameraError.length()?"CAMERA_CONFIG_ERROR":"CAMERA_CONFIG",cameraError.length()?cameraError:"applied and saved; ground reference invalidated");
}
void cameraSettingsRestore(sensor_t *s){
 cameraReadSettings(s,cameraDefaults);cameraActual.set(cameraDefaults);
 String saved=prefs.getString("camera","");if(!saved.length())return;
 JsonDocument settings;
 if(deserializeJson(settings,saved))cameraError="saved camera settings invalid JSON";
 else cameraPersisted=cameraApplySettings(s,settings.as<JsonObjectConst>(),cameraActual,cameraError);
 if(cameraError.length())logLine("CAMERA_CONFIG_ERROR",cameraError);else logLine("CAMERA_CONFIG","restored saved camera settings");
}
