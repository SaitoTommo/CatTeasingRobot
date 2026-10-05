#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <map>
#include <string>
using esp_err_t=int;
constexpr int ESP_OK=0,portMAX_DELAY=0;int frameLock=0;
struct camera_fb_t{uint8_t *buf;size_t len;uint32_t width,height;};
struct httpd_req_t{std::string query,status,type;std::map<std::string,std::string> headers;std::vector<uint8_t> body;};
void xSemaphoreTake(int,int){}void xSemaphoreGive(int){}
void *ps_malloc(size_t n){return malloc(n);}
int httpd_req_get_url_query_str(httpd_req_t *r,char *out,size_t n){if(r->query.empty())return -1;snprintf(out,n,"%s",r->query.c_str());return ESP_OK;}
int httpd_query_key_value(const char *query,const char *,char *out,size_t n){if(strncmp(query,"after=",6))return -1;snprintf(out,n,"%s",query+6);return ESP_OK;}
void httpd_resp_set_status(httpd_req_t *r,const char *s){r->status=s;}
void httpd_resp_set_type(httpd_req_t *r,const char *s){r->type=s;}
void httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *v){r->headers[k]=v;}
int httpd_resp_send(httpd_req_t *r,const char *s,size_t n){r->body.clear();if(n)r->body.assign(s,s+n);return ESP_OK;}
int httpd_resp_sendstr(httpd_req_t *r,const char *s){return httpd_resp_send(r,s,strlen(s));}
#include "../esp32-host/ESP32CatHost/vision_preview.h"
unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(){
 httpd_req_t empty;catPreviewHandler(&empty);CHECK(empty.status=="503 Service Unavailable");
 JsonDocument v;v["frameSeq"]=10;v["at"]=1000;v["mode"]="CAT";v["decodeMs"]=10;v["modelMs"]=180;v["latencyMs"]=210;v["cat"]["valid"]=true;v["cat"]["detected"]=true;v["cat"]["box"][0]=20;
 uint8_t jpeg1[]={0xff,0xd8,1,0xff,0xd9};camera_fb_t fb={jpeg1,sizeof(jpeg1),320,240};recordCatPreview(&fb,v.as<JsonObject>(),1000);
 jpeg1[2]=99;httpd_req_t first;catPreviewHandler(&first);CHECK(first.type=="image/jpeg");CHECK(first.body[2]==1);CHECK(first.headers["Cache-Control"]=="no-store");
 JsonDocument meta;CHECK(!deserializeJson(meta,first.headers["X-Vision-Result"]));CHECK(meta["cat"]["detected"].as<bool>());CHECK(meta["frameSeq"].as<int>()==10);CHECK(meta["modelMs"].as<int>()==180);
 uint8_t jpeg2[]={0xff,0xd8,2,0xff,0xd9};fb.buf=jpeg2;v["frameSeq"]=11;v["cat"]["detected"]=false;v["cat"].remove("box");recordCatPreview(&fb,v.as<JsonObject>(),1400);
 httpd_req_t second;catPreviewHandler(&second);CHECK(second.body[2]==2);CHECK(!deserializeJson(meta,second.headers["X-Vision-Result"]));CHECK(!meta["cat"]["detected"].as<bool>());CHECK(meta["startPeriodMs"].as<int>()==400);
 httpd_req_t same;same.query="after=2";catPreviewHandler(&same);CHECK(same.status=="204 No Content");CHECK(same.body.empty());
 // Profiling need not be enabled; all calls enter the independent timing ring.
 for(int id=3;id<=70;++id){v["frameSeq"]=id+9;recordCatPreview(&fb,v.as<JsonObject>(),600+400*id);}
 JsonDocument state;catPreviewSnapshot(state);CHECK(state["catPreview"]["available"].as<bool>());CHECK(state["catPreview"]["id"].as<int>()==70);CHECK(state["catPreview"]["samples"].size()==16);CHECK(state["catPreview"]["samples"][0][0].as<int>()==55);CHECK(state["catPreview"]["samples"][15][0].as<int>()==70);CHECK(state["catPreview"]["samples"][15][3].as<int>()==180);
 printf("actual cat preview cache and timings: %u checks passed\n",checks);free(catPreviewJpeg);
}
