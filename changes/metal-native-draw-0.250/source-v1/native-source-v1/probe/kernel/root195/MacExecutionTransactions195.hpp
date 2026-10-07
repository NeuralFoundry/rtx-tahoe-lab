#pragma once
#include "ExecutionTransactions195.hpp"
#include "../changes/gsp-submit-0.24/runtime/MacExecutionTransactions.hpp"
template<class Root>class MacExecutionTransactions195 {
  Root&ownedRoot;MacGSPContext &context;MacGSPRuntimeDma &dma;MacExecutionMemory &memory;const ChannelTransactions::Result &golden;
  bool exclude(unsigned long long start,unsigned long long end){
    if(end<start||end-start>=ChannelMemory::BudgetNs||state.excludedStagingNs>~0ULL-(end-start))return false;
    state.excludedStagingNs+=end-start;return true;
  }
public:
  MacExecutionQueueState &state;
  MacExecutionTransactions195(Root&root,MacGSPContext &c,MacGSPRuntimeDma &d,MacExecutionMemory &m,const ChannelTransactions::Result &g,MacExecutionQueueState &s)
    :ownedRoot(root),context(c),dma(d),memory(m),golden(g),state(s){}
  bool ready(){return state.scratch&&memory.ready()&&dma.ready()&&dma.directlyMapped();}
  bool ringReady(){return ready()&&memory.fixedReady();}
  bool claim(){
    if(!ready()||state.claimed||state.externalAttempted||state.externalComplete||memory.state.executionPrefixConsumed||memory.state.executionRepliesConsumed||state.expectedProducer!=ExternalVAS::FirstSequence+1||state.bellPending)return false;
    state.claimed=true;memory.state.executionPrefixConsumed=true;return true;
  }
  unsigned long long nowNs(){const auto actual=memory.nowNs();return actual>=state.excludedStagingNs?actual-state.excludedStagingNs:0;}
  void delayUs(unsigned us){context.delayUs(us);}
  bool prepareFixed(){
    if(!ready()||!state.claimed||state.externalAttempted||state.expectedProducer!=ExternalVAS::FirstSequence+1||state.bellPending||memory.state.executionRepliesConsumed)return false;
    const auto start=memory.nowNs();const bool passed=memory.stageFixed()&&ownedRoot.prepare(memory,context,dma);const auto end=memory.nowNs();return passed&&exclude(start,end);
  }
  bool externalClaim(){
    if(!ringReady()||!state.claimed||state.externalAttempted||state.externalComplete||state.externalReplies||state.externalDirectoryChecks||state.bellPending||
       state.expectedProducer!=ExternalVAS::FirstSequence+1||memory.state.executionRepliesConsumed)return false;
    state.externalAttempted=true;return true;
  }
  bool externalDirectory(){
    if(!ringReady()||!state.externalAttempted||state.externalComplete||state.bellPending||memory.state.executionRepliesConsumed||
       state.externalDirectoryChecks>1||state.externalReplies!=(state.externalDirectoryChecks?ExternalVAS::Steps:0U))return false;
    const auto &s=memory.state.storage;
    for(unsigned which=0;which<2;++which){
      const unsigned count=which?s.goldenBytes:12288,base=which?unsigned(GMMULeaves::NewBase):unsigned(ExternalVAS::RootPhysical);
      const unsigned char *expected=which?s.fixedChildren:s.root;
      if(!expected||count<8192||count>GMMULeaves::MaxChildBytes||count%4096)return false;
      for(unsigned off=0;off<count;off+=4096){
        if(!memory.readMemory(base+off,state.scratch,4096))return false;
        for(unsigned i=0;i<4096;++i)if(state.scratch[i]!=expected[off+i])return false;
      }
    }
    if(!ownedRoot.initialStable())return false;++state.externalDirectoryChecks;return true;
  }
  bool externalConsumed(unsigned step){
    if(!ringReady()||!state.externalAttempted||state.externalComplete||step>=ExternalVAS::Steps||state.externalReplies!=step||state.externalDirectoryChecks!=1||
       state.expectedProducer!=ExternalVAS::FirstSequence+2+step||state.bellPending||memory.state.executionRepliesConsumed)return false;
    ++state.externalReplies;return true;
  }
  bool externalFinish(){
    if(!ringReady()||!state.externalAttempted||state.externalComplete||state.externalReplies!=ExternalVAS::Steps||state.externalDirectoryChecks!=2||
       state.expectedProducer!=ExternalVAS::FinalProducer+1||state.bellPending||memory.state.executionRepliesConsumed)return false;
    state.externalComplete=true;return true;
  }
  bool prepareContext(const ExecutionPlan::Plan &p){
    if(!ringReady()||!state.claimed||!state.externalComplete||state.expectedProducer!=ExecutionCodec::FirstSequence+1+ExecutionCodec::PhysicalStep||state.bellPending||memory.state.executionRepliesConsumed!=ExecutionCodec::PhysicalStep)return false;
    const auto start=memory.nowNs();const bool passed=memory.stageContexts(p)&&ownedRoot.appendContexts(memory,context,dma,p);const auto end=memory.nowNs();return passed&&exclude(start,end);
  }
  ExternalVAS::Directory externalRoot()const{return ownedRoot.directory();}
  bool retainRootJournal(const GSPComputePrep::Result&prior,const uint8_t*requests,const uint8_t*records,const ExternalSetup::Result&result){
    return !state.externalAttempted&&ownedRoot.retainJournal(prior,requests,records,result);
  }
  bool confirmRoot(const GSPComputePrep::Result&prior,const uint8_t*requests,const uint8_t*records,
                   const ExternalSetup::Result&result,uint8_t*scratch){
    return state.externalComplete&&ownedRoot.confirmInitial(prior,requests,records,result,scratch);
  }
  bool contextReady(const ExecutionPlan::Plan &p){return ringReady()&&state.externalComplete&&memory.contextReady(p);}
  bool replyConsumed(unsigned step){
    if(!ringReady()||!state.claimed||!state.externalComplete||step>=ExecutionCodec::Steps||state.bellPending||memory.state.executionRepliesConsumed!=step||state.expectedProducer!=ExecutionCodec::FirstSequence+2+step)return false;
    ++memory.state.executionRepliesConsumed;return true;
  }
  bool import(){return ready()&&dma.synchronizeOne(GSPDmaProtocol::Queues,1);}
  bool publish(){return ringReady()&&state.claimed&&state.externalAttempted&&dma.synchronizeOne(GSPDmaProtocol::Queues,2);}
  bool read(unsigned off,unsigned char *out,unsigned bytes){return ready()&&out&&bytes&&bytes<=4096&&off<=0x81000&&bytes<=0x81000-off&&dma.copyOut(GSPDmaProtocol::Queues,off,out,bytes);}
  bool write(unsigned off,const unsigned char *data,unsigned bytes){
    if(!ringReady()||!state.claimed||!state.externalAttempted||!data||bytes<4)return false;
    const unsigned first=state.externalComplete?ExecutionCodec::FirstSequence+1:ExternalVAS::FirstSequence+1;
    const unsigned final=state.externalComplete?ExecutionCodec::FinalProducer:ExternalVAS::FinalProducer;
    const unsigned consumed=state.externalComplete?memory.state.executionRepliesConsumed:state.externalReplies;
    if(state.expectedProducer<first||state.expectedProducer>final+1)return false;
    const unsigned value=GSPComputePrep::get32(data);
    if(off==0x1020)return bytes==4&&value<63&&dma.copyIn(GSPDmaProtocol::Queues,off,data,bytes);
    if(off==0x1010){
      if(bytes!=4||state.expectedProducer>final||value!=state.expectedProducer||state.bellPending||consumed!=state.expectedProducer-first)return false;
      if(!dma.copyIn(GSPDmaProtocol::Queues,off,data,bytes))return false;++state.expectedProducer;state.bellPending=true;return true;
    }
    if(state.expectedProducer>final||state.bellPending||off!=0x2000+(state.expectedProducer-1)*4096||bytes!=4096||consumed!=state.expectedProducer-first)return false;
    const unsigned step=state.expectedProducer-first;
    if(state.externalComplete){
      if(step>=ExecutionCodec::PhysicalStep&&!memory.contextReady(memory.state.plan))return false;
      if(!ExecutionCodec::request(step,ExecutionCodec::FirstSequence+step,golden.context,golden.channelId,memory.state.plan,state.scratch,4096))return false;
    }else if(state.externalDirectoryChecks!=1||!ExternalVAS::request(step,state.scratch,4096,ownedRoot.directory()))return false;
    for(unsigned i=0;i<4096;++i)if(data[i]!=state.scratch[i])return false;
    return dma.copyIn(GSPDmaProtocol::Queues,off,data,bytes);
  }
  bool doorbell(){
    if(!ringReady()||!state.claimed||!state.externalAttempted||!state.bellPending)return false;
    state.bellPending=false;return context.write(GSPComputePrep::Doorbell,0);
  }
};
