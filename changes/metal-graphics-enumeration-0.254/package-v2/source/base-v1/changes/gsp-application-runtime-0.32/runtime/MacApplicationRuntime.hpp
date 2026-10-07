#pragma once
#include "RuntimeDispatch.hpp"
#include "../memory/MacApplicationMemory.hpp"
#include "../entry/ApplicationCapture.hpp"

// Per-client runtime backend. The service must serialize all calls, preallocate
// this state/storage before firmware, and retain them with the provider to reset.
// This header alone does not add IOUserClient selectors or a loadable driver.
class MacApplicationRuntime {
 MacGSPContext &context;MacChannelMemoryMapping &mapping;GSPExecutionOwner::Owner &owner;
 MacExecutionMemoryState &bootstrap;MacApplicationMemoryState &memory;
 const ExecutionTransactions::Result &execution;const HostFence::Result &host;
 unsigned char *capture;const unsigned captureBytes;
 ApplicationCapture::Storage *fullCapture;
 RtxRuntimeAccess032::State &state;
 bool held()const{
  return state.generation&&state.client&&context.owned()&&context.command()==6&&mapping.mapped()&&
   mapping.barBase==context.system.bar1&&owner.ledger().owned()&&owner.ledger().requiresPin()&&owner.ledger().generation()==state.generation&&
   context.identity.identity==0x252010de&&context.identity.subsystem==0x104c1043&&context.identity.targetBDF&&context.boot0==0xb76000a1U;
 }
 bool retainedProof()const{
  const auto &s=memory.storage;const auto &b=bootstrap.storage;const auto &fixed=bootstrap.fixed;
  return state.prepared&&held()&&bootstrap.leaseOwned&&bootstrap.fixedClaimed&&bootstrap.contextsClaimed&&bootstrap.windowObserved&&
   bootstrap.executionPrefixConsumed&&bootstrap.executionRepliesConsumed==ExecutionCodec::Steps&&
   fixed.passed&&fixed.failure==ChannelMemory::Failure::None&&fixed.windowSaved&&fixed.windowRestored&&
   fixed.windowBefore==bootstrap.originalWindow&&fixed.windowAfter==bootstrap.originalWindow&&
   bootstrap.contexts.passed&&bootstrap.contexts.failure==ChannelMemory::Failure::None&&
   memory.leaseOwned&&memory.claimed&&memory.writePhase==18&&memory.registerPhase==3&&
   s.requests==state.requests&&s.root==b.root&&s.goldenChildren==b.goldenChildren&&s.goldenBytes==b.goldenBytes&&
   s.liveChildren==b.fullChildren&&s.liveBytes==bootstrap.contexts.childBytes&&
   ApplicationMemory::ready(memory.result,s.liveBytes)&&ApplicationMemory::hostReady(host,execution);
 }
 bool ready(unsigned j){
  return retainedProof()&&owner.phase()==GSPExecutionOwner::Phase::RuntimeReady&&RtxRuntimeAccess032::accepted(state,j)&&
   context.read(ChannelMemory::Window)==0;
 }
 void retire(){state.session.ownershipLost();owner.fail();}
 struct WindowIO {
  MacApplicationRuntime &r;
  bool windowOwned(){return r.retainedProof()&&r.owner.phase()==GSPExecutionOwner::Phase::RuntimeReady;}
  unsigned long long nowNs(){return r.nowNs();}
  bool readWindow(unsigned &v){if(!windowOwned())return false;return GSPVirtualRegisters::read(r.context,ChannelMemory::Window,v);}
  bool writeWindow(unsigned v){
   if(!windowOwned()||(v&&v!=r.bootstrap.originalWindow))return false;
   return GSPVirtualRegisters::write(r.context,ChannelMemory::Window,v);
  }
 };
 struct StageIO {
  MacApplicationRuntime &r;unsigned j;
  bool ready(){return r.ready(j);}
  unsigned long long nowNs(){return r.nowNs();}
  bool claim(unsigned slot,uint64_t gen){return slot==j&&ready()&&RtxRuntimeAccess032::claimStage(r.state,j,gen);}
  bool readMemory(unsigned a,unsigned char *p,unsigned n){return r.read(j,a,p,n);}
  bool writeMemory(unsigned a,const unsigned char *p,unsigned n){
   return ready()&&r.mapping.span(a,n)&&RtxRuntimeAccess032::stageWrite(r.state,j,a,p,n)&&r.mapping.write(a,p,n);
  }
 };
 struct SubmitIO {
  MacApplicationRuntime &r;unsigned j;
  bool ready(){return r.ready(j)&&RtxRuntimeAccess032::staged(r.state,j);}
  unsigned long long nowNs(){return r.nowNs();}
  void delayUs(unsigned us){r.context.delayUs(us);}
  bool claim(unsigned slot){return slot==j&&ready()&&RtxRuntimeAccess032::claimSubmit(r.state,j);}
  bool physicalMode(){return ready()&&r.state.slots[j].submitClaimed;}
  bool readMemory(unsigned a,unsigned char *p,unsigned n){return r.state.slots[j].submitClaimed&&r.read(j,a,p,n);}
  bool writeMemory(unsigned a,const unsigned char *p,unsigned n){
   return ready()&&r.mapping.span(a,n)&&ApplicationQueueGate::write(r.state.queue,j,a,p,n)&&r.mapping.write(a,p,n);
  }
  bool notify(unsigned token){
   unsigned candidate=0;
   return ready()&&WorkSubmitToken::compose(r.execution.runlist,ExecutionPlan::HardwareChannelId,r.execution.rawToken,candidate)&&
    token==candidate&&token==r.execution.candidate&&ApplicationQueueGate::notify(r.state.queue,j)&&r.context.write(HostFence::Doorbell,token);
  }
 };
 bool read(unsigned j,unsigned address,unsigned char *out,unsigned bytes){
  return ready(j)&&state.slots[j].stageClaimed&&out&&RtxRuntimeAccess032::readable(address,bytes,memory.storage.liveBytes)&&
   mapping.span(address,bytes)&&mapping.read(address,out,bytes);
 }
 bool captureJob(unsigned j){
  if(fullCapture){
   struct Reader {
    MacApplicationRuntime &r;unsigned j;
    bool ready(){return r.ready(j);}
    unsigned long long nowNs(){return r.nowNs();}
    bool readMemory(unsigned a,unsigned char *p,unsigned n){return r.read(j,a,p,n);}
   } reader{*this,j};
   auto &c=*fullCapture;
   if(c.result.attempted||!c.device||capture!=c.device+12288||captureBytes!=ApplicationMemory::Bytes||
    !ApplicationCapture::capture(reader,memory.storage.liveBytes,c.root,c.children,c.device,c.result))return false;
   return ready(j)&&ApplicationCapture::verifyFull(c.result,c.root,c.children,c.device,memory.storage,host,j+1)&&ready(j);
  }
  const auto start=nowNs();uint64_t previous=0;
  for(unsigned at=0;at<captureBytes;at+=256){
   const auto now=nowNs();if(now<start||now-start<previous||now-start>=RtxRuntimeWindow032::BudgetNs)return false;previous=now-start;
   if(!read(j,ApplicationMemory::Base+at,capture+at,256))return false;
  }
  return ready(j)&&RtxRuntimeImage032::capture(capture,captureBytes,memory.storage.image,ApplicationMemory::Bytes,j+1);
 }
 struct DispatchIO {
  MacApplicationRuntime &r;
  bool acquire(unsigned j,uint64_t caller){WindowIO io{r};return RtxRuntimeWindow032::acquire(io,r.state.session,caller,r.bootstrap.originalWindow,r.state.slots[j].window);}
  bool stage(unsigned j){StageIO io{r,j};return RtxRuntimeStage032::execute(io,r.state.session.active(),r.memory.storage.image,ApplicationMemory::Bytes,r.state.slots[j].stage);}
  bool submit(unsigned j){SubmitIO io{r,j};return ApplicationSubmit::execute(io,j,r.memory.storage,r.memory.result,r.execution,r.host,r.state.slots[j].submit);}
  bool capture(unsigned j){return r.captureJob(j);}
  bool restore(unsigned j){WindowIO io{r};return RtxRuntimeWindow032::restore(io,r.state.slots[j].window);}
  bool proof(){return r.retainedProof()&&r.owner.phase()==GSPExecutionOwner::Phase::RuntimeReady;}
  void retain(){r.owner.fail();}
 };
public:
 MacApplicationRuntime(MacGSPContext &c,MacChannelMemoryMapping &map,GSPExecutionOwner::Owner &o,MacExecutionMemoryState &b,
  MacApplicationMemoryState &m,const ExecutionTransactions::Result &e,const HostFence::Result &h,
  unsigned char *raw,unsigned rawBytes,RtxRuntimeAccess032::State &s,ApplicationCapture::Storage *full=nullptr)
  :context(c),mapping(map),owner(o),bootstrap(b),memory(m),execution(e),host(h),capture(raw),captureBytes(rawBytes),fullCapture(full),state(s){}
 unsigned long long nowNs(){uint64_t absolute=0,ns=0;clock_get_uptime(&absolute);absolutetime_to_nanoseconds(absolute,&ns);return ns;}
 // Called while the original startup adapters still possess their valid window.
 // The capture is obtained here through real BAR1 reads, never a supplied flag.
 bool prepare(MacApplicationMemory &initial,uint64_t generation,uint64_t client){
  namespace A=RtxApplication032;
  if(state.prepared||state.opened||state.session.phase()!=A::Phase::Empty||&initial.state!=&memory||!generation||!client||
   captureBytes!=ApplicationMemory::Bytes||!A::separate(capture,captureBytes,memory.storage.image,ApplicationMemory::Bytes)||
   !A::separate(capture,captureBytes,&state,sizeof(state))||memory.storage.requests!=state.requests||
   !initial.ready()||!ApplicationMemory::ready(memory.result,memory.storage.liveBytes)||!ApplicationSubmit::imageProfile(memory.storage)||
   owner.phase()!=GSPExecutionOwner::Phase::BootReturned||owner.ledger().generation()!=generation)return false;
  for(unsigned j=0;j<4;++j)if(state.requests[j].generation!=generation||state.requests[j].requestId!=j+1||state.requests[j].count)return false;
  const auto start=nowNs();uint64_t previous=0;
  for(unsigned at=0;at<captureBytes;at+=256){
   const auto now=nowNs();if(now<start||now-start<previous||now-start>=RtxRuntimeWindow032::BudgetNs)return false;previous=now-start;
   if(!initial.readMemory(ApplicationMemory::Base+at,capture+at,256))return false;
  }
  if(!initial.ready()||!RtxRuntimeImage032::capture(capture,captureBytes,memory.storage.image,ApplicationMemory::Bytes,0))return false;
  state.generation=generation;state.client=client;state.prepared=true;return true;
 }
 // Call only after the existing executionMemory.restoreWindow() completes.
 // The old fixed result stays windowRestored=true for the rest of this boot.
 bool open(){
  if(state.opened||!retainedProof()||owner.phase()!=GSPExecutionOwner::Phase::BootReturned||
   RtxRuntimeWindow032::bad(bootstrap.originalWindow)||context.read(ChannelMemory::Window)!=bootstrap.originalWindow)return false;
  RtxApplication032::BootstrapEvidence e;
  e.generation=state.generation;e.client=state.client;
  e.ownerHeld=e.firmwareReady=e.hostQueueVerified=e.storageReady=e.windowRestored=true;
  if(!state.session.open(e)||!owner.beginRuntime(state.generation)){retire();return false;}
  state.opened=true;return true;
 }
 RtxApplication032::Error submit(uint64_t caller,const unsigned char *bytes,size_t size){
  DispatchIO io{*this};return RtxRuntimeDispatch032::execute(io,state,caller,bytes,size);
 }
 void close(uint64_t caller){if(state.session.close(caller))owner.fail();}
};
