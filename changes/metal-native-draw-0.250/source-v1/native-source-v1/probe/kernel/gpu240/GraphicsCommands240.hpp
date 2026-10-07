#pragma once
#include "GraphicsRequest240.hpp"
#include "GraphicsTemplate240.hpp"
// Freestanding kernel-side encoder. Output storage must be service-owned and
// preallocated; no large temporary, allocation, floating point or C++ runtime.
namespace RTXGraphicsCommands240 {
namespace R=RTXGraphicsRequest240;namespace S=RTXSpans165;namespace T=RTXGraphicsTemplate240;
constexpr uint64_t AddressEnd=uint64_t(1)<<40;
static_assert(sizeof(T::Commands)==4804&&sizeof(T::Program)==4096&&sizeof(T::Vertices)==48,"reviewed graphics template sizes");
struct Output {uint32_t commands[2048]{};uint8_t program[4096]{},vertices[48]{};uint64_t ringEntry=0,fenceAddress=0;uint32_t words=0,token=0;};
// Exact IEEE754 bits for positive integer n/2, n<=16384. No FP instructions
// may be used in the kernel. All these values are exactly representable.
inline uint32_t halfBits(uint32_t n){unsigned high=0;for(uint32_t x=n;x>1;x>>=1)++high;return ((high+126)<<23)|((n-(1u<<high))<<(23-high));}
inline bool build(const R::Request&r,const S::Mapping*m,const S::Plan&lease,Output&out){
 if((r.application&&(!R::validVertices249(r.vertices)||r.width>64||r.height>64))||!m||!r.session||r.serial==0||r.serial>UINT32_MAX||!r.width||r.width>16384||!r.height||r.height>16384||
    (r.pitch&127)||r.pitch<uint64_t(r.width)*4||lease.session!=r.session||!lease.ticket||lease.count!=5||
    !R::P::separate(&out,sizeof(out),&r,sizeof(r))||!R::P::separate(&out,sizeof(out),m,5*sizeof(*m))||
    !R::P::separate(&out,sizeof(out),&lease,sizeof(lease)))return false;
 const uint64_t sizes[]={4096,48,uint64_t(r.pitch)*r.height,16,8192},alignments[]={4096,4,128,16,4096};
 for(unsigned n=0;n<5;++n){
  const auto&b=r.bindings[n];const auto&v=m[n];const auto required=(n==2||n==3)?S::Write:S::Read;
  if(!v.allocation||!v.mapping||v.gpuVA<4096||(v.gpuVA&4095)||v.gpuVA>=AddressEnd||
     !v.logicalBytes||!v.mappedBytes||(v.mappedBytes&4095)||v.logicalBytes>v.mappedBytes||v.mappedBytes>AddressEnd-v.gpuVA||
     !S::access(v.access)||(required&~v.access)||!b.handle||b.index!=n||b.bytes!=sizes[n]||
     !S::span(b.offset,b.bytes,v.logicalBytes)||(b.offset&(alignments[n]-1))||
     lease.addresses[n]!=v.gpuVA+b.offset||lease.bytes[n]!=b.bytes)return false;
  for(unsigned k=0;k<n;++k)if(b.handle==r.bindings[k].handle||v.allocation==m[k].allocation||v.mapping==m[k].mapping||
   S::overlap(v.gpuVA,v.mappedBytes,m[k].gpuVA,m[k].mappedBytes))return false;
 }
 const auto program=lease.addresses[R::Program],fragment=program+256,color=lease.addresses[R::Color];
 const auto vertex=lease.addresses[R::Vertex],zero=program+2048,fence=lease.addresses[R::Fence];
 for(unsigned n=0;n<2048;++n)out.commands[n]=n<1201?T::Commands[n]:0;
 for(unsigned n=0;n<4096;++n)out.program[n]=T::Program[n];
 for(unsigned n=0;n<48;++n)out.vertices[n]=r.application?r.vertices[n]:T::Vertices[n];
 for(const auto&patch:T::Relocations){uint32_t value=0;
  switch(patch.kind){
   case T::Kind::ProgramHi:value=uint32_t(program>>32);break;case T::Kind::ProgramLo:value=uint32_t(program);break;
   case T::Kind::FragmentHi:value=uint32_t(fragment>>32);break;case T::Kind::FragmentLo:value=uint32_t(fragment);break;
   case T::Kind::ColorHi:value=uint32_t(color>>32);break;case T::Kind::ColorLo:value=uint32_t(color);break;
   case T::Kind::VertexHi:value=uint32_t(vertex>>32);break;case T::Kind::VertexLo:value=uint32_t(vertex);break;
   case T::Kind::ZeroHi:value=uint32_t(zero>>32);break;case T::Kind::ZeroLo:value=uint32_t(zero);break;
   case T::Kind::FenceHi:value=uint32_t(fence>>32);break;case T::Kind::FenceLo:value=uint32_t(fence);break;
   case T::Kind::Pitch:value=r.pitch;break;case T::Kind::Height:value=r.height;break;
   case T::Kind::ScaleX:value=halfBits(r.width);break;case T::Kind::ScaleY:value=halfBits(r.height);break;
   case T::Kind::WidthClip:value=r.width<<16;break;case T::Kind::HeightClip:value=r.height<<16;break;
   case T::Kind::Token:value=uint32_t(r.serial);break;
  }
  // Word534 is viewport Y scale at C797 method0x0a04. Word537 is
  // Y offset at0x0a10 and stays positive. The Metal origin is upper-left.
  if(r.application&&patch.word==534)value|=0x80000000u;
  out.commands[patch.word]=value;
 }
 out.words=1201;out.token=uint32_t(r.serial);out.fenceAddress=fence;
 out.ringEntry=lease.addresses[R::Command]|(uint64_t(1)<<41)|(uint64_t(1201)<<42);
 return true;
}
}
