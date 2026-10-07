#pragma once
#include "changes/gsp-program-library-0.33/memory/ProgramMemory.hpp"
#include "ReusableBacking.hpp"
// New initial image is finished before the first mapping/backing mutation.
// Storage, HOST proof, ordered PTE publication and invalidation are unchanged.
namespace RtxReusableMemory035 {
namespace M=ChannelMemory;namespace L=GMMULeaves;namespace H=HostFence;
using Storage=ProgramMemory::Storage;using Result=ProgramMemory::Result;
using ProgramMemory::storage;using ProgramMemory::hostReady;
constexpr unsigned Base=ProgramMemory::Base,Bytes=ProgramMemory::Bytes,PteOffset=ProgramMemory::PteOffset;
inline bool tables35(const Storage &s,const ChannelCodec::Plan &golden,const ExecutionPlan::Plan &execution){
 return ProgramMemory::tables(s,golden,execution)&&RtxReusableBacking035::image(s.library,s.code,s.image,Bytes);
}
template<class IO>bool stage(IO &io,const ChannelCodec::Plan &golden,const ExecutionTransactions::Result &execution,const H::Result &host,
 const Storage &s,Result &r){
 if(r.claimed||r.memory.attempted||r.memory.failure!=M::Failure::None)return false;
 if(!storage(s,golden,execution,host,r))return false;
 auto &m=r.memory;m.start=io.nowNs();
 if(!hostReady(host,execution)){M::fail(m,M::Failure::Owner);return false;}r.hostVerified=true;
 if(!tables35(s,golden,execution.context)){M::fail(m,M::Failure::Plan);return false;}r.imagesPrepared=true;
 if(!io.ready()){M::fail(m,M::Failure::Owner);return false;}
 if(!io.claim()){M::fail(m,M::Failure::Replay);return false;}r.claimed=true;m.attempted=true;m.childBytes=s.liveBytes;
 unsigned window=~0U;if(!M::regRead(io,m,M::Window,window))return false;
 if(window){M::fail(m,M::Failure::WindowState);return false;}
 // The execution owner retains its original window and performs cleanup.
 m.windowBefore=0;m.windowSaved=true;
 if(!M::compare(io,m,unsigned(L::OldBase),s.root,unsigned(L::OldBytes),s.scratch)||
    !M::compare(io,m,unsigned(L::NewBase),s.liveChildren,s.liveBytes,s.scratch)||!M::inspect(io,m,Base,Bytes,s.scratch))return false;
 for(unsigned off=0;off<Bytes;off+=4096){
  if(!M::write(io,m,Base+off,s.image+off,4096)||!M::compare(io,m,Base+off,s.image+off,4096,s.scratch))return false;
  m.verifiedBackingBytes+=4096;
 }
 m.backingVerified=true;
 if(!M::compare(io,m,unsigned(L::OldBase),s.root,unsigned(L::OldBytes),s.scratch)||
    !M::compare(io,m,unsigned(L::NewBase),s.liveChildren,s.liveBytes,s.scratch))return false;
 for(unsigned i=0;i<6;++i){const unsigned offset=PteOffset+i*8,address=unsigned(L::NewBase)+offset;
  if(!M::read(io,m,address,s.scratch,8))return false;
  if(L::read64(s.scratch)){M::fail(m,M::Failure::Changed);return false;}
  m.parentAttempted=true;
  if(!M::write(io,m,address+4,s.children+offset+4,4)||!M::write(io,m,address,s.children+offset,4)||
     !M::compare(io,m,address,s.children+offset,8,s.scratch))return false;
  ++m.linksPublished;
 }
 if(!M::compare(io,m,unsigned(L::OldBase),s.root,unsigned(L::OldBytes),s.scratch)||
    !M::compare(io,m,unsigned(L::NewBase),s.children,s.liveBytes,s.scratch))return false;
 m.childrenVerified=true;m.verifiedChildBytes=s.liveBytes;
 if(!M::invalidate(io,m))return false;m.passed=true;return true;
}
}
