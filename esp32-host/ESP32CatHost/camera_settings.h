#pragma once
// Driver status is configured state, not a measurement of live automatic exposure.
// Fixed JPEG/QVGA geometry is required by the recognition pipeline.
#define CAMERA_FIELDS(X) \
 X(quality, set_quality, 0, 63) \
 X(brightness, set_brightness, -2, 2) \
 X(contrast, set_contrast, -2, 2) \
 X(saturation, set_saturation, -2, 2) \
 X(aec_value, set_aec_value, 0, 1200) \
 X(ae_level, set_ae_level, -2, 2) \
 X(aec2, set_aec2, 0, 1) \
 X(agc_gain, set_agc_gain, 0, 30) \
 X(awb_gain, set_awb_gain, 0, 1) \
 X(wb_mode, set_wb_mode, 0, 4) \
 X(raw_gma, set_raw_gma, 0, 1) \
 X(lenc, set_lenc, 0, 1) \
 X(hmirror, set_hmirror, 0, 1) \
 X(vflip, set_vflip, 0, 1) \
 X(awb, set_whitebal, 0, 1) \
 X(aec, set_exposure_ctrl, 0, 1) \
 X(agc, set_gain_ctrl, 0, 1)

inline bool cameraRawGainCeiling(sensor_t *s){return s&&(s->id.PID==0x3660||s->id.PID==0x5640);}
inline int cameraGainCeilingMaximum(sensor_t *s){return cameraRawGainCeiling(s)?1023:6;}
inline bool cameraGainCeilingSupported(sensor_t *s){return s&&(cameraRawGainCeiling(s)?s->get_reg&&s->set_reg:bool(s->set_gainceiling));}
inline void cameraReadSettings(sensor_t *s,JsonDocument &out){
 out.clear();if(!s)return;
 #define READ_CAMERA(key,setter,lo,hi) if(s->setter)out[#key]=int(s->status.key);
 CAMERA_FIELDS(READ_CAMERA)
 #undef READ_CAMERA
 // OV3660/OV5640 use a 10-bit register value. uint8_t status truncates it,
 // and the enum's 0..6 values belong to OV2640, not these sensors.
 if(cameraGainCeilingSupported(s)){
  int ceiling=cameraRawGainCeiling(s)?s->get_reg(s,0x3A18,0x3FF):int(s->status.gainceiling);
  if(ceiling>=0)out["gainceiling"]=ceiling;
 }
}
inline bool cameraValidateSettings(sensor_t *s,JsonObjectConst obj,String &error){
 if(!s||obj.isNull()||!obj.size()){error="camera settings object required";return false;}
 for(JsonPairConst pair:obj){const char *key=pair.key().c_str();bool known=false,supported=false;int low=0,high=0;
  #define CHECK_CAMERA(name,setter,lo,hi) if(!strcmp(key,#name)){known=true;supported=s->setter;low=lo;high=hi;}
  CAMERA_FIELDS(CHECK_CAMERA)
  #undef CHECK_CAMERA
  if(!strcmp(key,"gainceiling")){known=true;supported=cameraGainCeilingSupported(s);low=0;high=cameraGainCeilingMaximum(s);}
  if(!known||!supported){error=String("unsupported camera setting: ")+key;return false;}
  if(!pair.value().is<int>()||pair.value().as<int>()<low||pair.value().as<int>()>high){error=String("invalid camera setting: ")+key;return false;}
 }
 return true;
}
inline bool cameraWriteSettings(sensor_t *s,JsonObjectConst obj,String &error){
 // Manual values before re-enabling automatic controls; driver may quantize values.
 #define WRITE_CAMERA(key,setter,lo,hi) if(!obj[#key].isNull()&&s->setter(s,obj[#key].as<int>())!=0){error=String("camera driver rejected: ")+#key;return false;}
 CAMERA_FIELDS(WRITE_CAMERA)
 #undef WRITE_CAMERA
 if(!obj["gainceiling"].isNull()){
  int value=obj["gainceiling"].as<int>();
  int result=cameraRawGainCeiling(s)?s->set_reg(s,0x3A18,0x3FF,value):s->set_gainceiling(s,gainceiling_t(value));
  if(result!=0){error="camera driver rejected: gainceiling";return false;}
  if(cameraRawGainCeiling(s))s->status.gainceiling=value&0xFF;
 }
 return true;
}
inline bool cameraApplySettings(sensor_t *s,JsonObjectConst obj,JsonDocument &actual,String &error){
 if(!cameraValidateSettings(s,obj,error))return false;
 JsonDocument before;cameraReadSettings(s,before);
 if(cameraRawGainCeiling(s)&&!obj["gainceiling"].isNull()&&before["gainceiling"].isNull()){error="camera gainceiling read failed";return false;}
 // Merge so auto-control state is restored even if a manual setter changes it.
 JsonDocument candidate;candidate.set(before);for(JsonPairConst p:obj)candidate[p.key()]=p.value();
 if(!cameraWriteSettings(s,candidate.as<JsonObjectConst>(),error)){
  String rollback;if(!cameraWriteSettings(s,before.as<JsonObjectConst>(),rollback))error+="; rollback failed: "+rollback;
  cameraReadSettings(s,actual);return false;
 }
 cameraReadSettings(s,actual);
 if(!candidate["gainceiling"].isNull()&&actual["gainceiling"].isNull()){
  error="camera gainceiling readback failed";String rollback;
  if(!cameraWriteSettings(s,before.as<JsonObjectConst>(),rollback))error+="; rollback failed: "+rollback;
  cameraReadSettings(s,actual);return false;
 }
 return true;
}
