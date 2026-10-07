#pragma once
#include "../../gsp-submit-0.24/fence/HostFence.hpp"
#include "../../gsp-submit-0.24/memory/ExecutionMemory.hpp"
#include "../BatchProfile.hpp"
#include "../reference/ShaderImage.hpp"

// Six fixed pages after the execution backing, before its private contexts.
// Caller supplies preallocated buffers. No new allocation occurs after GSP.
namespace BatchMemory {
namespace M=ChannelMemory;namespace E=ExecutionMemory;namespace P=ExecutionPlan;
namespace T=ExecutionTables;namespace L=GMMULeaves;namespace Q=QmdProfile;namespace H=HostFence;
constexpr unsigned Base=Q::ProgramPhysical,Bytes=6*4096,CommandBytes=32;
constexpr unsigned PteOffset=4096+4*8,PteBytes=6*8;
static_assert(Base==P::FixedEnd&&Base+Bytes<P::ContextBase,"Owned physical gap");
static_assert(Q::ConstantPhysical==Base+8192&&Q::QmdPhysical==Base+12288&&Q::OutputPhysical==Base+16384&&Q::FencePhysical==Base+20480,"Six adjacent pages");
static_assert(Q::ProgramVA==L::VABase+4*4096&&Q::FenceVA==L::VABase+9*4096,"PT0 slots4..9");
struct Storage {
 const unsigned char *root=nullptr,*goldenChildren=nullptr,*liveChildren=nullptr;
 unsigned goldenBytes=0,liveBytes=0;
 unsigned char *rootScratch=nullptr,*expectedScratch=nullptr,*children=nullptr,*image=nullptr,*command=nullptr,*scratch=nullptr;
};
struct Result {M::Result memory;bool hostVerified=false,imagesPrepared=false,claimed=false;};
inline bool image(unsigned char *out,unsigned bytes,unsigned char *command,unsigned commandBytes){
 if(bytes!=Bytes||commandBytes!=32||!Q::separate(out,bytes,command,commandBytes))return false;
 unsigned char commands[128],entries[32];
 if(!RtxBatch031::build(out,bytes,commands,128,entries,32,RtxVectorShader030::code,
  sizeof(RtxVectorShader030::code),RtxBatch031::DefaultSeeds,RtxBatch031::DefaultCounts))return false;
 // Existing staging storage reserves one command; later commands are rebuilt
 // from the immutable per-job layout immediately before their queue publication.
 for(unsigned i=0;i<32;++i)command[i]=commands[i];return true;
}
inline bool hostReady(const H::Result &host,const ExecutionTransactions::Result &execution){
 const auto &x=execution.external;
 if(!x.rpc.passed||x.rpc.failure||x.rpc.sent!=ExternalVAS::Steps||x.rpc.completed!=ExternalVAS::Steps||x.directoryChecks!=2||!x.directoryAcknowledged||!x.vas.accepted||
    x.rpc.txWriter!=ExternalVAS::FinalProducer||x.rpc.txReader!=ExternalVAS::FinalProducer||execution.rpc.initialReader!=x.rpc.rxReader||execution.rpc.initialSequence!=x.rpc.rxSequence)return false;
 if(!host.passed||host.failure!=H::None||!host.claimed||!host.commandAttempted||!host.entryAttempted||!host.putAttempted||!host.bellAttempted||
   host.writes!=3||host.initialGet||host.initialPut||host.initialFence||host.lastGet!=1||host.lastPut!=1||host.lastFence!=SubmitCodec::FenceValue||
   host.token!=execution.candidate||!execution.rpc.passed||execution.rpc.failure||execution.rpc.completed!=ExecutionCodec::Steps||execution.rpc.sent!=ExecutionCodec::Steps||
   execution.rpc.txWriter!=ExecutionCodec::FinalProducer||execution.rpc.txReader!=ExecutionCodec::FinalProducer||!P::sessionId(execution.channelId)||!execution.contextPrepared||!execution.fixedPrepared)return false;
 unsigned candidate=0;
 if(!WorkSubmitToken::compose(execution.runlist,P::HardwareChannelId,execution.rawToken,candidate)||candidate!=execution.candidate)return false;
 unsigned char command[20],entry[8];unsigned long long value=0;
 if(!SubmitCodec::fence(command,20)||!SubmitCodec::entry(SubmitCodec::CommandVA,20,value))return false;
 L::write64(entry,value);return E::equal(command,host.command,20)&&E::equal(entry,host.entry,8);
}
inline bool storage(const Storage &s,const ChannelCodec::Plan &golden,const ExecutionTransactions::Result &execution,const H::Result &host,Result &r){
 const void *inputs[]={s.root,s.goldenChildren,s.liveChildren,&s,&golden,&execution,&host};
 const size_t inBytes[]={L::OldBytes,s.goldenBytes,s.liveBytes,sizeof(s),sizeof(golden),sizeof(execution),sizeof(host)};
 const void *outputs[]={s.rootScratch,s.expectedScratch,s.children,s.image,s.command,s.scratch,&r};
 const size_t outBytes[]={L::OldBytes,L::MaxChildBytes,L::MaxChildBytes,Bytes,32,4096,sizeof(r)};
 // Validate the result before reset, even when it aliases a captured input.
 for(unsigned i=0;i<7;++i)if(inputs[i]&&!P::disjoint(inputs[i],inBytes[i],&r,sizeof(r)))return false;
 for(unsigned i=0;i<6;++i)if(outputs[i]&&!P::disjoint(outputs[i],outBytes[i],&r,sizeof(r)))return false;
 r={};
 if(s.goldenBytes<8192||s.goldenBytes>L::MaxChildBytes||s.goldenBytes%4096||s.liveBytes<s.goldenBytes||s.liveBytes>L::MaxChildBytes||s.liveBytes%4096)return false;
 for(unsigned i=0;i<7;++i)for(unsigned j=0;j<7;++j)if(!P::disjoint(outputs[i],outBytes[i],inputs[j],inBytes[j]))return false;
 for(unsigned i=0;i<7;++i)for(unsigned j=0;j<i;++j)if(!P::disjoint(outputs[i],outBytes[i],outputs[j],outBytes[j]))return false;
 return P::disjoint(s.root,L::OldBytes,s.goldenChildren,s.goldenBytes)&&P::disjoint(s.root,L::OldBytes,s.liveChildren,s.liveBytes)&&
   P::disjoint(s.goldenChildren,s.goldenBytes,s.liveChildren,s.liveBytes);
}
inline bool tables(const Storage &s,const ChannelCodec::Plan &golden,const P::Plan &execution){
 T::Result result;
 if(!T::merge(s.root,unsigned(L::OldBytes),s.goldenChildren,s.goldenBytes,golden,execution,s.rootScratch,s.expectedScratch,s.children,
   unsigned(L::MaxChildBytes),result)||result.childBytes!=s.liveBytes||!E::equal(s.children,s.liveChildren,s.liveBytes))return false;
 if(L::read64(s.children)!=0x20||L::read64(s.children+8)!=((L::NewBase+4096)>>4|2))return false;
 for(unsigned i=0;i<6;++i)if(L::read64(s.children+PteOffset+i*8))return false;
 for(unsigned i=0;i<6;++i)L::write64(s.children+PteOffset+i*8,(L::Kind<<56)|((Base+i*4096)>>4)|1);
 return image(s.image,Bytes,s.command,32);
}
template<class IO>bool execute(IO &io,const ChannelCodec::Plan &golden,const ExecutionTransactions::Result &execution,const H::Result &host,
 const Storage &s,Result &r){
 if(!storage(s,golden,execution,host,r))return false;
 auto &m=r.memory;m.start=io.nowNs();
 if(!hostReady(host,execution)){M::fail(m,M::Failure::Owner);return false;}r.hostVerified=true;
 if(!tables(s,golden,execution.context)){M::fail(m,M::Failure::Plan);return false;}r.imagesPrepared=true;
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
inline bool ready(const Result &r,unsigned bytes){
 const auto &m=r.memory;return r.claimed&&r.hostVerified&&r.imagesPrepared&&m.passed&&m.failure==M::Failure::None&&m.attempted&&m.modified&&
  m.backingVerified&&m.verifiedBackingBytes==Bytes&&m.inspectedBytes==Bytes&&m.zeroedBytes==0&&m.childrenVerified&&m.parentAttempted&&
  m.linksPublished==6&&m.childBytes==bytes&&m.verifiedChildBytes==bytes&&m.invalidation.passed&&m.invalidation.completed&&m.invalidation.commandAttempted;
}
}
