#pragma once
#include "ProgramQueueGate.hpp"
#include "../stage/MacProgramStage.hpp"

// Composes the already checked stage owner with independent persistent queue
// latches. No IOUserClient entry, allocation, reset or release is performed here.
class MacProgramSubmit {
 MacGSPContext &context;MacChannelMemoryMapping &mapping;MacProgramMemoryState &memory;
 const ExecutionTransactions::Result &execution;const HostFence::Result &host;
 RtxProgramAccess033::State &stageState;ProgramQueueGate::State &queue;
 const ProgramSubmit::Storage &storage;unsigned slot;MacProgramStage prior;
 bool bindings()const{
  if(storage.memory!=&memory.storage||storage.history!=stageState.history||storage.proofPlan!=&stageState.proofPlan||
     storage.scratch!=stageState.proofScratch||storage.scratchBytes!=sizeof(stageState.proofScratch)||
     storage.captureBytes!=RtxProgramImage033::ImageBytes)return false;
  for(unsigned i=0;i<4;++i)if(storage.plans[i]!=&stageState.slots[i].stage.plan)return false;
  return true;
 }
 bool readable(unsigned address,unsigned bytes)const{
  if(!bytes||bytes>256||(address&3)||(bytes&3))return false;
  const struct Span{unsigned base,bytes;} spans[]={{unsigned(GMMULeaves::OldBase),12288},{unsigned(GMMULeaves::NewBase),memory.storage.liveBytes},
   {ProgramMemory::Base,ProgramMemory::Bytes},{HostFence::Ring,4096},{HostFence::Command,4096},{HostFence::Fence,4096}};
  for(const auto &s:spans)if(address>=s.base&&address-s.base<s.bytes&&bytes<=s.bytes-(address-s.base))return true;
  return false;
 }
public:
 MacProgramSubmit(MacGSPContext &c,MacChannelMemoryMapping &map,GSPExecutionOwner::Owner &o,MacExecutionMemoryState &b,
  MacProgramMemoryState &m,const ExecutionTransactions::Result &e,const HostFence::Result &h,RtxProgramAccess033::State &s,
  ProgramQueueGate::State &q,const ProgramSubmit::Storage &store,unsigned j)
  :context(c),mapping(map),memory(m),execution(e),host(h),stageState(s),queue(q),storage(store),slot(j),prior(c,map,o,b,m,e,h,s,j){}
 bool ready(){return bindings()&&prior.ready()&&RtxProgramAccess033::staged(stageState,slot)&&ProgramQueueGate::ready(queue,slot);}
 uint64_t nowNs(){return prior.nowNs();}
 void delayUs(unsigned us){context.delayUs(us);}
 bool claim(unsigned j){return j==slot&&ready()&&ProgramQueueGate::claim(queue,j,stageState.slots[j].stage.plan);}
 bool physicalMode(){return ready()&&queue.jobs[slot].claimed;}
 bool readMemory(unsigned address,uint8_t *out,unsigned bytes){
  return ready()&&queue.jobs[slot].claimed&&out&&readable(address,bytes)&&mapping.span(address,bytes)&&mapping.read(address,out,bytes);
 }
 bool writeMemory(unsigned address,const uint8_t *data,unsigned bytes){
  return ready()&&mapping.span(address,bytes)&&ProgramQueueGate::write(queue,slot,address,data,bytes)&&mapping.write(address,data,bytes);
 }
 bool notify(unsigned token){
  unsigned candidate=0;
  return ready()&&WorkSubmitToken::compose(execution.runlist,ExecutionPlan::HardwareChannelId,execution.rawToken,candidate)&&
   token==candidate&&token==execution.candidate&&ProgramQueueGate::notify(queue,slot)&&context.write(HostFence::Doorbell,token);
 }
 bool submit(){return ready()&&ProgramSubmit::execute(*this,slot,storage,memory.result,execution,host,queue.jobs[slot].result);}
};
