#pragma once
#include "../transactions/ExecutionCodec.hpp"
#include "../../../driver/GSPVirtualRegisters.hpp"
#include "ExecutionMemory.hpp"
#include "../../gsp-channel-0.23/memory/MacChannelMemory.hpp"
#include "../../gsp-channel-0.23/native/ChannelSnapshot.hpp"

struct MacExecutionMemoryState {
  bool leaseOwned=false,fixedClaimed=false,contextsClaimed=false,windowObserved=false,physicalMode=false;
  bool executionPrefixConsumed=false;unsigned executionRepliesConsumed=0;
  unsigned originalWindow=0;
  ExecutionMemory::Storage storage;
  ExecutionPlan::Plan plan;
  ExecutionMemory::Result fixed,contexts;
};

// Reuses the BAR1 map allocated before firmware exposure. All new storage is
// caller-owned and must also be allocated before exposure and retained to reboot.
// Wire this after the successful 0.23 snapshot/window cleanup but before the
// coordinator transitions out of BootReturned. This is not an entry point.
class MacExecutionMemory {
  MacGSPContext &context;MacChannelMemoryMapping &mapping;MacChannelMemory &goldenMemory;
  const ChannelTransactions::Result &goldenRpc;const ChannelSnapshot::Result &snapshot;
  GSPExecutionOwner::Owner &owner;const unsigned long long generation;
  static bool within(unsigned address,unsigned bytes,unsigned base,unsigned length){return address>=base&&address<base+length&&bytes<=base+length-address;}
  bool privateBacking(unsigned address,unsigned bytes)const{
    if(!state.contextsClaimed||!ExecutionPlan::valid(state.plan,goldenRpc.context))return false;
    for(const auto &b:state.plan.buffers)if(within(address,bytes,unsigned(b.physical),unsigned(b.allocated)))return true;return false;
  }
  static bool sameGolden(const ChannelCodec::Plan &a,const ChannelCodec::Plan &b){
    if(!ChannelCodec::planValid(a)||!ChannelCodec::planValid(b))return false;
    for(unsigned i=0;i<9;++i){const auto &x=a.buffers[i],&y=b.buffers[i];
      if(x.id!=y.id||x.kind!=y.kind||x.bytes!=y.bytes||x.allocated!=y.allocated||x.alignment!=y.alignment||x.physical!=y.physical||
        x.virtualAddress!=y.virtualAddress||x.usePhysical!=y.usePhysical||x.useVirtual!=y.useVirtual)return false;
    }return true;
  }
public:
  MacExecutionMemoryState &state;
  MacExecutionMemory(MacGSPContext &c,MacChannelMemoryMapping &map,MacChannelMemory &gm,const ChannelTransactions::Result &rpc,
    const ChannelSnapshot::Result &capture,GSPExecutionOwner::Owner &o,unsigned long long gen,MacExecutionMemoryState &s)
    :context(c),mapping(map),goldenMemory(gm),goldenRpc(rpc),snapshot(capture),owner(o),generation(gen),state(s){}
  bool ready(){
    const auto &g=goldenMemory.state;const auto &r=goldenRpc.rpc;const auto &s=state.storage;
    return state.leaseOwned&&s.root&&s.goldenChildren&&s.rootScratch&&s.expectedScratch&&s.fixedChildren&&s.fullChildren&&s.scratch&&
      mapping.mapped()&&goldenMemory.ready()&&context.owned()&&context.command()==6&&owner.ledger().generation()==generation&&owner.ledger().requiresPin()&&
      owner.phase()==GSPExecutionOwner::Phase::BootReturned&&r.passed&&r.failure==GSPComputePrep::None&&r.completed==5&&r.sent==5&&r.txWriter==14&&r.txReader==14&&
      goldenRpc.channelId==3&&goldenRpc.subdeviceMask<=1&&goldenRpc.contextPrepared&&goldenRpc.contextPreparationAttempted&&
      g.ring.passed&&g.ring.windowRestored&&g.contexts.passed&&g.contexts.invalidation.passed&&g.contexts.verifiedBackingBytes==goldenRpc.context.backingBytes&&
      snapshot.passed&&!snapshot.failure&&snapshot.rootBytes==12288&&snapshot.childBytes==g.contexts.childBytes&&snapshot.requested==snapshot.childBytes&&
      s.goldenBytes==snapshot.childBytes&&ExecutionPlan::goldenFits(goldenRpc.context)&&sameGolden(goldenRpc.context,g.plan);
  }
  unsigned long long nowNs(){uint64_t absolute=0,ns=0;clock_get_uptime(&absolute);absolutetime_to_nanoseconds(absolute,&ns);return ns;}
  bool claimFixed(){if(!ready()||state.fixedClaimed||state.contextsClaimed||!state.executionPrefixConsumed||state.executionRepliesConsumed)return false;
    state.fixedClaimed=true;return true;}
  bool claimContexts(const ExecutionPlan::Plan &p){
    if(!fixedReady()||state.contextsClaimed||!state.executionPrefixConsumed||state.executionRepliesConsumed!=ExecutionCodec::PhysicalStep||!ExecutionPlan::valid(p,goldenRpc.context))return false;
    state.plan=p;state.contextsClaimed=true;return true;
  }
  bool readMemory(unsigned address,unsigned char *out,unsigned bytes){
    if(!ready()||!out||!state.fixedClaimed||!state.physicalMode||!mapping.span(address,bytes))return false;
    return (within(address,bytes,unsigned(GMMULeaves::OldBase),12288)||within(address,bytes,unsigned(GMMULeaves::NewBase),unsigned(GMMULeaves::MaxChildBytes))||
      within(address,bytes,unsigned(ExecutionPlan::Base),ExecutionMemory::FixedBytes)||privateBacking(address,bytes))&&mapping.read(address,out,bytes);
  }
  bool writeMemory(unsigned address,const unsigned char *data,unsigned bytes){
    namespace P=ExecutionPlan;namespace L=GMMULeaves;
    if(!ready()||!data||!state.fixedClaimed||!state.physicalMode||!mapping.span(address,bytes))return false;
    bool zero=false;const unsigned char *expected=nullptr;
    if(!state.contextsClaimed){
      if(state.fixed.passed||state.executionRepliesConsumed)return false;
      if(within(address,bytes,unsigned(P::Base),ExecutionMemory::FixedBytes)){
        if(state.fixed.backingVerified||state.executionRepliesConsumed)return false;zero=true;
      }else if(bytes==4&&address>=L::NewBase+4096+8&&address<L::NewBase+4096+32&&state.fixed.backingVerified&&state.fixed.parentAttempted)
        expected=state.storage.fixedChildren+address-unsigned(L::NewBase);
      else return false;
    }else{
      if(state.contexts.passed||state.executionRepliesConsumed!=ExecutionCodec::PhysicalStep)return false;
      if(privateBacking(address,bytes)){if(state.contexts.backingVerified||state.executionRepliesConsumed!=ExecutionCodec::PhysicalStep)return false;zero=true;}
      else if(state.contexts.backingVerified&&state.contexts.childBytes>=state.storage.goldenBytes&&
        within(address,bytes,unsigned(L::NewBase)+state.storage.goldenBytes,state.contexts.childBytes-state.storage.goldenBytes))
        expected=state.storage.fullChildren+address-unsigned(L::NewBase);
      else if(state.contexts.backingVerified&&state.contexts.parentAttempted&&bytes==4&&within(address,4,unsigned(L::NewBase),4096)){
        const unsigned group=(address-unsigned(L::NewBase))/16;const auto *old=state.storage.fixedChildren+group*16;
        if(L::read64(old)||L::read64(old+8))return false;expected=state.storage.fullChildren+address-unsigned(L::NewBase);
      }else return false;
    }
    if(zero){for(unsigned i=0;i<bytes;++i)if(data[i])return false;}
    else if(!expected||!ExecutionMemory::equal(data,expected,bytes))return false;
    return mapping.write(address,data,bytes);
  }
  bool readRegister(unsigned address,unsigned &value){
    if(!ready()||!state.fixedClaimed)return false;
    if(address!=ChannelMemory::Window&&(!state.physicalMode||(address!=GMMUInvalidate::PDBRegister&&address!=GMMUInvalidate::UpperRegister&&address!=GMMUInvalidate::CommandRegister)))return false;
    if(!GSPVirtualRegisters::read(context,address,value))return false;
    if(address==ChannelMemory::Window){
      if(!state.windowObserved&&!GMMUInvalidate::unreadable(value)){state.originalWindow=value;state.windowObserved=true;}
      state.physicalMode=value==0;
    }return true;
  }
  bool writeRegister(unsigned address,unsigned value){
    if(!ready()||!state.fixedClaimed)return false;
    if(address==ChannelMemory::Window){if(!state.windowObserved||(value!=0&&value!=state.originalWindow))return false;state.physicalMode=false;}
    else{const auto &r=state.contextsClaimed?state.contexts:state.fixed;
      if(r.passed||state.executionRepliesConsumed!=(state.contextsClaimed?ExecutionCodec::PhysicalStep:0U)||!state.physicalMode||!r.backingVerified||!r.childrenVerified||!r.parentAttempted||
        !((address==GMMUInvalidate::PDBRegister&&value==GMMUInvalidate::PDBValue)||(address==GMMUInvalidate::UpperRegister&&value==0)||
          (address==GMMUInvalidate::CommandRegister&&value==GMMUInvalidate::CommandValue)))return false;
    }
    return GSPVirtualRegisters::write(context,address,value);
  }
  bool fixedReady(){return ready()&&state.physicalMode&&ExecutionMemory::fixedReady(state.fixed,state.storage.goldenBytes);}
  bool contextReady(const ExecutionPlan::Plan &p){
    const auto &r=state.contexts;
    if(!fixedReady()||!state.contextsClaimed||!r.passed||r.failure!=ChannelMemory::Failure::None||!r.backingVerified||
      r.verifiedBackingBytes!=p.backingBytes||!r.childrenVerified||r.verifiedChildBytes!=r.childBytes||!r.invalidation.passed||
      !ExecutionPlan::valid(p,goldenRpc.context)||!ExecutionPlan::valid(state.plan,goldenRpc.context))return false;
    for(unsigned i=0;i<3;++i){const auto &a=p.buffers[i],&b=state.plan.buffers[i];
      if(a.id!=b.id||a.kind!=b.kind||a.bytes!=b.bytes||a.allocated!=b.allocated||a.alignment!=b.alignment||a.physical!=b.physical||a.va!=b.va)return false;
    }return true;
  }
  bool stageFixed(){return ExecutionMemory::fixed(*this,goldenRpc.context,goldenRpc.channelId,state.storage,state.fixed)&&fixedReady();}
  bool stageContexts(const ExecutionPlan::Plan &p){return ExecutionMemory::contexts(*this,goldenRpc.context,p,state.storage,state.fixed,state.contexts)&&contextReady(p);}
  void restoreWindow(){ChannelMemory::restoreWindow(*this,state.fixed);}
};
