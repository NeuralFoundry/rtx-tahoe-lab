#pragma once
#include "RuntimeRequest.hpp"
#include "../gsp-batch-compute-0.31/BatchProfile.hpp"

namespace RtxRuntimeImage032 {
namespace A=RtxApplication032;namespace B=RtxBatch031;namespace Q=QmdProfile;
constexpr unsigned ImageBytes=B::ImageBytes,SlotBytes=1024,OutputBytes=256,PlanBytes=1280;
constexpr unsigned constantPhysical(unsigned slot){return Q::ConstantPhysical+slot*SlotBytes;}
constexpr unsigned outputPhysical(unsigned slot){return Q::OutputPhysical+slot*SlotBytes;}
inline bool slot(const A::Request &r,unsigned index,uint8_t *cb,size_t cbBytes,uint8_t *poison,size_t outBytes){
 if(index>=A::Slots||!A::valid(r)||r.requestId!=uint64_t(index)+1||cbBytes!=SlotBytes||outBytes!=OutputBytes||
    !A::separate(cb,cbBytes,poison,outBytes)||!A::separate(cb,cbBytes,&r,sizeof(r))||!A::separate(poison,outBytes,&r,sizeof(r)))return false;
 for(unsigned i=0;i<SlotBytes;++i)cb[i]=0;
 const uint64_t va=Q::ConstantVA+index*SlotBytes;
 A::put64(cb+0x28,0xfffdc0);A::put64(cb+0x160,Q::OutputVA+index*SlotBytes);
 A::put64(cb+0x168,va+0x200);A::put64(cb+0x170,va+0x300);A::put32(cb+0x178,r.count);
 for(unsigned i=0;i<A::Elements;++i){A::put32(cb+0x200+i*4,r.a[i]);A::put32(cb+0x300+i*4,r.b[i]);A::put32(poison+i*4,~(r.a[i]+r.b[i]));}
 return true;
}
inline bool build(const A::Request *requests,uint8_t *image,size_t imageBytes,uint8_t *commands,size_t commandBytes,
 uint8_t *entries,size_t entryBytes,const uint8_t *code,size_t codeBytes){
 if(!requests||imageBytes!=ImageBytes||commandBytes!=B::CommandBytes||entryBytes!=B::EntryBytes||codeBytes!=B::CodeBytes)return false;
 const struct{void *p;size_t n;} outputs[]={{image,imageBytes},{commands,commandBytes},{entries,entryBytes}};
 for(const auto &o:outputs)if(!A::separate(o.p,o.n,requests,A::Slots*sizeof(A::Request)))return false;
 uint32_t seeds[4]={};unsigned counts[4]={};
 for(unsigned j=0;j<4;++j){if(!A::valid(requests[j])||requests[j].requestId!=uint64_t(j)+1||requests[j].generation!=requests[0].generation)return false;counts[j]=requests[j].count;}
 if(!B::build(image,imageBytes,commands,commandBytes,entries,entryBytes,code,codeBytes,seeds,counts))return false;
 for(unsigned j=0;j<4;++j){
  if(!slot(requests[j],j,image+B::cbOffset(j),SlotBytes,image+B::outOffset(j),OutputBytes)||
     !Q::B::put(image+B::qmdOffset(j),256,{832,32},A::completion(j)))return false;
 }
 return true;
}
// initial is the driver-owned canonical image built above, updated only after
// successful stage readback. Submitted QMDs may be modified by the GPU.
inline bool capture(const uint8_t *actual,size_t actualBytes,const uint8_t *initial,size_t initialBytes,unsigned completed){
 if(actualBytes!=ImageBytes||initialBytes!=ImageBytes||completed>A::Slots||!A::separate(actual,actualBytes,initial,initialBytes))return false;
 for(unsigned j=0;j<4;++j){
  const unsigned count=A::get32(initial+B::cbOffset(j)+0x178);if(count>A::Elements||A::get32(initial+B::fenceOffset(j)))return false;
  if(A::get32(actual+B::fenceOffset(j))!=(j<completed?A::completion(j):0))return false;
  for(unsigned i=0;i<64;++i){
   const uint32_t sum=A::get32(initial+B::cbOffset(j)+0x200+i*4)+A::get32(initial+B::cbOffset(j)+0x300+i*4);
   if(A::get32(initial+B::outOffset(j)+i*4)!=~sum||A::get32(actual+B::outOffset(j)+i*4)!=(j<completed&&i<count?sum:~sum))return false;
  }
 }
 for(unsigned i=0;i<ImageBytes;++i){
  if(i>=12288&&i<12288+completed*256)continue;
  if(i>=16384&&i<20480&&(i-16384)%1024<256)continue;
  if(i>=20480&&i<21504&&(i-20480)%256<4)continue;
  if(actual[i]!=initial[i])return false;
 }return true;
}
}
