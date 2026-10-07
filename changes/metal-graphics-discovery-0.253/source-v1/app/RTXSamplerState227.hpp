#pragma once
#include <array>
#include <cstdint>
namespace RTXSampler227 {
constexpr uint32_t Magic=0x534d3227;
enum class Address:uint32_t {Edge=0,Repeat=1,Mirror=2,Zero=3};
struct State {uint32_t normalized=1,s=0,t=0,filter=0;};
using Descriptor=std::array<uint8_t,32>;
inline bool valid(const State&v){
 return v.normalized<=1&&v.s<=3&&v.t<=3&&v.filter<=1&&
   (v.normalized||((v.s==0||v.s==3)&&(v.t==0||v.t==3)));
}
inline uint32_t get(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline bool encode(const State&v,Descriptor&out){
 if(!valid(v))return false;Descriptor next{};uint32_t fields[]={Magic,1,v.normalized,v.s,v.t,v.filter,0,0};
 for(unsigned i=0;i<8;++i)for(unsigned j=0;j<4;++j)next[4*i+j]=uint8_t(fields[i]>>(j*8));out=next;return true;
}
inline bool decode(const uint8_t*p,State&out){
 if(!p||get(p)!=Magic||get(p+4)!=1||get(p+24)||get(p+28))return false;
 State v{get(p+8),get(p+12),get(p+16),get(p+20)};if(!valid(v))return false;out=v;return true;
}
}
