#pragma once
#include "RTXLibraryCatalog187.hpp"
#include "NativeOwnedDispatch185.hpp"
#include <array>
#include <algorithm>
#include <vector>
// Command-layer snapshots. Keys identify retained host objects only and are
// never encoded. Native requests contain four verified slot indices, not VAs.
namespace RTXBatch187 {
namespace P=RtxProgram164;namespace Q=P::Q;using Bytes=std::vector<uint8_t>;
constexpr uint64_t Magic=0x525458434d443138ULL,Capacities[]={4097,65537,1048577,4194305};
constexpr size_t Header=512,MaxPayload=4097+65537+1048577+4194305;
struct Input {uint64_t key=0,length=0,offset=0;uint32_t index=0;};
struct Resource {uint64_t key=0,bytes=0,payloadOffset=0;uint32_t slot=0,access=0;};
struct Binding {uint32_t resource=0,index=0;uint64_t offset=0,bytes=0;};
struct Plan {uint64_t generation=0,serial=0,payloadBytes=0;uint32_t program=0,count=0,resources=0;RTXGeometry164::Size groups{},threads{};std::array<Resource,4>resource{};std::array<Binding,8>binding{};};
inline bool plan(const P::Program&p,uint32_t program,const Input*input,size_t count,RTXGeometry164::Size groups,RTXGeometry164::Size threads,uint64_t generation,uint64_t serial,Plan&out){
 if(!input||!generation||!serial||!count||count>8||p.parameters!=count||!P::separate(input,count*sizeof(Input),&out,sizeof(out)))return false;
 RTXGeometry164::Shape geometry;if(threads.x!=p.localX||threads.y!=p.localY||threads.z!=p.localZ||!RTXGeometry164::dispatch(groups,threads,geometry))return false;
 Plan next;next.generation=generation;next.serial=serial;next.program=program;next.count=uint32_t(count);next.groups=groups;next.threads=threads;
 uint32_t parameterMask=0;
 for(unsigned i=0;i<count;++i){if(p.bindings[i]>=32||(i&&p.bindings[i]<=p.bindings[i-1]))return false;const Input*found=nullptr;
  for(unsigned j=0;j<count;++j)if(input[j].index==p.bindings[i]){if(found)return false;found=input+j;}
  if(!found||!found->key||!found->length||found->length>Capacities[3]||found->offset>=found->length||(found->offset&3))return false;
  unsigned resource=0;for(;resource<next.resources;++resource)if(next.resource[resource].key==found->key)break;
  if(resource==next.resources){if(next.resources==4)return false;next.resource[resource].key=found->key;next.resource[resource].bytes=found->length;++next.resources;}
  else if(next.resource[resource].bytes!=found->length)return false;
  const uint32_t bit=1u<<found->index;parameterMask|=bit;next.resource[resource].access|=((p.readMask&bit)?1:0)|((p.writeMask&bit)?2:0);
  next.binding[i]={resource,found->index,found->offset,found->length-found->offset};
 }
 if(!p.writeMask||((p.readMask|p.writeMask)&~parameterMask))return false;
 // Largest resource first; choose the smallest still-free compatible slot.
 unsigned order[]={0,1,2,3};std::stable_sort(order,order+next.resources,[&](unsigned a,unsigned b){return next.resource[a].bytes>next.resource[b].bytes;});unsigned used=0;
 for(unsigned i=0;i<next.resources;++i){auto&r=next.resource[order[i]];unsigned slot=0;while(slot<4&&((used&(1u<<slot))||r.bytes>Capacities[slot]))++slot;if(slot==4)return false;r.slot=slot;used|=1u<<slot;}
 for(unsigned i=0;i<next.resources;++i){auto&r=next.resource[i];r.payloadOffset=next.payloadBytes;if(r.bytes>MaxPayload-next.payloadBytes)return false;next.payloadBytes+=r.bytes;}
 out=next;return true;
}
inline std::array<uint8_t,Header> header(const Plan&p){
 std::array<uint8_t,Header>raw{};Q::put64(raw.data(),Magic);Q::put32(raw.data()+8,187);Q::put32(raw.data()+12,Header);Q::put64(raw.data()+16,p.generation);Q::put64(raw.data()+24,p.serial);Q::put32(raw.data()+32,p.program);Q::put32(raw.data()+36,p.count);Q::put32(raw.data()+40,p.resources);
 const uint64_t dims[]={p.groups.x,p.groups.y,p.groups.z,p.threads.x,p.threads.y,p.threads.z};for(unsigned i=0;i<6;++i)Q::put64(raw.data()+48+i*8,dims[i]);Q::put64(raw.data()+96,p.payloadBytes);Q::put64(raw.data()+104,Header+p.payloadBytes);
 for(unsigned i=0;i<p.resources&&i<4;++i){const auto&r=p.resource[i];auto*q=raw.data()+128+i*32;Q::put32(q,r.slot);Q::put32(q+4,r.access);Q::put64(q+8,r.bytes);Q::put64(q+16,r.payloadOffset);}
 for(unsigned i=0;i<p.count&&i<8;++i){const auto&b=p.binding[i];auto*q=raw.data()+256+i*32;Q::put32(q,b.resource);Q::put32(q+4,b.index);Q::put64(q+8,b.offset);Q::put64(q+16,b.bytes);}
 return raw;
}
inline bool decode(const uint8_t*raw,size_t n,const P::Library&library,uint64_t generation,uint64_t serial,Plan&out){
 if(n<Header||n>Header+MaxPayload||!P::separate(raw,n,&out,sizeof(out))||P::get64(raw)!=Magic||P::get32(raw+8)!=187||P::get32(raw+12)!=Header||P::get64(raw+16)!=generation||P::get64(raw+24)!=serial||!generation||!serial)return false;
 const unsigned program=P::get32(raw+32),count=P::get32(raw+36),resources=P::get32(raw+40);if(program>=library.count||!count||count>8||!resources||resources>4)return false;
 std::array<Input,8>input{};
 for(unsigned i=0;i<count;++i){const auto*b=raw+256+i*32;const unsigned resource=P::get32(b);if(resource>=resources)return false;input[i]={resource+1,P::get64(raw+128+resource*32+8),P::get64(b+8),P::get32(b+4)};}
 Plan checked;if(!plan(library.programs[program],program,input.data(),count,{P::get64(raw+48),P::get64(raw+56),P::get64(raw+64)},{P::get64(raw+72),P::get64(raw+80),P::get64(raw+88)},generation,serial,checked))return false;
 const auto expected=header(checked);if(n!=Header+checked.payloadBytes||std::memcmp(expected.data(),raw,Header))return false;out=checked;return true;
}
inline bool assemble(const Plan&p,const std::array<Bytes,4>&snapshots,Bytes&out){
 if(!p.resources||p.resources>4||!p.count||p.count>8||p.payloadBytes>MaxPayload)return false;
 Bytes next(Header+p.payloadBytes);const auto raw=header(p);std::copy(raw.begin(),raw.end(),next.begin());
 for(unsigned i=0;i<p.resources;++i){const auto&r=p.resource[i];if(snapshots[i].size()!=r.bytes||r.payloadOffset>p.payloadBytes||r.bytes>p.payloadBytes-r.payloadOffset)return false;std::copy(snapshots[i].begin(),snapshots[i].end(),next.begin()+Header+r.payloadOffset);}
 out.swap(next);return true;
}
inline bool readback(const Plan&p,const uint8_t*request,size_t requestBytes,const uint8_t*result,size_t resultBytes){
 if(!request||!result||requestBytes!=Header+p.payloadBytes||resultBytes!=p.payloadBytes)return false;
 for(unsigned i=0;i<p.resources;++i){const auto&r=p.resource[i];if(!(r.access&2)&&std::memcmp(request+Header+r.payloadOffset,result+r.payloadOffset,size_t(r.bytes)))return false;}return true;
}
inline std::array<RTXNativeDispatch185::Binding,8> native(const Plan&p){
 std::array<RTXNativeDispatch185::Binding,8>result{};for(unsigned i=0;i<p.count&&i<8;++i){const auto&b=p.binding[i];result[i]={p.resource[b.resource].slot,b.index,b.offset,b.bytes};}return result;
}
}
