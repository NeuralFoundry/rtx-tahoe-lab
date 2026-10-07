#pragma once
#include "NativeCommandEvidence.hpp"

// Host-side gate for a successful 0.37 dispatch. Failed/partial records are
// saved unchanged for the offline decoder and cannot pass this gate.
namespace RTXNativeObservation037 {
namespace E=RTXNativeEvidence036;namespace N=E::N;
constexpr unsigned Selector=77,Bytes=512;
constexpr uint64_t Magic=UINT64_C(0x5254584f42533337);
inline bool completed(E::Bytes raw,E::Bytes runtime,E::Bytes job,uint64_t generation,uint64_t serial,unsigned children){
 if(!E::shape(raw,Bytes)||!E::shape(runtime,512)||!E::shape(job,1024)||!generation||!serial||!E::childSize(children))return false;
 const auto v=[&](unsigned i){return E::word(raw,i);};
 const auto rt=[&](unsigned i){return E::word(runtime,i);};
 const auto j=[&](unsigned i){return E::word(job,i);};
 const unsigned phase=serial==UINT64_MAX?4:1;
 const struct Field{unsigned index;uint64_t value;} fields[]={
  {0,Magic},{1,1},{2,generation},{3,serial},{4,phase},{5,serial},{6,0},{7,1},{8,1},{9,1},
  {10,7},{11,1},{17,0},{18,4},{19,serial},{20,1},{21,49152+children},{22,1},{23,1},
  {24,1},{25,0},{26,serial},{27,0}};
 for(auto f:fields)if(v(f.index)!=f.value)return false;
 if(v(12)==0||v(12)>v(13)||v(13)>N::MaxOperations||v(15)>=N::BudgetNs||v(16)!=v(12)+4)return false;
 // Independent structures must describe the same retired job and capture.
 if(rt(0)!=UINT64_C(0x5254585254493335)||rt(1)!=1||rt(2)!=generation||rt(3)!=phase||rt(4)!=serial||rt(16)!=serial||rt(17)!=(serial==UINT64_MAX)||rt(24)||rt(32)!=4||rt(33)!=serial||rt(34)!=1||rt(35)!=children)return false;
 if(j(2)!=generation||j(3)!=serial||j(4)!=phase||j(5)!=serial||j(6)!=serial||j(7)||j(8)!=1||j(9)!=1||j(10)||j(11)!=7||j(12)!=1||j(17)!=1||j(40)!=4||j(41)!=serial||j(42)!=1)return false;
 if(j(0)!=UINT64_C(0x52545852544a3335)||j(1)!=1||j(43)!=1||j(45)!=v(21)||v(12)!=j(14)||v(13)!=j(13)||v(14)!=j(16)||v(15)!=j(15))return false;
 for(unsigned i=28;i<32;++i)if(v(i))return false;
 for(unsigned i=43;i<48;++i)if(v(i))return false;
 for(unsigned i=59;i<64;++i)if(v(i))return false;
 const auto put=((serial&31)+1)&31;
 for(unsigned offset:{32u,48u}){
  const unsigned stage=offset==32?5:4;
  if(v(offset)!=stage||v(offset+1)!=v(16)-(offset==32?0:1)||v(offset+2)!=1||v(offset+3)!=1||v(offset+4)!=1)return false;
  if(v(offset+5)>v(offset+6)||v(offset+6)>v(15))return false;
  if(v(offset+7)!=put||v(offset+8)!=put||v(offset+9)!=serial||v(offset+10)!=serial)return false;
 }
 return v(54)<=v(37);
}
}
