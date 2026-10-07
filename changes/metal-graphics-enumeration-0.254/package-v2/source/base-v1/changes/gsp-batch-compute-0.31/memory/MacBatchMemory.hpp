#pragma once
#include "BatchMemory.hpp"
#include "../../gsp-submit-0.24/fence/MacHostFence.hpp"

struct MacBatchMemoryState {
 bool leaseOwned=false,claimed=false;unsigned writePhase=0,registerPhase=0;
 BatchMemory::Storage storage;BatchMemory::Result result;
};
// No entry point yet. Bind to the same execution owner before its final phase
// transition; allocate storage before firmware and retain all resources to reboot.
class MacBatchMemory {
 MacGSPContext &context;MacChannelMemoryMapping &mapping;MacExecutionMemory &memory;
 const MacExecutionQueueState &queue;const ExecutionTransactions::Result &execution;
 const MacHostFenceState &fenceState;const HostFence::Result &fence;
 static bool within(unsigned a,unsigned n,unsigned b,unsigned length){return a>=b&&a<b+length&&n&&n<=length-(a-b);}
public:
 MacBatchMemoryState &state;
 MacBatchMemory(MacGSPContext &c,MacChannelMemoryMapping &map,MacExecutionMemory &mem,const MacExecutionQueueState &q,
  const ExecutionTransactions::Result &e,const MacHostFenceState &f,const HostFence::Result &h,MacBatchMemoryState &s)
  :context(c),mapping(map),memory(mem),queue(q),execution(e),fenceState(f),fence(h),state(s){}
 bool ready(){
  const auto &s=state.storage;const auto &old=memory.state.storage;
  return state.leaseOwned&&memory.contextReady(execution.context)&&memory.state.physicalMode&&memory.state.executionPrefixConsumed&&
   memory.state.executionRepliesConsumed==ExecutionCodec::Steps&&queue.claimed&&queue.externalComplete&&queue.externalReplies==ExternalVAS::Steps&&queue.externalDirectoryChecks==2&&
   execution.external.rpc.passed&&!queue.bellPending&&queue.expectedProducer==ExecutionCodec::FinalProducer+1&&
   fenceState.claimed&&fenceState.notified&&fenceState.phase==3&&BatchMemory::hostReady(fence,execution)&&mapping.mapped()&&
   s.root==old.root&&s.goldenChildren==old.goldenChildren&&s.goldenBytes==old.goldenBytes&&s.liveChildren==old.fullChildren&&
   s.liveBytes==memory.state.contexts.childBytes&&s.image&&s.children&&s.command&&s.scratch&&s.rootScratch&&s.expectedScratch&&
   context.identity.identity==0x252010de&&context.identity.subsystem==0x104c1043&&context.identity.targetBDF&&context.boot0==0xb76000a1U;
 }
 unsigned long long nowNs(){return memory.nowNs();}
 bool claim(){if(!ready()||state.claimed||state.writePhase||state.registerPhase||!state.result.imagesPrepared)return false;state.claimed=true;return true;}
 bool readMemory(unsigned address,unsigned char *out,unsigned bytes){
  if(!ready()||!state.claimed||!mapping.span(address,bytes))return false;
  return (within(address,bytes,unsigned(GMMULeaves::OldBase),12288)||within(address,bytes,unsigned(GMMULeaves::NewBase),state.storage.liveBytes)||
    within(address,bytes,BatchMemory::Base,BatchMemory::Bytes))&&mapping.read(address,out,bytes);
 }
 bool writeMemory(unsigned address,const unsigned char *data,unsigned bytes){
  namespace C=BatchMemory;
  if(!ready()||!state.claimed||!data||state.result.memory.passed||!mapping.span(address,bytes))return false;
  const unsigned phase=state.writePhase;const unsigned char *expected=nullptr;
  if(phase<6){
   if(address!=C::Base+phase*4096||bytes!=4096||state.result.memory.backingVerified)return false;
   expected=state.storage.image+phase*4096;
  }else if(phase<18){
   const unsigned offset=C::PteOffset+((phase-6)/2)*8+((phase%2)==0?4:0);
   if(address!=GMMULeaves::NewBase+offset||bytes!=4||!state.result.memory.backingVerified||!state.result.memory.parentAttempted)return false;
   expected=state.storage.children+offset;
  }else return false;
  if(!ExecutionMemory::equal(data,expected,bytes))return false;
  ++state.writePhase;return mapping.write(address,data,bytes); // Consume before uncertain write.
 }
 bool readRegister(unsigned address,unsigned &value){
  if(!ready()||!state.claimed)return false;
  if(address!=ChannelMemory::Window&&address!=GMMUInvalidate::CommandRegister&&address!=GMMUInvalidate::PDBRegister&&address!=GMMUInvalidate::UpperRegister)return false;
  return GSPVirtualRegisters::read(context,address,value);
 }
 bool writeRegister(unsigned address,unsigned value){
  const unsigned addresses[]={GMMUInvalidate::PDBRegister,GMMUInvalidate::UpperRegister,GMMUInvalidate::CommandRegister};
  const unsigned values[]={GMMUInvalidate::PDBValue,0,GMMUInvalidate::CommandValue};
  const auto &r=state.result.memory;
  if(!ready()||!state.claimed||state.writePhase!=18||state.registerPhase>=3||r.passed||!r.childrenVerified||!r.backingVerified||
     r.linksPublished!=6||address!=addresses[state.registerPhase]||value!=values[state.registerPhase])return false;
  ++state.registerPhase;return GSPVirtualRegisters::write(context,address,value);
 }
 bool stage(const ChannelCodec::Plan &golden){return BatchMemory::execute(*this,golden,execution,fence,state.storage,state.result)&&BatchMemory::ready(state.result,state.storage.liveBytes);}
};
