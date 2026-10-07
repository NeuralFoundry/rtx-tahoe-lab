#pragma once
#include "ProgramLibrary.hpp"
namespace RtxProgramRequest033 {
namespace P=RtxProgram033;
constexpr unsigned WireBytes=2112,DataBytes=P::MaxBindings*P::BufferBytes;
constexpr uint64_t Magic=UINT64_C(0x5254585245513333);
struct Request {uint64_t generation=0,id=0;unsigned program=0,groups=0;uint8_t data[DataBytes]={};};
inline bool valid(const Request &r,const P::Library &lib){
 if(!lib.count||lib.count>P::MaxPrograms||!r.generation||!r.id||r.id>P::Slots||r.program>=lib.count||!r.groups||r.groups>P::ElementsPerBuffer)return false;
 const auto &p=lib.programs[r.program];
 if(!p.parameters||p.parameters>P::MaxBindings||!p.localX||p.localX>P::ElementsPerBuffer||r.groups>P::ElementsPerBuffer/p.localX)return false;
 return P::zero(r.data,p.parameters*P::BufferBytes,DataBytes);
}
inline bool encode(const Request &r,const P::Library &lib,uint8_t *out,size_t bytes){
 if(bytes!=WireBytes||!P::separate(&r,sizeof(r),out,bytes)||!P::separate(&lib,sizeof(lib),out,bytes)||!valid(r,lib))return false;
 for(unsigned i=0;i<WireBytes;++i)out[i]=0;
 P::Q::put64(out,Magic);P::Q::put32(out+8,1);P::Q::put32(out+12,WireBytes);
 P::Q::put64(out+16,r.generation);P::Q::put64(out+24,r.id);P::Q::put32(out+32,r.program);P::Q::put32(out+36,r.groups);
 for(unsigned i=0;i<DataBytes;++i)out[64+i]=r.data[i];return true;
}
inline bool decode(const uint8_t *wire,size_t bytes,const P::Library &lib,Request &out){
 if(!lib.count||lib.count>P::MaxPrograms||bytes!=WireBytes||!P::separate(wire,bytes,&out,sizeof(out))||!P::separate(&lib,sizeof(lib),&out,sizeof(out))||
    P::get64(wire)!=Magic||P::get32(wire+8)!=1||P::get32(wire+12)!=WireBytes||!P::zero(wire,40,64))return false;
 const auto generation=P::get64(wire+16),id=P::get64(wire+24);const auto program=P::get32(wire+32),groups=P::get32(wire+36);
 if(!generation||!id||id>P::Slots||program>=lib.count||!groups||groups>P::ElementsPerBuffer)return false;
 const auto &p=lib.programs[program];
 if(!p.parameters||p.parameters>P::MaxBindings||!p.localX||p.localX>P::ElementsPerBuffer||groups>P::ElementsPerBuffer/p.localX||
    !P::zero(wire,64+p.parameters*P::BufferBytes,WireBytes))return false;
 // The service supplies a private snapshot. Validate before changing the active
 // record, without a second 2KiB stack-local Request in the kernel call chain.
 out.generation=generation;out.id=id;out.program=program;out.groups=groups;
 for(unsigned i=0;i<DataBytes;++i)out.data[i]=wire[64+i];return true;
}
}
