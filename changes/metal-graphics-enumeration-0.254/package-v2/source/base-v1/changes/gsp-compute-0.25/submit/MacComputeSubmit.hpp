#pragma once
#include "ComputeSubmit.hpp"
#include "../memory/MacComputeMemory.hpp"
struct MacComputeSubmitState {bool claimed=false,notified=false;unsigned phase=0;};
class MacComputeSubmit {
 MacGSPContext &context;MacChannelMemoryMapping &mapping;MacComputeMemory &memory;
 const ExecutionTransactions::Result &execution;
 static bool within(unsigned a,unsigned n,unsigned b,unsigned length){return a>=b&&a<b+length&&n&&n<=length-(a-b);}
public:
 MacComputeSubmitState &state;
 MacComputeSubmit(MacGSPContext &c,MacChannelMemoryMapping &map,MacComputeMemory &m,const ExecutionTransactions::Result &e,MacComputeSubmitState &s)
  :context(c),mapping(map),memory(m),execution(e),state(s){}
 bool ready(){return memory.ready()&&memory.state.claimed&&memory.state.writePhase==18&&memory.state.registerPhase==3&&
   ComputeMemory::ready(memory.state.result,memory.state.storage.liveBytes)&&ComputeSubmit::commandValid(memory.state.storage.command);}
 unsigned long long nowNs(){return memory.nowNs();}
 void delayUs(unsigned us){context.delayUs(us);}
 bool claim(){if(!ready()||state.claimed||state.notified||state.phase)return false;state.claimed=true;return true;}
 bool physicalMode(){return ready()&&state.claimed&&context.read(ChannelMemory::Window)==0;}
 bool readMemory(unsigned address,unsigned char *out,unsigned bytes){
  if(!ready()||!state.claimed||!out||!mapping.span(address,bytes))return false;
  namespace H=HostFence;
  if(within(address,bytes,H::Ring,256)||within(address,bytes,H::Command,4096)||within(address,bytes,H::Fence,4096)||within(address,bytes,H::Get,8))
   return mapping.read(address,out,bytes);
  return memory.readMemory(address,out,bytes);
 }
 bool writeMemory(unsigned address,const unsigned char *data,unsigned bytes){
  if(!ready()||!state.claimed||state.notified||!data||!mapping.span(address,bytes))return false;
  unsigned char expected[8];const unsigned char *canonical=nullptr;
  if(state.phase==0){if(address!=ComputeSubmit::Command||bytes!=32)return false;canonical=memory.state.storage.command;}
  else if(state.phase==1){if(address!=ComputeSubmit::Entry||bytes!=8||!ComputeSubmit::entry(expected))return false;canonical=expected;}
  else if(state.phase==2){if(address!=HostFence::Put||bytes!=4)return false;GSPComputePrep::put32(expected,2);canonical=expected;}
  else return false;
  if(!ExecutionMemory::equal(data,canonical,bytes))return false;
  ++state.phase;return mapping.write(address,data,bytes);
 }
 bool notify(unsigned token){
  unsigned candidate=0;
  if(!ready()||!state.claimed||state.notified||state.phase!=3||!WorkSubmitToken::compose(execution.runlist,ExecutionPlan::HardwareChannelId,execution.rawToken,candidate)||
   token!=candidate||token!=execution.candidate)return false;
  state.notified=true;return context.write(HostFence::Doorbell,token);
 }
};
