#pragma once
// Two explicitly requested stationary frames: capture reference, then verify it.
bool groundCapturePending=false,groundCaptureVerifying=false;
uint32_t groundCaptureToken=0,groundCaptureStarted=0,groundCaptureGeneration=1;
String groundCaptureError,groundCaptureStatus="IDLE";
void groundCalibrationSnapshot(JsonDocument &d){auto s=d["frontCalibration"].to<JsonObject>();s["pending"]=groundCapturePending;s["startedAt"]=groundCaptureStarted;s["status"]=groundCaptureStatus;s["error"]=groundCaptureError;}
void groundCalibrationFailure(const String &reason){
 if(visionGate.ticket.token==groundCaptureToken)cancelVision();
 groundCapturePending=false;groundCaptureError=reason;groundCaptureStatus="ERROR";logLine("CONFIG_ERROR",reason);
}
void requestGroundFrame(){
 ++groundCaptureGeneration;if(!groundCaptureGeneration)++groundCaptureGeneration;
 requestVision(0,1,groundCaptureGeneration,"TAGS");groundCaptureToken=visionGate.ticket.token;
}
void startGroundCalibration(){
 if((phase!="IDLE"&&phase!="STOPPED")||!stopReady()||debugId||manualUntil||configStep>=0){logLine("CONFIG_ERROR","confirmed stop required first");return;}
 if(!cameraOk){logLine("CONFIG_ERROR","camera unavailable for ground calibration");return;}
 if(groundCapturePending)return;
 groundCaptureError="";groundCapturePending=true;groundCaptureVerifying=false;groundCaptureStarted=millis();groundCaptureStatus="CAPTURING";
 requestGroundFrame();logLine("CALIBRATION","requesting fresh stationary ground frame");
}
void completeGroundCalibration(JsonObject v){
 if(!groundCapturePending||(v["ticket"]|0u)!=groundCaptureToken)return;
 if((phase!="IDLE"&&phase!="STOPPED")||!stopReady()||debugId||manualUntil||configStep>=0){groundCalibrationFailure("ground capture interrupted; confirmed stop required");return;}
 if(v["floorSignature"].size()!=72){groundCalibrationFailure("invalid fresh ground signature");return;}
 if(!groundCaptureVerifying){
  config["floorReference"]=v["floorSignature"];config["frontCalibrated"]=true;groundReferenceChangedAt=millis();
  String saved;serializeJson(config,saved);prefs.putString("config",saved);
  groundCaptureVerifying=true;groundCaptureStatus="VERIFYING";requestGroundFrame();
  logLine("CALIBRATION","ground reference captured; requesting verification frame");
 }else{
  if(!v["frontCalibrated"].as<bool>()){groundCalibrationFailure("ground reference unavailable in verification frame");return;}
  groundCapturePending=false;groundCaptureStatus="READY";logLine("CALIBRATION","ground reference captured and verified with a new frame");
 }
}
void groundCalibrationTick(){
 if(!groundCapturePending)return;
 if((phase!="IDLE"&&phase!="STOPPED")||!stopReady()||debugId||manualUntil||configStep>=0){groundCalibrationFailure("ground capture interrupted; confirmed stop required");return;}
 if(visionGate.ticket.token!=groundCaptureToken){groundCalibrationFailure("ground capture cancelled; retry when stopped");return;}
 if(uint32_t(millis()-groundCaptureStarted)>8000)groundCalibrationFailure("ground capture timeout; check camera and retry");
}
