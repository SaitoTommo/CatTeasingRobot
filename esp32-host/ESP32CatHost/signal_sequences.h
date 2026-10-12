#pragma once
#include <stdint.h>
namespace SignalSequence {
constexpr uint16_t lightLengths[4]={0,1800,2400,3000};
constexpr uint16_t soundLengths[4]={0,600,1100,1600};
constexpr uint16_t offsets[4][3]={{0,0,0},{0,0,0},{0,800,0},{0,600,1200}};
constexpr uint16_t beeps[4][3]={{0,0,0},{600,0,0},{180,180,0},{150,150,150}};
constexpr uint8_t counts[4]={0,1,2,3};
inline bool lightOn(int light,uint32_t t){return t<lightLengths[light]&&(light==1||light==2&&t%800<300||light==3&&(t%1200<180||t%1200>=400&&t%1200<580));}
}
