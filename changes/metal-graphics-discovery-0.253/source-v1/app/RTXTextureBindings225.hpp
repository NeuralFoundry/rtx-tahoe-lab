#pragma once
#include "RTXTextureStorage224.hpp"
#include <array>

namespace RTXTexture225 {
constexpr uint32_t Magic=0x54583225, Format=3, FloatFormats226=0x80000498;
inline bool formatContract(uint32_t f){return f==Format||f==FloatFormats226;}
inline bool permits(uint32_t contract,uint32_t f){
 return contract==Format?f==Format:contract==FloatFormats226&&(f==3||f==4||f==7||f==10);
}
constexpr unsigned MetadataBytes=128, DescriptorBytes=256;
struct Resource {uint32_t kind=0,index=0,record=0,format=0;};
using Resources=std::array<Resource,8>;
using Descriptor=std::array<uint8_t,32>;
inline uint32_t get(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline void put(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(i*8));}
inline bool decode(const uint8_t*bytes,unsigned parameters,uint32_t readMask,uint32_t writeMask,Resources&out){
 if(!bytes||parameters<2||parameters>8)return false;
 Resources next{};unsigned textures=0,samplers=0;int lastBuffer=-1,lastTexture=-1,lastSampler=-1;
 for(unsigned i=0;i<8;++i){const auto*p=bytes+i*16;Resource r{get(p),get(p+4),get(p+8),get(p+12)};
  if(i>=parameters){if(r.kind||r.index||r.record||r.format)return false;continue;}
  if(r.kind==1){if(textures||i==parameters-1||r.index>=32||int(r.index)<=lastBuffer||r.record||r.format)return false;lastBuffer=int(r.index);}
  else if(r.kind==2){if(samplers||i==parameters-1||r.index>=128||int(r.index)<=lastTexture||r.record!=textures||!formatContract(r.format))return false;lastTexture=int(r.index);++textures;}
  else if(r.kind==4){if(!textures||i==parameters-1||r.index>=16||int(r.index)<=lastSampler||r.record!=textures+samplers||r.format||!(readMask&(uint32_t(1)<<i))||(writeMask&(uint32_t(1)<<i)))return false;lastSampler=int(r.index);++samplers;}
  else if(r.kind==3){if(i!=parameters-1||!textures||r.index||r.record||r.format)return false;}
  else return false;
  next[i]=r;
 }
 const auto descriptorBit=uint32_t(1)<<(parameters-1);
 if(next[parameters-1].kind!=3||!(readMask&descriptorBit)||(writeMask&descriptorBit))return false;
 out=next;return true;
}
inline bool descriptor(const RTXTexture224::Layout&layout,size_t backingBytes,Descriptor&out,uint32_t contract=Format){
 using namespace RTXTexture224;
 Layout checked;
 if(!permits(contract,static_cast<uint32_t>(layout.format))||!make(layout.format,layout.width,layout.height,layout.offset,layout.rowPitch,backingBytes,checked)||
    layout.pixel!=checked.pixel||layout.storageBytes!=checked.storageBytes)return false;
 Descriptor v{};const uint32_t fields[]={Magic,1,uint32_t(checked.width),uint32_t(checked.height),uint32_t(checked.rowPitch),uint32_t(checked.pixel),static_cast<uint32_t>(checked.format),uint32_t(checked.storageBytes)};
 for(unsigned i=0;i<8;++i)put(v.data()+i*4,fields[i]);out=v;return true;
}
}
