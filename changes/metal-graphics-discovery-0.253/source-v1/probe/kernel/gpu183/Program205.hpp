#pragma once
#include "../root179/changes/gsp-program-library-0.33/ProgramLibrary.hpp"
#include "../root179/DispatchGeometry164.hpp"
#include "../changes/gsp-program-library-0.33/ProgramLibrary.hpp"

// A root-admitted program may use either existing payload ABI. Instruction
// bytes remain unchanged. This is shape validation, not shader authentication.
namespace RTXProgram205 {
namespace P=RtxProgram164;
constexpr uint64_t Magic=0x5254585052473230ULL;
constexpr unsigned Select=107,Info=108,InfoBytes=64,PayloadBytes=4608;
struct Library {P::Library programs{};unsigned abi=0;};
inline bool decode(const uint8_t*wire,size_t n,const uint8_t*code,size_t bytes,Library&out){
 if(n!=512||bytes!=4096||!P::separate(wire,n,&out,sizeof(out))||!P::separate(code,bytes,&out,sizeof(out)))return false;
 Library next;
 if(P::get32(wire+8)==2){if(!P::decode(wire,n,code,bytes,next.programs))return false;next.abi=2;}
 else{
  RtxProgram033::Library old;if(!RtxProgram033::decode(wire,n,code,bytes,old))return false;
  next.abi=1;next.programs.count=old.count;next.programs.usedCodeBytes=old.usedCodeBytes;
  for(unsigned i=0;i<old.count;++i){const auto&a=old.programs[i];auto&b=next.programs.programs[i];
   if(a.localX>RtxProgram033::ElementsPerBuffer)return false;
   b.offset=a.offset;b.bytes=a.bytes;b.registers=a.registers;b.localX=a.localX;b.localY=b.localZ=1;
   b.parameters=a.parameters;b.readMask=a.readMask;b.writeMask=a.writeMask;b.constantBytes=a.constantBytes;
   for(unsigned j=0;j<8;++j)b.bindings[j]=a.bindings[j];
  }
 }
 out=next;return true;
}
inline bool geometry(const Library&l,unsigned program,RTXGeometry164::Size groups,RTXGeometry164::Size threads){
 if(program>=l.programs.count)return false;const auto&p=l.programs.programs[program];RTXGeometry164::Shape shape;
 if(threads.x!=p.localX||threads.y!=p.localY||threads.z!=p.localZ||!RTXGeometry164::dispatch(groups,threads,shape))return false;
 return l.abi==2||(l.abi==1&&groups.y==1&&groups.z==1&&threads.y==1&&threads.z==1&&shape.invocations<=RtxProgram033::ElementsPerBuffer);
}
}
