#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_camera.h>
#include "dl_detect_base.hpp"
#include "dl_detect_espdet_postprocessor.hpp"
#include "dl_image_jpeg.hpp"
#include "cat_model.h"
extern "C" {
#include "src/apriltag.h"
#include "src/tag36h11.h"
}
extern SemaphoreHandle_t stateLock;
extern JsonDocument config;
extern void enqueueCamera(const String &);
class CatModel : public dl::detect::DetectImpl {
public:
  CatModel() {
    m_model=new dl::Model((const char*)CAT_MODEL,fbs::MODEL_LOCATION_IN_FLASH_RODATA);
    m_model->minimize();
    m_image_preprocessor=new dl::image::ImagePreprocessor(m_model,{0,0,0},{255,255,255});
    m_image_preprocessor->enable_letterbox({114,114,114});
    m_postprocessor=new dl::detect::ESPDetPostProcessor(m_model,m_image_preprocessor,.6,.7,10,{{8,8,4,4},{16,16,8,8},{32,32,16,16}});
  }
};
void processVision(camera_fb_t *fb) {
  static CatModel *cats=nullptr;static apriltag_detector_t *tags=nullptr;static uint8_t *gray=nullptr;static uint32_t last=0;
  if(millis()-last<350)return;last=millis();uint32_t start=millis();
  if(!cats){cats=new CatModel();tags=apriltag_detector_create();apriltag_detector_add_family_bits(tags,tag36h11_create(),0);tags->nthreads=1;tags->quad_decimate=1.5;tags->refine_edges=1;gray=(uint8_t*)ps_malloc(320*240);}
  dl::image::jpeg_img_t jpg={.data=fb->buf,.data_len=fb->len};auto img=dl::image::sw_decode_jpeg(jpg,dl::image::DL_IMAGE_PIX_TYPE_RGB888);
  if(!img.data||!gray){if(img.data)heap_caps_free(img.data);return;}
  JsonDocument d;d["op"]="camera";auto v=d["vision"].to<JsonObject>();v["source"]="OV_CAMERA";v["at"]=start;v["frameWidth"]=fb->width;v["frameHeight"]=fb->height;
  auto cat=v["cat"].to<JsonObject>();cat["detected"]=false;
  auto &results=cats->run(img);float best=0;for(auto &r:results)if(r.score>best){best=r.score;cat["detected"]=true;cat["score"]=r.score;cat["centerX"]=(r.box[0]+r.box[2])/(2.0f*fb->width);cat["width"]=(r.box[2]-r.box[0])/(float)fb->width;cat["bottom"]=r.box[3]/(float)fb->height;for(int i=0;i<4;i++)cat["box"][i]=r.box[i];}
  auto pixels=(uint8_t*)img.data;for(size_t i=0;i<fb->width*fb->height;i++)gray[i]=(pixels[3*i]*77+pixels[3*i+1]*150+pixels[3*i+2]*29)>>8;
  image_u8_t im={.width=(int)fb->width,.height=(int)fb->height,.stride=(int)fb->width,.buf=gray};
  zarray_t *found=apriltag_detector_detect(tags,&im);auto arr=v["tags"].to<JsonArray>();
  for(int i=0;i<zarray_size(found)&&i<24;i++){apriltag_detection_t *tag;zarray_get(found,i,&tag);auto o=arr.add<JsonObject>();o["id"]=tag->id;o["centerX"]=tag->c[0]/fb->width;float width=0;for(int k=0;k<4;k++)width+=hypotf(tag->p[k][0]-tag->p[(k+1)%4][0],tag->p[k][1]-tag->p[(k+1)%4][1]);o["width"]=width/(4.0f*fb->width);o["margin"]=tag->decision_margin;for(int k=0;k<4;k++){o["corners"][k][0]=tag->p[k][0];o["corners"][k][1]=tag->p[k][1];}}
  apriltag_detections_destroy(found);heap_caps_free(img.data);
  // A calibrated ground-appearance guard is qualitative, not depth sensing.
  // Unknown calibration stays INVALID. Similar-colour obstacles remain a
  // known limitation of this prototype and require physical commissioning.
  float roi=.75f,threshold=35,bottom=.88f;bool calibrated=false;float reference[72]={};
  xSemaphoreTakeRecursive(stateLock,portMAX_DELAY);roi=config["floorRoiTop"]|.75f;threshold=config["floorDifferenceThreshold"]|35.0f;bottom=config["frontBlockBottom"]|.88f;calibrated=config["frontCalibrated"].as<bool>()&&config["floorReference"].size()==72;for(int k=0;k<72;k++)reference[k]=config["floorReference"][k].as<float>();xSemaphoreGiveRecursive(stateLock);
  // Decode buffer was freed above: use grayscale signature replicated into
  // three channels, keeping the 72-value calibration schema stable.
  auto signature=v["floorSignature"].to<JsonArray>();float difference=0;int feature=0;
  for(int row=0;row<3;row++)for(int col=0;col<8;col++){
    int x0=fb->width/4+col*fb->width/16,x1=fb->width/4+(col+1)*fb->width/16;
    int y0=roi*fb->height+row*(1-roi)*fb->height/3,y1=roi*fb->height+(row+1)*(1-roi)*fb->height/3;
    uint32_t sum=0,n=0;for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++){sum+=gray[y*fb->width+x];n++;}float mean=n?(float)sum/n:0;
    for(int ch=0;ch<3;ch++){signature.add(mean);difference=max(difference,fabsf(mean-reference[feature++]));}
  }
  v["floorDifference"]=difference;v["frontCalibrated"]=calibrated;
  v["front"]=cat["detected"].as<bool>()&&cat["bottom"].as<float>()>bottom?"BLOCKED":!calibrated?"INVALID":difference>threshold?"BLOCKED":"CLEAR";
  v["latencyMs"]=millis()-start;String out;serializeJson(d,out);enqueueCamera(out);
}
