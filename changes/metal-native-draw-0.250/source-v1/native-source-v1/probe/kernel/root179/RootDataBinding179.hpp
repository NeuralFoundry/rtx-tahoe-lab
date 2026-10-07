#pragma once
#include "OwnedBufferSpans165.hpp"
#include "../root170/OwnedRootTransition170.hpp"

namespace RTXRootData179 {
// Native private binding, not a user-supplied acknowledgement or pointer API.
// The root transition journal is decoded again before publishing any handle.
class Binding {
 enum class Phase:uint32_t {Idle,Attempted,Ready,Failed};
 Phase phase=Phase::Idle;
 uint64_t session=0,root=0;
 uint32_t count=0;
 uint64_t wire[64][8]{};
 Binding(const Binding&)=delete;Binding&operator=(const Binding&)=delete;
 bool fail(){phase=Phase::Failed;return false;}
public:
 Binding()=default;
 bool ready()const{return phase==Phase::Ready;}
 template<class Arena,class Owner>bool commit(Arena&arena,Owner&owner,uint64_t generation,
   const GSPComputePrep::Result&prior,const uint8_t*request,const uint8_t*records,
   const RTXRootTransition170::Result&result,uint8_t*scratch,
   const uint8_t preparedDigest[32],const uint8_t observedDigest[32]){
  if(phase!=Phase::Idle)return false;phase=Phase::Attempted;
  if(!generation||!preparedDigest||!observedDigest||!arena.exposedStable()||
     arena.info().session!=generation||!arena.info().buffers||arena.info().buffers>64||
     arena.treeInfo().mappings!=arena.info().totalRows||
     !result.verified||!RTXRootTransition170::verify(arena.treeInfo(),prior,request,records,result,scratch))return fail();
  for(unsigned i=0;i<32;++i)if(preparedDigest[i]!=observedDigest[i])return fail();
  count=arena.info().buffers;session=generation;root=arena.treeInfo().root;
  for(uint32_t i=0;i<count;++i){uint64_t scope=0,handle=0;RTXSpans165::Mapping mapping{};
   if(!arena.preparedSpan(i,scope,mapping)||scope!=session||mapping.allocation!=uint64_t(i)+1||
      mapping.mapping!=uint64_t(i)+1||!owner.admitMapping(mapping,handle)||!handle)return fail();
   RTXSpans165::Mapping observed{};uint32_t refs=0;
   if(!owner.inspect(handle,observed,refs)||refs||observed.allocation!=mapping.allocation||observed.mapping!=mapping.mapping||
      observed.gpuVA!=mapping.gpuVA||observed.logicalBytes!=mapping.logicalBytes||
      observed.mappedBytes!=mapping.mappedBytes||observed.access!=mapping.access)return fail();
   wire[i][0]=session;wire[i][1]=handle;wire[i][2]=mapping.allocation;wire[i][3]=mapping.mapping;
   wire[i][4]=mapping.gpuVA;wire[i][5]=mapping.logicalBytes;wire[i][6]=mapping.mappedBytes;wire[i][7]=mapping.access;
  }
  if(!arena.exposedStable()||arena.treeInfo().root!=root||arena.info().session!=session||arena.info().buffers!=count)return fail();
  phase=Phase::Ready;return true;
 }
private:
 bool capture(uint64_t offset,void*out,uint64_t bytes)const{
  if(!ready()||!out||!bytes||bytes>4096||offset>uint64_t(count)*64||bytes>uint64_t(count)*64-offset)return false;
  auto*p=static_cast<uint8_t*>(out);const auto*source=reinterpret_cast<const uint8_t*>(wire);
  for(uint64_t i=0;i<bytes;++i)p[i]=source[offset+i];return true;
 }public:
 bool captureConfirmed(bool rootConfirmed,uint64_t offset,void*out,uint64_t bytes)const{
  return rootConfirmed&&capture(offset,out,bytes);
 }
};
}
