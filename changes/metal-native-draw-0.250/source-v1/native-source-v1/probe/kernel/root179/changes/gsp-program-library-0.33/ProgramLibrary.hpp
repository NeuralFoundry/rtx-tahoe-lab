#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../gsp-compute-0.25/qmd3/QmdProfile.hpp"

// Portable staging proposal; no device access, allocation or GPU ownership.
// Raw instruction correctness is the reviewed compiler's responsibility. This
// decoder validates the bounded container/ABI, not arbitrary SASS semantics.
namespace RtxProgram164 {
constexpr unsigned WireBytes=512,CodeBytes=4096,MaxPrograms=4,MaxBindings=8;
constexpr unsigned EntryBytes=112,Slots=4,ElementsPerBuffer=64,BufferBytes=256;
constexpr uint64_t Magic=UINT64_C(0x5254584c49423333);
namespace Q=QmdProfile;
inline uint32_t get32(const uint8_t *p){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(p[i])<<(8*i);return v;}
inline uint64_t get64(const uint8_t *p){uint64_t v=0;for(unsigned i=0;i<8;++i)v|=uint64_t(p[i])<<(8*i);return v;}
inline bool separate(const void *a,size_t an,const void *b,size_t bn){
 if(!a||!b)return false;const uintptr_t x=reinterpret_cast<uintptr_t>(a),y=reinterpret_cast<uintptr_t>(b);
 return an<=UINTPTR_MAX-x&&bn<=UINTPTR_MAX-y&&!(x<y+bn&&y<x+an);
}
struct Program {
 uint32_t offset=0,bytes=0,registers=0,localX=0,localY=0,localZ=0,parameters=0,readMask=0,writeMask=0,constantBytes=0;
 uint32_t bindings[MaxBindings]={};
};
struct Library {unsigned count=0,usedCodeBytes=0;Program programs[MaxPrograms];};
inline bool zero(const uint8_t *p,unsigned start,unsigned end){for(unsigned i=start;i<end;++i)if(p[i])return false;return true;}
inline bool decode(const uint8_t *wire,size_t wireBytes,const uint8_t *code,size_t codeBytes,Library &out){
 if(wireBytes!=WireBytes||codeBytes!=CodeBytes||!separate(wire,wireBytes,code,codeBytes)||
    !separate(wire,wireBytes,&out,sizeof(out))||!separate(code,codeBytes,&out,sizeof(out)))return false;
 if(get64(wire)!=Magic||get32(wire+8)!=2||get32(wire+12)!=WireBytes||get32(wire+24)!=0x86||!zero(wire,28,64))return false;
 const unsigned count=get32(wire+16),used=get32(wire+20);
 if(!count||count>MaxPrograms||!used||used>CodeBytes||used%256)return false;
 Library parsed;parsed.count=count;parsed.usedCodeBytes=used;unsigned end=0;
 for(unsigned index=0;index<MaxPrograms;++index){
  const auto *p=wire+64+index*EntryBytes;
  if(index>=count){if(!zero(p,0,EntryBytes))return false;continue;}
  auto &e=parsed.programs[index];
  e.offset=get32(p+4);e.bytes=get32(p+8);e.registers=get32(p+12);e.localX=get32(p+16);e.localY=get32(p+20);e.localZ=get32(p+24);
  e.parameters=get32(p+28);e.readMask=get32(p+32);e.writeMask=get32(p+36);e.constantBytes=get32(p+40);
  if(get32(p)!=index+1||e.offset!=end||!e.bytes||e.bytes%128||e.bytes>CodeBytes-end||
     !e.registers||e.registers>255||!e.localX||e.localX>1024||!e.localY||e.localY>1024||!e.localZ||e.localZ>64||uint64_t(e.localX)*e.localY*e.localZ>1024||
     !e.parameters||e.parameters>MaxBindings||e.constantBytes!=0x160+e.parameters*8||!zero(p,44,48)||!zero(p,80,EntryBytes))return false;
  unsigned mask=0;
  for(unsigned n=0;n<MaxBindings;++n){
   const unsigned binding=get32(p+48+n*4);e.bindings[n]=binding;
   if(n>=e.parameters){if(binding)return false;continue;}
   if(binding>=32||(n&&binding<=e.bindings[n-1]))return false;mask|=1u<<binding;
  }
  if(!e.writeMask||((e.readMask|e.writeMask)&~mask))return false;
  if(zero(code,e.offset,e.offset+e.bytes))return false;
  end=(e.offset+e.bytes+255u)&~255u;
  if(end>CodeBytes||!zero(code,e.offset+e.bytes,end))return false;
 }
 if(end!=used||!zero(code,used,CodeBytes))return false;
 out=parsed;return true;
}

} // namespace RtxProgram164
