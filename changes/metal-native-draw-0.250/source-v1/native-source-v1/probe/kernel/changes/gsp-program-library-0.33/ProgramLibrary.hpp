#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../gsp-compute-0.25/qmd3/QmdProfile.hpp"

// Portable staging proposal; no device access, allocation or GPU ownership.
// Raw instruction correctness is the reviewed compiler's responsibility. This
// decoder validates the bounded container/ABI, not arbitrary SASS semantics.
namespace RtxProgram033 {
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
 uint32_t offset=0,bytes=0,registers=0,localX=0,parameters=0,readMask=0,writeMask=0,constantBytes=0;
 uint32_t bindings[MaxBindings]={};
};
struct Library {unsigned count=0,usedCodeBytes=0;Program programs[MaxPrograms];};
inline bool zero(const uint8_t *p,unsigned start,unsigned end){for(unsigned i=start;i<end;++i)if(p[i])return false;return true;}
inline bool decode(const uint8_t *wire,size_t wireBytes,const uint8_t *code,size_t codeBytes,Library &out){
 if(wireBytes!=WireBytes||codeBytes!=CodeBytes||!separate(wire,wireBytes,code,codeBytes)||
    !separate(wire,wireBytes,&out,sizeof(out))||!separate(code,codeBytes,&out,sizeof(out)))return false;
 if(get64(wire)!=Magic||get32(wire+8)!=1||get32(wire+12)!=WireBytes||get32(wire+24)!=0x86||!zero(wire,28,64))return false;
 const unsigned count=get32(wire+16),used=get32(wire+20);
 if(!count||count>MaxPrograms||!used||used>CodeBytes||used%256)return false;
 Library parsed;parsed.count=count;parsed.usedCodeBytes=used;unsigned end=0;
 for(unsigned index=0;index<MaxPrograms;++index){
  const auto *p=wire+64+index*EntryBytes;
  if(index>=count){if(!zero(p,0,EntryBytes))return false;continue;}
  auto &e=parsed.programs[index];
  e.offset=get32(p+4);e.bytes=get32(p+8);e.registers=get32(p+12);e.localX=get32(p+16);
  e.parameters=get32(p+28);e.readMask=get32(p+32);e.writeMask=get32(p+36);e.constantBytes=get32(p+40);
  if(get32(p)!=index+1||e.offset!=end||!e.bytes||e.bytes%128||e.bytes>CodeBytes-end||
     !e.registers||e.registers>255||!e.localX||e.localX>1024||get32(p+20)!=1||get32(p+24)!=1||
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

// Four disjoint 2KiB buffer slots occupy the existing mapped spare/output pages.
// A slot has at most eight 256-byte binding regions, independently of access mode.
// These VAs are proposed layout facts; they do not prove the pages are mapped.
constexpr uint64_t bufferVA(unsigned slot,unsigned parameter){
 return (slot<2?Q::ProgramVA+4096:Q::OutputVA)+uint64_t(slot%2)*2048+uint64_t(parameter)*BufferBytes;
}
constexpr uint64_t commandVA(unsigned slot){return UINT64_C(0x1020001040)+uint64_t(slot)*64;}
constexpr uint32_t completion(unsigned slot){return 0x306033f0u+slot;}
struct Dispatch {unsigned program=0,slot=0,groups=0;};
struct Launch {
 unsigned program=0,slot=0,invocations=0,codeOffset=0,codeBytes=0,registers=0,parameters=0;
 uint64_t programVA=0,constantVA=0,qmdVA=0,fenceVA=0,entry=0,buffers[MaxBindings]={};
};
// qmd/constant/command are caller-owned output scratch, never live VRAM. A native
// adapter must stage only this slot's 1024 constant bytes at the returned VA.
inline bool build(const uint8_t *wire,size_t wireBytes,const uint8_t *code,size_t codeBytes,const Dispatch &d,
 uint8_t *qmd,size_t qmdBytes,uint8_t *constant,size_t constantBytes,uint8_t *command,size_t commandBytes,Launch &launch){
 if(qmdBytes!=256||constantBytes!=4096||commandBytes!=32)return false;
 const struct Span{const void *p;size_t n;} inputs[]={{wire,wireBytes},{code,codeBytes},{&d,sizeof(d)}},
 outputs[]={{qmd,qmdBytes},{constant,constantBytes},{command,commandBytes},{&launch,sizeof(launch)}};
 for(unsigned i=0;i<4;++i){
  for(unsigned j=0;j<3;++j)if(!separate(outputs[i].p,outputs[i].n,inputs[j].p,inputs[j].n))return false;
  for(unsigned j=0;j<i;++j)if(!separate(outputs[i].p,outputs[i].n,outputs[j].p,outputs[j].n))return false;
 }
 Library library;if(!decode(wire,wireBytes,code,codeBytes,library)||d.program>=library.count||d.slot>=Slots||!d.groups||d.groups>ElementsPerBuffer)return false;
 const auto &e=library.programs[d.program];
 if(e.localX>ElementsPerBuffer||d.groups>ElementsPerBuffer/e.localX)return false;
 Launch result;result.program=d.program;result.slot=d.slot;result.invocations=d.groups*e.localX;
 result.codeOffset=e.offset;result.codeBytes=e.bytes;result.registers=e.registers;result.parameters=e.parameters;
 result.programVA=Q::ProgramVA+e.offset;result.constantVA=Q::ConstantVA+d.slot*1024;
 result.qmdVA=Q::QmdVA+d.slot*256;result.fenceVA=Q::FenceVA+d.slot*256;
 result.entry=commandVA(d.slot)|(UINT64_C(1)<<41)|(UINT64_C(8)<<42);
 for(unsigned n=0;n<e.parameters;++n)result.buffers[n]=bufferVA(d.slot,n);
 // The baseline constructor's constants are independently source-oracle tested.
 // All dynamic constraints above are checked before mutating caller output.
 if(!Q::build(qmd,qmdBytes,constant,constantBytes,command,commandBytes))return false;
 const struct Value{Q::B::Field field;uint32_t value;} fields[]={
  {{256,32},uint32_t(result.programVA>>8)},{{1632,9},uint32_t(result.programVA>>40)},
  {{1536,32},uint32_t(result.programVA)},{{1568,17},uint32_t(result.programVA>>32)},{{1641,9},(e.bytes+255)/256},
  {{384,32},d.groups},{{592,16},e.localX},{{648,9},e.registers},
  {{768,32},uint32_t(result.fenceVA)},{{800,8},uint32_t(result.fenceVA>>32)},{{832,32},completion(d.slot)},
  {{1024,32},uint32_t(result.constantVA)},{{1056,17},uint32_t(result.constantVA>>32)},{{1075,13},(e.constantBytes+15)/16}
 };
 for(const auto &v:fields)if(!Q::B::put(qmd,qmdBytes,v.field,v.value))return false;
 // Baseline writes an old first pointer; all parameter slots are overwritten.
 for(unsigned n=0;n<e.parameters;++n)Q::put64(constant+0x160+n*8,result.buffers[n]);
 Q::put32(command+20,uint32_t(result.qmdVA>>8));launch=result;return true;
}
}
