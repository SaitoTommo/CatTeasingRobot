#include <Arduino.h>
#include <ArduinoJson.h>
#include "../esp32-host/ESP32CatHost/pwm_floor.h"
#include "../esp32-host/ESP32CatHost/policy_defaults.h"
#include "../esp32-host/ESP32CatHost/policy_storage.h"
unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(){
 for(int floor:{1,100,140,200,255})for(int request=-255;request<=255;++request){
  int actual=drivePwmFloor(request,floor);
  CHECK(request==0?actual==0:((actual>0)==(request>0)&&abs(actual)>=floor&&abs(actual)<=255&&abs(actual)>=abs(request)));
 }
 CHECK(applyDrivePwmFloor("MOTOR 95 -95",140)=="MOTOR 140 -140");
 CHECK(applyDrivePwmFloor("MOTOR 0 75",140)=="MOTOR 0 140");
 CHECK(applyDrivePwmFloor("MOTOR 0 0",255)=="MOTOR 0 0");
 CHECK(applyDrivePwmFloor("TEST_MOTOR -90 0 300 BYPASS",140)=="TEST_MOTOR -140 0 300 BYPASS");
 CHECK(applyDrivePwmFloor("TEST_MOTOR 210 -210 50 SAFE",140)=="TEST_MOTOR 210 -210 50 SAFE");
 CHECK(applyDrivePwmFloor("DRIVE -1.00 95",140)=="DRIVE -1.00 140");
 CHECK(applyDrivePwmFloor("ROTATE -20.2 30",140)=="ROTATE -20.2 140");
 CHECK(applyDrivePwmFloor("SR 2147483647 -2 75 71 1",140)=="SR 2147483647 -2 140 71 1");
 CHECK(applyDrivePwmFloor("SCAN_ROTATE 11 2 75 9 0",140)=="SCAN_ROTATE 11 2 140 9 0");
 CHECK(applyDrivePwmFloor("MOVE BACKWARD 1 300",140)=="MOVE BACKWARD 140 300");
 CHECK(applyDrivePwmFloor("TURN LEFT 90 300",140)=="TURN LEFT 140 300");
 CHECK(applyDrivePwmFloor("  MOTOR\t+095   -30 ",140)=="  MOTOR\t140   -140 ");
 CHECK(applyDrivePwmFloor("MOTOR 95 -95",100)=="MOTOR 100 -100");
 CHECK(applyDrivePwmFloor("MOTOR 95 -95",1)=="MOTOR 95 -95");
 for(const char *input:{"STOP","SESSION_STOP","VR 1 2 3 0 140","SC 200 100 3000 140000 3000","BEEP 95","DEBUG_MOTOR_MODE DRY_RUN","MOTOR 95 -999","MOTOR 95 nan","MOTOR 95 3.5","MOTOR 1 2 3","MOTOR --1 30","ROTATE 10 0","MOTOR  95\n -30",""})CHECK(applyDrivePwmFloor(input,140)==input);
 CHECK(applyDrivePwmFloor("MOTOR 95 -95",0)=="MOTOR 95 -95");
 CHECK(applyDrivePwmFloor("MOTOR 95 -95",256)=="MOTOR 95 -95");
 // Actual generated validator; defaults merge into old saved configs on boot.
 JsonDocument d;policyDefaults(d);CHECK(d["minimumDrivePwm"].as<int>()==140);
 d["route"][0]=9;d["patrolSearchPwm"]=45;d["patrolAlignPwm"]=30;d["wheelMm"]=67.5;d["trackMm"]=120;d["stopMarginCm"]=3;
 d["tagStopWidth"]=.25;d["catNearWidth"]=.35;d["catFarWidth"]=.12;d["visionMaxAgeMs"]=1500;d["frontBlockBottom"]=.88;d["floorRoiTop"]=.75;d["floorDifferenceThreshold"]=35;
 d["frontAvoidanceEnabled"]=false;d["frontCalibrated"]=false;d["floorReference"].to<JsonArray>();String error;
 CHECK(d["robotActionLimit"].as<int>()==5);d["robotActionLimit"]=5.5;CHECK(!validateSettings(d,error));d["robotActionLimit"]=5;
 CHECK(d["tagLossGraceMs"].as<int>()==1000);CHECK(d["tagRecoveryAngleDeg"].as<float>()==60);
 for(int good:{0,1000,3000}){d["tagLossGraceMs"]=good;CHECK(validateSettings(d,error));}
 for(float bad:{-1.f,3001.f,1.5f,NAN}){d["tagLossGraceMs"]=bad;CHECK(!validateSettings(d,error));}d["tagLossGraceMs"]=1000;
 for(float good:{20.f,60.f,120.f}){d["tagRecoveryAngleDeg"]=good;CHECK(validateSettings(d,error));}
 for(float bad:{19.f,121.f,NAN}){d["tagRecoveryAngleDeg"]=bad;CHECK(!validateSettings(d,error));}d["tagRecoveryAngleDeg"]=60;
 CHECK(d["tagScanMaxFrames"].as<int>()==3);
 for(float bad:{0.f,11.f,1.5f}){d["tagScanMaxFrames"]=bad;CHECK(!validateSettings(d,error));}
 for(int good:{1,3,10}){d["tagScanMaxFrames"]=good;CHECK(validateSettings(d,error));}
 d["tagScanMaxFrames"]=3;
 CHECK(d["catCheckMaxFrames"].as<int>()==3);
 for(float bad:{0.0f,4.0f,2.5f}){d["catCheckMaxFrames"]=bad;CHECK(!validateSettings(d,error));}
 for(int good:{1,2,3}){d["catCheckMaxFrames"]=good;CHECK(validateSettings(d,error));}
 CHECK(validateSettings(d,error));
 JsonDocument saved;saved["centerTolerancePercent"]=12;d["centerTolerancePercent"]=12;migrateCenterSettings(d,saved);CHECK(d["tagCenterTolerancePercent"].as<float>()==12);CHECK(validateSettings(d,error));
 saved["tagCenterTolerancePercent"]=3;d["tagCenterTolerancePercent"]=3;migrateCenterSettings(d,saved);CHECK(d["tagCenterTolerancePercent"].as<float>()==3);CHECK(d["centerTolerancePercent"].as<float>()==12);
 for(float bad:{0.f,21.f,NAN}){d["tagCenterTolerancePercent"]=bad;CHECK(!validateSettings(d,error));}d["tagCenterTolerancePercent"]=5.5;d["centerTolerancePercent"]=5.5;
 for(int good:{1,100,140,255}){d["minimumDrivePwm"]=good;CHECK(validateSettings(d,error));}
 for(float bad:{0.f,256.f,140.5f,NAN,INFINITY}){d["minimumDrivePwm"]=bad;CHECK(!validateSettings(d,error));CHECK(error.indexOf("minimumDrivePwm")>=0);}
 d["minimumDrivePwm"]="140";CHECK(!validateSettings(d,error));d["minimumDrivePwm"]=true;CHECK(!validateSettings(d,error));
 d["minimumDrivePwm"]=140;
 CHECK(d["escapePreyAfterRounds"].as<int>()==5);CHECK(d["explorePreyMinimumSeconds"].as<int>()==30);
 for(const char *key:{"escapePreyPercent","explorePreyPercent"}){
   for(int value:{0,20,100}){d[key]=value;CHECK(validateSettings(d,error));}
   for(float value:{-1.f,101.f,NAN}){d[key]=value;CHECK(!validateSettings(d,error));}d[key]=20;
 }
 for(const char *key:{"escapePreyAfterRounds","explorePreyMinimumSeconds"}){d[key]=3.5;CHECK(!validateSettings(d,error));d[key]=key[0]=='e'&&key[1]=='s'?5:30;}
 d["catFarWidth"]=.4;d["catNearWidth"]=.35;CHECK(!validateSettings(d,error));d["catFarWidth"]=.12;
 d["captureWaitMaxSeconds"]=60;CHECK(validateSettings(d,error));d["captureWaitMaxSeconds"]=61;CHECK(!validateSettings(d,error));
 d["captureWaitMaxSeconds"]=15;d["catConfirmPreyPercent"]=60;d["catConfirmExplorePercent"]=41;CHECK(!validateSettings(d,error));
 d["catConfirmExplorePercent"]=40;CHECK(validateSettings(d,error));
 for(float value:{-1.f,31.f,NAN}){d["catConfirmWaitSeconds"]=value;CHECK(!validateSettings(d,error));}
 d["catConfirmWaitSeconds"]=5;CHECK(validateSettings(d,error));
 d["escapeMinDistanceCm"]=30;d["escapeMaxDistanceCm"]=80;CHECK(validateSettings(d,error));
 d["escapeMinDistanceCm"]=81;CHECK(!validateSettings(d,error));
 d["escapeMinDistanceCm"]=30;d["escapeMaxDistanceCm"]=101;CHECK(!validateSettings(d,error));
 d["escapeMaxDistanceCm"]=30;CHECK(validateSettings(d,error));
 for(const char *key:{"catInteractionIdleSeconds","catObserveTimeoutSeconds"}){for(float v:{0.f,121.f,NAN}){d[key]=v;CHECK(!validateSettings(d,error));}d[key]=10;CHECK(validateSettings(d,error));}
 struct Storage{bool fail=false;String stored;size_t putString(const char *,const String &s){if(fail)return 0;stored=s;return s.length();}} storage;
 JsonDocument active,candidate;active["tagLossGraceMs"]=1000;candidate.set(active);candidate["tagLossGraceMs"]=1500;storage.fail=true;CHECK(!persistPolicySettings(storage,candidate,active));CHECK(active["tagLossGraceMs"].as<int>()==1000);storage.fail=false;CHECK(persistPolicySettings(storage,candidate,active));CHECK(active["tagLossGraceMs"].as<int>()==1500);JsonDocument restored;CHECK(!deserializeJson(restored,storage.stored));CHECK(restored["tagLossGraceMs"].as<int>()==1500);
 printf("actual ESP PWM floor and settings: %u checks passed\n",checks);
}
