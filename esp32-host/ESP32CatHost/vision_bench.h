#pragma once
// Runtime-only profiling; control and stop gates remain owned by policy.
bool visionBench=false;
uint32_t benchGapMs=200,benchCaptureMs=200,benchEpoch=0;
struct BenchSample {uint32_t id,frame,decode,model,total,period,idle,age,tags;};
BenchSample benchSamples[128]={};
uint32_t benchCount=0,benchPreviousStart=0,benchPreviousEnd=0;
uint8_t *benchJpeg=nullptr;size_t benchJpegLen=0;String benchResult;
void recordVisionBench(camera_fb_t *fb,JsonObject v,uint32_t start,uint32_t decode,uint32_t model,uint32_t run){
 xSemaphoreTakeRecursive(stateLock,portMAX_DELAY);
 if(!visionBench||run!=benchEpoch){xSemaphoreGiveRecursive(stateLock);return;}
 xSemaphoreTake(frameLock,portMAX_DELAY);
 uint32_t end=millis(),id=++benchCount;
 benchSamples[(id-1)%128]={id,v["frameSeq"]|0u,decode,model,end-start,benchPreviousStart?start-benchPreviousStart:0,benchPreviousEnd?start-benchPreviousEnd:0,end-(v["at"]|end),v["tagsValid"].as<bool>()?1u:0u};
 benchPreviousStart=start;benchPreviousEnd=end;
 JsonDocument result;result["id"]=id;result["epoch"]=benchEpoch;result["frameSeq"]=v["frameSeq"];result["width"]=fb->width;result["height"]=fb->height;result["cat"]=v["cat"];benchResult="";serializeJson(result,benchResult);
 if(!benchJpeg)benchJpeg=(uint8_t*)ps_malloc(160000);
 benchJpegLen=benchJpeg&&fb->len<160000?fb->len:0;if(benchJpegLen)memcpy(benchJpeg,fb->buf,benchJpegLen);
 xSemaphoreGive(frameLock);xSemaphoreGiveRecursive(stateLock);
}
esp_err_t benchStateHandler(httpd_req_t *r){
 JsonDocument d;xSemaphoreTakeRecursive(stateLock,portMAX_DELAY);d["enabled"]=visionBench;d["gapMs"]=benchGapMs;d["captureGapMs"]=benchCaptureMs;d["epoch"]=benchEpoch;
 xSemaphoreTake(frameLock,portMAX_DELAY);d["count"]=benchCount;auto rows=d["samples"].to<JsonArray>();
 for(uint32_t id=benchCount>128?benchCount-127:1;id<=benchCount;id++){auto &s=benchSamples[(id-1)%128];auto row=rows.add<JsonArray>();row.add(s.id);row.add(s.frame);row.add(s.decode);row.add(s.model);row.add(s.total);row.add(s.period);row.add(s.idle);row.add(s.age);row.add(s.tags);}
 xSemaphoreGive(frameLock);xSemaphoreGiveRecursive(stateLock);String out;serializeJson(d,out);httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Cache-Control","no-store");return httpd_resp_send(r,out.c_str(),out.length());
}
esp_err_t benchFrameHandler(httpd_req_t *r){
 uint8_t *copy=nullptr;size_t len=0;String result;xSemaphoreTake(frameLock,portMAX_DELAY);if(benchJpegLen){len=benchJpegLen;copy=(uint8_t*)ps_malloc(len);if(copy){memcpy(copy,benchJpeg,len);result=benchResult;}}xSemaphoreGive(frameLock);
 if(!copy){httpd_resp_set_status(r,"503 Service Unavailable");return httpd_resp_sendstr(r,"no processed cat frame yet");}
 httpd_resp_set_type(r,"image/jpeg");httpd_resp_set_hdr(r,"Cache-Control","no-store");httpd_resp_set_hdr(r,"X-Vision-Result",result.c_str());auto err=httpd_resp_send(r,(const char*)copy,len);free(copy);return err;
}
