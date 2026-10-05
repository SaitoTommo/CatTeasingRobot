#pragma once
#include <lwip/sockets.h>
// One worker shares each cached JPEG among at most four async requests. The
// HTTP thread remains available for API requests while video connections live.
struct VideoClient {httpd_req_t *request=nullptr;uint32_t previous=0,frames=0;};
VideoClient videoClients[STREAM_HTTP_MAX_SOCKETS];
SemaphoreHandle_t videoLock=nullptr;
bool videoWorkerReady=false;
esp_err_t videoUnavailable(httpd_req_t *request,const char *message){httpd_resp_set_status(request,"503 Service Unavailable");httpd_resp_sendstr(request,message);return ESP_FAIL;}
void videoTask(void *){
  while(true){
    bool active=false;xSemaphoreTake(videoLock,portMAX_DELAY);
    for(auto &client:videoClients)active|=client.request!=nullptr;
    xSemaphoreGive(videoLock);
    uint8_t *copy=nullptr;size_t len=0;uint32_t seq=0;
    if(active){xSemaphoreTake(frameLock,portMAX_DELAY);seq=frameSeq;
      if(jpegLen){len=jpegLen;copy=(uint8_t*)ps_malloc(len);if(copy)memcpy(copy,jpegCache,len);}
      xSemaphoreGive(frameLock);
    }
    if(copy){
      char head[100];int n=snprintf(head,sizeof(head),"\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",(unsigned)len);
      for(auto &client:videoClients){
        xSemaphoreTake(videoLock,portMAX_DELAY);httpd_req_t *request=client.request;bool fresh=request&&client.previous!=seq;xSemaphoreGive(videoLock);
        if(!fresh)continue;
        esp_err_t result=httpd_resp_send_chunk(request,head,n);
        if(result==ESP_OK)result=httpd_resp_send_chunk(request,(char*)copy,len);
        xSemaphoreTake(videoLock,portMAX_DELAY);
        if(result==ESP_OK){client.previous=seq;client.frames++;}
        else client.request=nullptr;
        xSemaphoreGive(videoLock);
        // Shut down while async ownership still protects this descriptor.
        // Completing wakes HTTPD to close it; never close a potentially reused
        // descriptor after releasing async ownership.
        if(result!=ESP_OK){int fd=httpd_req_to_sockfd(request);if(fd>=0)shutdown(fd,SHUT_RDWR);httpd_req_async_handler_complete(request);}
      }
      free(copy);
    }
    vTaskDelay(pdMS_TO_TICKS(80));
  }
}
void videoStart(){videoLock=xSemaphoreCreateMutex();if(videoLock)videoWorkerReady=xTaskCreate(videoTask,"http-video",4096,nullptr,1,nullptr)==pdPASS;}
esp_err_t streamHandler(httpd_req_t *request){
  if(!videoWorkerReady)return videoUnavailable(request,"video worker unavailable");
  xSemaphoreTake(videoLock,portMAX_DELAY);
  VideoClient *freeClient=nullptr;for(auto &client:videoClients)if(!client.request){freeClient=&client;break;}
  if(!freeClient){xSemaphoreGive(videoLock);return videoUnavailable(request,"four video clients already connected");}
  httpd_resp_set_type(request,"multipart/x-mixed-replace;boundary=frame");httpd_resp_set_hdr(request,"Access-Control-Allow-Origin","*");httpd_resp_set_hdr(request,"Cache-Control","no-store");
  httpd_req_t *async=nullptr;esp_err_t result=httpd_req_async_handler_begin(request,&async);
  if(result==ESP_OK)*freeClient={async,0,0};
  xSemaphoreGive(videoLock);
  if(result!=ESP_OK)videoUnavailable(request,"video request unavailable");
  return result;
}
void videoSnapshot(JsonDocument &d){
  auto v=d["videoStream"].to<JsonObject>();v["path"]="/stream";v["maxClients"]=STREAM_HTTP_MAX_SOCKETS;v["workerReady"]=videoWorkerReady;
  auto clients=v["clients"].to<JsonArray>();if(!videoLock)return;
  xSemaphoreTake(videoLock,portMAX_DELAY);for(int i=0;i<STREAM_HTTP_MAX_SOCKETS;i++)if(videoClients[i].request){auto c=clients.add<JsonObject>();c["slot"]=i;c["frames"]=videoClients[i].frames;c["frameSeq"]=videoClients[i].previous;}xSemaphoreGive(videoLock);
}
