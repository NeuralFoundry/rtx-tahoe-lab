#pragma once
#include "BatchQueueGate.hpp"
#include "../memory/MacBatchMemory.hpp"
using MacBatchSubmitState=BatchQueueGate::State;
class MacBatchSubmit {
 MacGSPContext &context;MacChannelMemoryMapping &mapping;MacBatchMemory &memory;
 const ExecutionTransactions::Result &execution;unsigned job;
 static bool within(unsigned a,unsigned n,unsigned b,unsigned length){return a>=b&&a<b+length&&n&&n<=length-(a-b);}
public:
 MacBatchSubmitState &state;
 MacBatchSubmit(MacGSPContext &c,MacChannelMemoryMapping &map,MacBatchMemory &m,const ExecutionTransactions::Result &e,MacBatchSubmitState &s,unsigned j)
  :context(c),mapping(map),memory(m),execution(e),job(j),state(s){}
 bool ready(){return BatchQueueGate::ready(state,job)&&memory.ready()&&memory.state.claimed&&memory.state.writePhase==18&&memory.state.registerPhase==3&&
  BatchMemory::ready(memory.state.result,memory.state.storage.liveBytes)&&BatchSubmit::imageProfile(memory.state.storage);}
 unsigned long long nowNs(){return memory.nowNs();}
 void delayUs(unsigned us){context.delayUs(us);}
 bool claim(unsigned j){return j==job&&ready()&&BatchQueueGate::claim(state,job);}
 bool physicalMode(){return ready()&&state.jobs[job].claimed&&context.read(ChannelMemory::Window)==0;}
 bool readMemory(unsigned address,unsigned char *out,unsigned bytes){
  if(!ready()||!state.jobs[job].claimed||!out||!mapping.span(address,bytes))return false;
  namespace H=HostFence;
  if(within(address,bytes,H::Ring,256)||within(address,bytes,H::Command,4096)||within(address,bytes,H::Fence,4096)||within(address,bytes,H::Get,8))return mapping.read(address,out,bytes);
  return memory.readMemory(address,out,bytes);
 }
 bool writeMemory(unsigned address,const unsigned char *data,unsigned bytes){
  if(!ready()||!mapping.span(address,bytes)||!BatchQueueGate::write(state,job,address,data,bytes))return false;
  return mapping.write(address,data,bytes);
 }
 bool notify(unsigned token){
  unsigned candidate=0;
  if(!ready()||!WorkSubmitToken::compose(execution.runlist,ExecutionPlan::HardwareChannelId,execution.rawToken,candidate)||
   token!=candidate||token!=execution.candidate||!BatchQueueGate::notify(state,job))return false;
  return context.write(HostFence::Doorbell,token);
 }
 bool finish(const BatchSubmit::Result &r){return ready()&&r.token==execution.candidate&&BatchQueueGate::finish(state,job,r);}
};
