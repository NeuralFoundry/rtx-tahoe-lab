#pragma once
#include "HostFence.hpp"
#include "../runtime/MacExecutionTransactions.hpp"
struct MacHostFenceState {bool claimed=false,notified=false;unsigned phase=0;};
class MacHostFence {
  MacGSPContext &context;MacChannelMemoryMapping &mapping;MacExecutionMemory &memory;
  const MacExecutionQueueState &queue;const ExecutionTransactions::Result &execution;unsigned char *scratch;
public:
  MacHostFenceState &state;
  MacHostFence(MacGSPContext &c,MacChannelMemoryMapping &m,MacExecutionMemory &mem,const MacExecutionQueueState &q,
    const ExecutionTransactions::Result &e,unsigned char *s,MacHostFenceState &st):context(c),mapping(m),memory(mem),queue(q),execution(e),scratch(s),state(st){}
  bool ready(){return scratch&&memory.contextReady(execution.context)&&memory.state.executionPrefixConsumed&&memory.state.executionRepliesConsumed==ExecutionCodec::Steps&&
    queue.claimed&&queue.externalComplete&&queue.externalReplies==ExternalVAS::Steps&&queue.externalDirectoryChecks==2&&
    execution.external.rpc.passed&&!queue.bellPending&&queue.expectedProducer==ExecutionCodec::FinalProducer+1&&execution.rpc.passed&&execution.rpc.failure==GSPComputePrep::None&&
    execution.rpc.completed==ExecutionCodec::Steps&&execution.rpc.sent==ExecutionCodec::Steps&&execution.rpc.txWriter==ExecutionCodec::FinalProducer&&execution.rpc.txReader==ExecutionCodec::FinalProducer&&ExecutionPlan::sessionId(execution.channelId)&&
    context.identity.identity==0x252010de&&context.identity.subsystem==0x104c1043&&context.identity.targetBDF&&context.boot0==0xb76000a1U;}
  unsigned long long nowNs(){return memory.nowNs();}
  void delayUs(unsigned us){context.delayUs(us);}
  bool verifyMappings(){
    if(!ready()||state.claimed)return false;const auto start=nowNs();
    const unsigned bases[]={unsigned(GMMULeaves::OldBase),unsigned(GMMULeaves::NewBase)};
    const unsigned lengths[]={12288,memory.state.contexts.childBytes};
    const unsigned char *expected[]={memory.state.storage.root,memory.state.storage.fullChildren};
    if(lengths[1]<8192||lengths[1]>GMMULeaves::MaxChildBytes||lengths[1]%4096)return false;
    for(unsigned k=0;k<2;++k)for(unsigned off=0;off<lengths[k];off+=4096){
      const auto now=nowNs();if(now<start||now-start>=HostFence::BudgetNs||!ready()||!memory.readMemory(bases[k]+off,scratch,4096))return false;
      if(!ExecutionMemory::equal(scratch,expected[k]+off,4096))return false;
    }
    return ready()&&context.read(ChannelMemory::Window)==0;
  }
  bool claim(){if(!ready()||state.claimed)return false;state.claimed=true;return true;}
  bool readMemory(unsigned address,unsigned char *out,unsigned bytes){
    namespace H=HostFence;
    if(!ready()||!state.claimed||!mapping.span(address,bytes))return false;
    const bool allowed=(address>=H::Ring&&address<H::Ring+256&&bytes<=H::Ring+256-address)||
      (address>=H::Command&&address<H::Command+4096&&bytes<=H::Command+4096-address)||
      (address>=H::Fence&&address<H::Fence+4096&&bytes<=H::Fence+4096-address)||
      (address>=H::Get&&address<=H::Put&&bytes<=H::Put+4-address);
    return allowed&&mapping.read(address,out,bytes);
  }
  bool writeMemory(unsigned address,const unsigned char *data,unsigned bytes){
    namespace H=HostFence;unsigned char expected[20]={};unsigned long long entry=0;
    if(!ready()||!state.claimed||state.notified||!data)return false;
    if(state.phase==0){if(address!=H::Command||bytes!=20||!SubmitCodec::fence(expected,20))return false;}
    else if(state.phase==1){if(address!=H::Ring||bytes!=8||!SubmitCodec::entry(SubmitCodec::CommandVA,20,entry))return false;GMMULeaves::write64(expected,entry);}
    else if(state.phase==2){if(address!=H::Put||bytes!=4)return false;GSPComputePrep::put32(expected,1);}
    else return false;
    if(!ExecutionMemory::equal(data,expected,bytes))return false;
    ++state.phase;return mapping.write(address,data,bytes); // Consume before an uncertain hardware write.
  }
  bool notify(unsigned token){
    unsigned candidate=0;
    if(!ready()||!state.claimed||state.notified||state.phase!=3||!WorkSubmitToken::compose(execution.runlist,ExecutionPlan::HardwareChannelId,execution.rawToken,candidate)||
       token!=candidate||token!=execution.candidate)return false;
    state.notified=true;return context.write(HostFence::Doorbell,token);
  }
};
