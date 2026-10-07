#pragma once
#include "NativeCommandEvidence.hpp"

namespace RTXResidentEvidence058 {
namespace E=RTXNativeEvidence036;namespace P=E::P;
constexpr uint64_t InfoMagic=UINT64_C(0x5254585253493538),HeaderMagic=UINT64_C(0x5254585253443538);
inline bool initial(E::Bytes info,uint64_t generation){
 if(!E::shape(info,256)||!generation)return false;
 const uint64_t values[]={InfoMagic,1,generation,1,1};
 for(unsigned i=0;i<5;++i)if(P::get64(info.data+i*8)!=values[i])return false;
 return P::zero(info.data,40,256);
}
inline bool header(std::array<uint8_t,128> &out,uint64_t generation,uint64_t epoch,uint64_t completed,E::Bytes payload){
 P::Library lib;if(!generation||!epoch||epoch==UINT64_MAX||!E::shape(payload,4608)||!P::decode(payload.data,512,payload.data+512,4096,lib)||!RtxLibraryUpload036::profile(lib))return false;
 out.fill(0);auto *p=out.data();P::Q::put64(p,HeaderMagic);P::Q::put32(p+8,1);P::Q::put32(p+12,128);P::Q::put64(p+16,generation);P::Q::put64(p+24,epoch);P::Q::put64(p+32,completed);P::Q::put32(p+40,4608);P::Q::put32(p+44,0x86);
 GSPDigest::SHA256 hash;hash.update(payload.data,4608);hash.finish(p+48);return true;
}
inline bool sealed(E::Bytes info,E::Bytes header){
 if(!E::shape(info,256)||!E::shape(header,128))return false;
 const uint64_t values[]={InfoMagic,1,P::get64(header.data+16),P::get64(header.data+24),3,0,4608,P::get64(header.data+32)};
 for(unsigned i=0;i<8;++i)if(P::get64(info.data+i*8)!=values[i])return false;
 return P::zero(info.data,64,160)&&E::equal(info.data+160,header.data+48,32)&&E::equal(info.data+192,header.data+48,32)&&P::zero(info.data,224,256);
}
inline bool replaced(E::Bytes info,E::Bytes header,unsigned children){
 if(!E::shape(info,256)||!E::shape(header,128)||!E::childSize(children)||P::get64(header.data+24)==UINT64_MAX)return false;
 const uint64_t values[]={InfoMagic,1,P::get64(header.data+16),P::get64(header.data+24)+1,1,0,4608,P::get64(header.data+32),1,1,1,1,1,1};
 for(unsigned i=0;i<14;++i)if(P::get64(info.data+i*8)!=values[i])return false;
 const auto v=[&](unsigned i){return P::get64(info.data+i*8);};
 return v(14)<=UINT32_MAX&&v(14)!=UINT32_MAX&&v(14)==v(15)&&v(16)==2*(12+children/4096)&&v(17)==1&&v(19)<E::N::BudgetNs&&
  E::equal(info.data+160,header.data+48,32)&&E::equal(info.data+192,header.data+48,32)&&v(28)==1&&P::zero(info.data,232,256);
}
}
