#pragma once
#include "../gsp-compute-0.25/qmd3/QmdProfile.hpp"
// CPU preparation only. No IO, queue, driver or device access is provided here.
namespace RtxVector030 {
namespace Q=QmdProfile;
constexpr unsigned Elements=64,DefaultCount=61,Threads=32,Blocks=2;
constexpr unsigned CodeBytes=512,Registers=12,ConstantBytes=384,ImageBytes=24576;
constexpr uint32_t Completion=0x306030f0,DefaultSeed=0x30603001;
constexpr uint64_t InputAVA=Q::ConstantVA+0x200,InputBVA=Q::ConstantVA+0x300;
inline uint32_t get32(const uint8_t *p){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(p[i])<<(i*8);return v;}
inline void inputs(uint32_t seed,uint32_t *a,uint32_t *b){
 constexpr uint32_t edgeA[]={0,UINT32_MAX,0x80000000,0x7fffffff,1,UINT32_MAX,0xaaaaaaaa,0x55555555};
 constexpr uint32_t edgeB[]={0,1,0x80000000,1,UINT32_MAX,UINT32_MAX,0x55555555,0xaaaaaaaa};
 for(unsigned i=0;i<Elements;++i){seed=seed*1664525u+1013904223u;a[i]=seed;seed=seed*1664525u+1013904223u;b[i]=seed;}
 for(unsigned i=0;i<8;++i){a[i]=edgeA[i];b[i]=edgeB[i];}
}
inline bool build(uint8_t *image,size_t imageBytes,uint8_t *command,size_t commandBytes,
                  const uint8_t *code,size_t codeBytes,const uint32_t *a,const uint32_t *b,unsigned count){
 if(imageBytes!=ImageBytes||commandBytes!=32||codeBytes!=CodeBytes||count>Elements||!a||!b||
    !Q::separate(image,imageBytes,command,commandBytes)||!Q::separate(image,imageBytes,code,codeBytes)||
    !Q::separate(image,imageBytes,a,Elements*4)||!Q::separate(image,imageBytes,b,Elements*4)||
    !Q::separate(command,commandBytes,code,codeBytes)||!Q::separate(command,commandBytes,a,Elements*4)||
    !Q::separate(command,commandBytes,b,Elements*4))return false;
 for(size_t i=0;i<imageBytes;++i)image[i]=0;
 for(unsigned i=0;i<CodeBytes;++i)image[i]=code[i];
 uint8_t *cb=image+8192,*qmd=image+12288;
 if(!Q::build(qmd,256,cb,4096,command,32))return false;
 const struct {Q::B::Field field;uint32_t value;} changes[]={
  {{384,32},Blocks},{{592,16},Threads},{{648,9},Registers},
  {{832,32},Completion},{{1075,13},ConstantBytes/16},{{1641,9},CodeBytes/256}
 };
 for(const auto &v:changes)if(!Q::B::put(qmd,256,v.field,v.value))return false;
 Q::put64(cb+0x160,Q::OutputVA);Q::put64(cb+0x168,InputAVA);Q::put64(cb+0x170,InputBVA);Q::put32(cb+0x178,count);
 for(unsigned i=0;i<Elements;++i){
  Q::put32(cb+0x200+i*4,a[i]);Q::put32(cb+0x300+i*4,b[i]);
  // Every active result differs from its initial poison, including modulo overflow.
  Q::put32(image+16384+i*4,~(a[i]+b[i]));
 }
 for(unsigned i=256;i<4096;++i)image[16384+i]=uint8_t(0xa5^((i*13+7)&255));
 for(unsigned i=4;i<4096;++i)image[20480+i]=uint8_t(0x5a^((i*13+7)&255));
 return true;
}
inline bool validateCapture(const uint8_t *actual,size_t actualBytes,const uint8_t *initial,size_t initialBytes,unsigned count){
 if(actualBytes!=ImageBytes||initialBytes!=ImageBytes||count>Elements||!Q::separate(actual,actualBytes,initial,initialBytes))return false;
 // These bytes are immutable: SASS, inputs, parameters, unused QMD tail, guards.
 for(unsigned i=0;i<ImageBytes;++i){
  if((i>=12288&&i<12544)||(i>=16384&&i<16384+count*4)||(i>=20480&&i<20484))continue;
  if(actual[i]!=initial[i])return false;
 }
 if(get32(initial+8192+0x178)!=count||get32(initial+20480)||get32(actual+20480)!=Completion)return false;
 for(unsigned i=0;i<count;++i){
  const uint32_t sum=get32(initial+8192+0x200+i*4)+get32(initial+8192+0x300+i*4);
  if(get32(initial+16384+i*4)!=~sum||get32(actual+16384+i*4)!=sum)return false;
 }
 return true;
}
}
