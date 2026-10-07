#pragma once
#include "../root179/OwnedBufferSpans165.hpp"
namespace RTXGraphicsRequest240 {
namespace P=RtxProgram164;namespace S=RTXSpans165;
constexpr uint64_t Magic=0x5254584752503234ULL;
constexpr unsigned Version=240,RequestBytes=256,Resources=5;
enum Role:unsigned {Program=0,Vertex=1,Color=2,Fence=3,Command=4};
struct Request {uint64_t session=0,serial=0;uint32_t width=0,height=0,pitch=0;S::Binding bindings[Resources]{};bool application=false;uint8_t vertices[48]{};};
// Version249 carries one snapshotted triangle. Finite IEEE754 input is
// validated with integer operations; no kernel floating-point execution.
inline bool validVertices249(const uint8_t*p){
 if(!p)return false;
 for(unsigned i=0;i<12;++i){const auto bits=P::get32(p+i*4)&0x7fffffffu;
  if(bits>=0x7f800000u||(i%4<2&&bits>0x3f800000u))return false;
 }return true;
}
inline bool decode(const void*data,size_t n,Request&out){
 if((n!=RequestBytes&&n!=320)||!data||!P::separate(data,n,&out,sizeof(out)))return false;
 const auto*p=static_cast<const uint8_t*>(data);
 const bool application=n==320;
 if(P::get64(p)!=Magic||P::get32(p+8)!=(application?249u:Version)||P::get32(p+12)!=n||
    P::get32(p+44)!=(application?3u:2u)||!P::zero(p,48,64)||!P::zero(p,224,256)||
    (application&&(!validVertices249(p+256)||!P::zero(p,304,320))))return false;
 Request next;next.application=application;
 if(application)for(unsigned i=0;i<48;++i)next.vertices[i]=p[256+i];next.session=P::get64(p+16);next.serial=P::get64(p+24);
 next.width=P::get32(p+32);next.height=P::get32(p+36);next.pitch=P::get32(p+40);
 if(!next.session||!next.serial||next.serial>UINT32_MAX||!next.width||next.width>16384||
    !next.height||next.height>16384||(next.pitch&127)||next.pitch<uint64_t(next.width)*4)return false;
 if(application&&(next.width>64||next.height>64))return false;
 const uint64_t sizes[]={4096,48,uint64_t(next.pitch)*next.height,16,8192};
 const uint64_t alignments[]={4096,4,128,16,4096};
 for(unsigned i=0;i<Resources;++i){
  const auto*b=p+64+i*32;next.bindings[i]={P::get64(b),P::get64(b+8),P::get64(b+16),P::get32(b+24)};
  const auto&v=next.bindings[i];if(!v.handle||v.index!=i||P::get32(b+28)||v.bytes!=sizes[i]||(v.offset&(alignments[i]-1)))return false;
  for(unsigned j=0;j<i;++j)if(next.bindings[j].handle==v.handle)return false;
 }
 out=next;return true;
}
}
