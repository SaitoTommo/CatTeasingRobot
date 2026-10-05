#pragma once
// Last completed cat inference, independent of profiling and policy acceptance.
// JPEG and metadata are replaced together under frameLock.
uint8_t *catPreviewJpeg=nullptr;size_t catPreviewLen=0;
uint32_t catPreviewCount=0;String catPreviewResult;
uint32_t catPreviewFirstSample=1;
struct CatPreviewTiming {uint32_t id,frame,decode,model,total,period;};
CatPreviewTiming catPreviewTimings[64]={};uint32_t catPreviewPreviousStart=0;
void recordCatPreview(camera_fb_t *fb,JsonObject v,uint32_t start){
 xSemaphoreTake(frameLock,portMAX_DELAY);
 if(!catPreviewJpeg)catPreviewJpeg=(uint8_t*)ps_malloc(160000);
 catPreviewLen=catPreviewJpeg&&fb->len<=160000?fb->len:0;
 if(catPreviewLen)memcpy(catPreviewJpeg,fb->buf,catPreviewLen);
 uint32_t id=++catPreviewCount,period=catPreviewPreviousStart?start-catPreviewPreviousStart:0;catPreviewPreviousStart=start;
 catPreviewTimings[(id-1)%64]={id,v["frameSeq"]|0u,v["decodeMs"]|0u,v["modelMs"]|0u,v["latencyMs"]|0u,period};
 JsonDocument result;result["id"]=id;result["frameSeq"]=v["frameSeq"];result["at"]=v["at"];result["width"]=fb->width;result["height"]=fb->height;result["cat"]=v["cat"];result["mode"]=v["mode"];result["latencyMs"]=v["latencyMs"];result["decodeMs"]=v["decodeMs"];result["modelMs"]=v["modelMs"];result["startPeriodMs"]=period;
 catPreviewResult="";serializeJson(result,catPreviewResult);
 xSemaphoreGive(frameLock);
}
void catPreviewSnapshot(JsonDocument &d){
 xSemaphoreTake(frameLock,portMAX_DELAY);
 JsonDocument result;if(catPreviewResult.length()&&!deserializeJson(result,catPreviewResult))d["catPreview"]=result;
 d["catPreview"]["available"]=catPreviewLen>0;
 auto rows=d["catPreview"]["samples"].to<JsonArray>();
 for(uint32_t id=max(catPreviewFirstSample,catPreviewCount>16?catPreviewCount-15:1u);id<=catPreviewCount;++id){auto &s=catPreviewTimings[(id-1)%64];auto row=rows.add<JsonArray>();row.add(s.id);row.add(s.frame);row.add(s.decode);row.add(s.model);row.add(s.total);row.add(s.period);}
 xSemaphoreGive(frameLock);
}
esp_err_t catPreviewHandler(httpd_req_t *r){
 char query[64]={},after[24]={};
 if(httpd_req_get_url_query_str(r,query,sizeof(query))==ESP_OK&&httpd_query_key_value(query,"after",after,sizeof(after))==ESP_OK&&after[0]){
  xSemaphoreTake(frameLock,portMAX_DELAY);bool unchanged=catPreviewLen&&strtoul(after,nullptr,10)==catPreviewCount;xSemaphoreGive(frameLock);
  if(unchanged){httpd_resp_set_status(r,"204 No Content");httpd_resp_set_hdr(r,"Cache-Control","no-store");return httpd_resp_send(r,nullptr,0);}
 }
 uint8_t *copy=nullptr;size_t len=0;String result;
 xSemaphoreTake(frameLock,portMAX_DELAY);
 if(catPreviewLen){len=catPreviewLen;copy=(uint8_t*)ps_malloc(len);if(copy){memcpy(copy,catPreviewJpeg,len);result=catPreviewResult;}}
 xSemaphoreGive(frameLock);
 if(!copy){httpd_resp_set_status(r,"503 Service Unavailable");return httpd_resp_sendstr(r,"no processed cat preview yet");}
 httpd_resp_set_type(r,"image/jpeg");httpd_resp_set_hdr(r,"Cache-Control","no-store");httpd_resp_set_hdr(r,"X-Vision-Result",result.c_str());
 auto err=httpd_resp_send(r,(const char*)copy,len);free(copy);return err;
}
