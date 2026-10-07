#pragma once
#include "ProgramRequest.hpp"
namespace RtxProgramImage033 {
namespace P=RtxProgram033;namespace R=RtxProgramRequest033;
constexpr unsigned ImageBytes=24576,PlanBytes=4096;
constexpr unsigned constantOffset(unsigned slot){return 8192+slot*1024;}
constexpr unsigned qmdOffset(unsigned slot){return 12288+slot*256;}
constexpr unsigned dataOffset(unsigned slot){return (slot<2?4096:16384)+(slot%2)*2048;}
constexpr unsigned fenceOffset(unsigned slot){return 20480+slot*256;}
constexpr uint8_t guard(unsigned offset){return uint8_t((offset>=20480?0x5a:0xa5)^((offset*13+7)&255));}
struct Plan {P::Launch launch;uint8_t data[2048]={},constant[1024]={},qmd[256]={},command[32]={},entry[8]={};};
inline bool initial(const uint8_t *wire,size_t wireBytes,const uint8_t *code,size_t codeBytes,uint8_t *image,size_t bytes){
 if(bytes!=ImageBytes||!P::separate(wire,wireBytes,image,bytes)||!P::separate(code,codeBytes,image,bytes))return false;
 P::Library library;if(!P::decode(wire,wireBytes,code,codeBytes,library))return false;
 for(unsigned i=0;i<ImageBytes;++i)image[i]=guard(i);
 for(unsigned i=0;i<P::CodeBytes;++i)image[i]=code[i];
 for(unsigned i=8192;i<16384;++i)image[i]=0;
 for(unsigned j=0;j<P::Slots;++j)P::Q::put32(image+fenceOffset(j),0);
 return true;
}
inline bool plan(const uint8_t *wire,size_t wireBytes,const uint8_t *code,size_t codeBytes,const R::Request &request,
 uint8_t *scratch,size_t scratchBytes,Plan &out){
 if(scratchBytes!=4096)return false;
 const struct Span{const void *p;size_t n;} ins[]={{wire,wireBytes},{code,codeBytes},{&request,sizeof(request)}},
 outs[]={{scratch,scratchBytes},{&out,sizeof(out)}};
 for(unsigned i=0;i<2;++i){for(unsigned j=0;j<3;++j)if(!P::separate(ins[j].p,ins[j].n,outs[i].p,outs[i].n))return false;}
 if(!P::separate(scratch,scratchBytes,&out,sizeof(out)))return false;
 P::Library lib;if(!P::decode(wire,wireBytes,code,codeBytes,lib)||!R::valid(request,lib))return false;
 const unsigned slot=unsigned(request.id-1);const P::Dispatch dispatch={request.program,slot,request.groups};
 if(!P::build(wire,wireBytes,code,codeBytes,dispatch,out.qmd,256,scratch,scratchBytes,out.command,32,out.launch))return false;
 for(unsigned i=0;i<1024;++i)out.constant[i]=scratch[i];
 for(unsigned i=0;i<2048;++i)out.data[i]=guard(dataOffset(slot)+i);
 for(unsigned i=0;i<lib.programs[request.program].parameters*P::BufferBytes;++i)out.data[i]=request.data[i];
 P::Q::put64(out.entry,out.launch.entry);return true;
}
// Call only after exact GPU staging readback and a fresh owner check. This is a
// CPU expected-image update, and conveys no right to publish queue entries.
inline bool commit(const Plan &plan,uint8_t *canonical,size_t bytes){
 if(bytes!=ImageBytes||plan.launch.slot>=P::Slots||!P::separate(&plan,sizeof(plan),canonical,bytes))return false;
 const unsigned slot=plan.launch.slot;
 for(unsigned i=0;i<2048;++i)canonical[dataOffset(slot)+i]=plan.data[i];
 for(unsigned i=0;i<1024;++i)canonical[constantOffset(slot)+i]=plan.constant[i];
 for(unsigned i=0;i<256;++i)canonical[qmdOffset(slot)+i]=plan.qmd[i];
 return true;
}
inline bool encodePlan(const Plan &p,uint8_t *out,size_t bytes){
 if(bytes!=PlanBytes||!P::separate(&p,sizeof(p),out,bytes)||p.launch.slot>=P::Slots)return false;
 for(unsigned i=0;i<PlanBytes;++i)out[i]=0;
 const auto &l=p.launch;const unsigned words[]={l.program,l.slot,l.invocations,l.codeOffset,l.codeBytes,l.registers,l.parameters};
 for(unsigned i=0;i<7;++i)P::Q::put32(out+i*4,words[i]);
 const uint64_t addresses[]={l.programVA,l.constantVA,l.qmdVA,l.fenceVA,l.entry};
 for(unsigned i=0;i<5;++i)P::Q::put64(out+32+i*8,addresses[i]);
 for(unsigned i=0;i<P::MaxBindings;++i)P::Q::put64(out+72+i*8,l.buffers[i]);
 for(unsigned i=0;i<2048;++i)out[256+i]=p.data[i];
 for(unsigned i=0;i<1024;++i)out[2304+i]=p.constant[i];
 for(unsigned i=0;i<256;++i)out[3328+i]=p.qmd[i];
 for(unsigned i=0;i<32;++i)out[3584+i]=p.command[i];
 for(unsigned i=0;i<8;++i)out[3616+i]=p.entry[i];return true;
}

// Byte/lifetime guard only. Mutable output values are intentionally not checked
// against addition or another operation; the application verifies semantics.
// The canonical image and history must be protected service-owned snapshots.
inline bool capture(const uint8_t *actual,size_t actualBytes,const uint8_t *canonical,size_t canonicalBytes,
 const P::Library &lib,const R::Request *history,unsigned staged,unsigned completed){
 if(actualBytes!=ImageBytes||canonicalBytes!=ImageBytes||!history||staged>P::Slots||completed>staged||
    !P::separate(actual,actualBytes,canonical,canonicalBytes))return false;
 for(unsigned slot=0;slot<P::Slots;++slot){
  if(P::get32(canonical+fenceOffset(slot))||P::get32(actual+fenceOffset(slot))!=(slot<completed?P::completion(slot):0))return false;
  if(slot<staged&&(!R::valid(history[slot],lib)||history[slot].id!=uint64_t(slot)+1||history[slot].generation!=history[0].generation))return false;
 }
 for(unsigned offset=0;offset<ImageBytes;++offset){
  bool mutableByte=false;
  if(offset>=12288&&offset<12288+completed*256)mutableByte=true;
  if(offset>=20480&&offset<20480+completed*256&&(offset-20480)%256<4)mutableByte=true;
  if((offset>=4096&&offset<8192)||(offset>=16384&&offset<20480)){
   const unsigned region=offset>=16384?offset-16384:offset-4096;
   const unsigned slot=(offset>=16384?2u:0u)+region/2048,parameter=(region%2048)/256,within=region%256;
   if(slot<completed){const auto &r=history[slot];const auto &p=lib.programs[r.program];
    mutableByte=parameter<p.parameters&&(p.writeMask&(1u<<p.bindings[parameter]))&&within<r.groups*p.localX*4;
   }
  }
  if(!mutableByte&&actual[offset]!=canonical[offset])return false;
 }
 return true;
}
}
