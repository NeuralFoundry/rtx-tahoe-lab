#pragma once
#include "ProgramAccess.hpp"
#include "../memory/MacProgramMemory.hpp"
#include "../../../driver/GSPExecutionOwner.hpp"

// No driver entry yet. The enclosing runtime must acquire/restore each request's
// window, serialize the owning client, and retain all exposed storage on failure.
class MacProgramStage {
 MacGSPContext &context;MacChannelMemoryMapping &mapping;GSPExecutionOwner::Owner &owner;
 MacExecutionMemoryState &bootstrap;MacProgramMemoryState &memory;
 const ExecutionTransactions::Result &execution;const HostFence::Result &host;
 RtxProgramAccess033::State &state;unsigned slot;
 bool held()const{
  return state.generation&&state.client&&context.owned()&&context.command()==6&&mapping.mapped()&&
   mapping.barBase==context.system.bar1&&owner.ledger().owned()&&owner.ledger().requiresPin()&&owner.ledger().generation()==state.generation&&
   context.identity.identity==0x252010de&&context.identity.subsystem==0x104c1043&&context.identity.targetBDF&&context.boot0==0xb76000a1U;
 }
 bool retainedProof()const{
  const auto &s=memory.storage;const auto &b=bootstrap.storage;const auto &fixed=bootstrap.fixed;const auto &m=state.storage;
  return held()&&bootstrap.leaseOwned&&bootstrap.fixedClaimed&&bootstrap.contextsClaimed&&bootstrap.windowObserved&&
   bootstrap.executionPrefixConsumed&&bootstrap.executionRepliesConsumed==ExecutionCodec::Steps&&
   fixed.passed&&fixed.failure==ChannelMemory::Failure::None&&fixed.windowSaved&&fixed.windowRestored&&
   fixed.windowBefore==bootstrap.originalWindow&&fixed.windowAfter==bootstrap.originalWindow&&
   bootstrap.contexts.passed&&bootstrap.contexts.failure==ChannelMemory::Failure::None&&
   memory.leaseOwned&&memory.claimed&&memory.writePhase==18&&memory.registerPhase==3&&
   s.root==b.root&&s.goldenChildren==b.goldenChildren&&s.goldenBytes==b.goldenBytes&&s.liveChildren==b.fullChildren&&
   s.liveBytes==bootstrap.contexts.childBytes&&s.library==m.library&&s.code==m.code&&s.image==m.canonical&&
   ProgramMemory::ready(memory.result,s.liveBytes)&&ProgramMemory::hostReady(host,execution);
 }
public:
 MacProgramStage(MacGSPContext &c,MacChannelMemoryMapping &map,GSPExecutionOwner::Owner &o,MacExecutionMemoryState &b,
  MacProgramMemoryState &m,const ExecutionTransactions::Result &e,const HostFence::Result &h,RtxProgramAccess033::State &s,unsigned j)
  :context(c),mapping(map),owner(o),bootstrap(b),memory(m),execution(e),host(h),state(s),slot(j){}
 bool ready(){
  return retainedProof()&&owner.phase()==GSPExecutionOwner::Phase::RuntimeReady&&RtxProgramAccess033::accepted(state,slot)&&
   context.read(ChannelMemory::Window)==0;
 }
 uint64_t nowNs(){uint64_t absolute=0,ns=0;clock_get_uptime(&absolute);absolutetime_to_nanoseconds(absolute,&ns);return ns;}
 bool claim(unsigned j,uint64_t generation){return j==slot&&ready()&&RtxProgramAccess033::claimStage(state,j,generation);}
 bool readMemory(unsigned address,uint8_t *out,unsigned bytes){
  return ready()&&out&&state.slots[slot].stageClaimed&&RtxProgramAccess033::readable(slot,address,bytes)&&
   mapping.span(address,bytes)&&mapping.read(address,out,bytes);
 }
 bool writeMemory(unsigned address,const uint8_t *data,unsigned bytes){
  return ready()&&mapping.span(address,bytes)&&RtxProgramAccess033::stageWrite(state,slot,address,data,bytes)&&mapping.write(address,data,bytes);
 }
 bool stage(){
  return ready()&&RtxProgramStage033::execute(*this,state.session.active(),state.storage,state.slots[slot].stage)&&
   RtxProgramAccess033::staged(state,slot);
 }
};
