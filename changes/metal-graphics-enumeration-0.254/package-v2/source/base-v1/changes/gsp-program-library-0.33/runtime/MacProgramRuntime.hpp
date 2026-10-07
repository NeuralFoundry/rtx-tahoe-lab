#pragma once
#include "ProgramRuntime.hpp"
#include "../queue/MacProgramSubmit.hpp"
#include "../queue/MacProgramCapture.hpp"

// The driver entry preallocates State and every buffer before firmware. All
// accepted calls, including client close, must execute under its serialization.
class MacProgramRuntime {
 MacGSPContext &context;MacChannelMemoryMapping &mapping;GSPExecutionOwner::Owner &owner;
 MacExecutionMemoryState &bootstrap;MacProgramMemoryState &memory;
 const ExecutionTransactions::Result &execution;const HostFence::Result &host;RtxProgramRuntime033::State &state;
 bool held()const{
  const auto &a=state.access;
  return a.generation&&a.client&&context.owned()&&context.command()==6&&mapping.mapped()&&mapping.barBase==context.system.bar1&&
   owner.ledger().owned()&&owner.ledger().requiresPin()&&owner.ledger().generation()==a.generation&&
   context.identity.identity==0x252010de&&context.identity.subsystem==0x104c1043&&context.identity.targetBDF&&context.boot0==0xb76000a1U;
 }
 bool retainedProof()const{
  const auto &m=memory.storage;const auto &b=bootstrap.storage;const auto &fixed=bootstrap.fixed;
  return state.access.prepared&&state.preparation.passed&&held()&&RtxProgramRuntime033::bound(state,m)&&
   bootstrap.leaseOwned&&bootstrap.fixedClaimed&&bootstrap.contextsClaimed&&bootstrap.windowObserved&&bootstrap.executionPrefixConsumed&&
   bootstrap.executionRepliesConsumed==ExecutionCodec::Steps&&fixed.passed&&fixed.failure==ChannelMemory::Failure::None&&
   fixed.windowSaved&&fixed.windowRestored&&fixed.windowBefore==bootstrap.originalWindow&&fixed.windowAfter==bootstrap.originalWindow&&
   bootstrap.contexts.passed&&bootstrap.contexts.failure==ChannelMemory::Failure::None&&memory.leaseOwned&&memory.claimed&&
   memory.writePhase==18&&memory.registerPhase==3&&m.root==b.root&&m.goldenChildren==b.goldenChildren&&m.goldenBytes==b.goldenBytes&&
   m.liveChildren==b.fullChildren&&m.liveBytes==bootstrap.contexts.childBytes&&
   ProgramMemory::ready(memory.result,m.liveBytes)&&ProgramMemory::hostReady(host,execution);
 }
 struct PrepareIO {
  MacProgramRuntime &r;MacProgramMemory &initial;
  uint64_t nowNs(){return r.nowNs();}
  bool ready(uint64_t generation){return &initial.state==&r.memory&&initial.ready()&&r.owner.phase()==GSPExecutionOwner::Phase::BootReturned&&r.owner.ledger().generation()==generation;}
  bool readMemory(unsigned a,uint8_t *out,unsigned n){return initial.readMemory(a,out,n);}
 };
 struct OpenIO {
  MacProgramRuntime &r;
  bool openProof(uint64_t generation){return r.retainedProof()&&r.owner.phase()==GSPExecutionOwner::Phase::BootReturned&&r.owner.ledger().generation()==generation;}
  bool readWindow(unsigned &v){return GSPVirtualRegisters::read(r.context,ChannelMemory::Window,v);}
  bool beginRuntime(uint64_t generation){return r.owner.beginRuntime(generation);}
  bool runtimeProof(uint64_t generation){return r.retainedProof()&&r.owner.phase()==GSPExecutionOwner::Phase::RuntimeReady&&r.owner.ledger().generation()==generation&&r.context.read(ChannelMemory::Window)==r.bootstrap.originalWindow;}
  void retain(){r.owner.fail();}
 };
 struct WindowIO {
  MacProgramRuntime &r;
  bool windowOwned(){return r.retainedProof()&&r.owner.phase()==GSPExecutionOwner::Phase::RuntimeReady;}
  uint64_t nowNs(){return r.nowNs();}
  bool readWindow(unsigned &v){return windowOwned()&&GSPVirtualRegisters::read(r.context,ChannelMemory::Window,v);}
  bool writeWindow(unsigned v){return windowOwned()&&(!v||v==r.bootstrap.originalWindow)&&GSPVirtualRegisters::write(r.context,ChannelMemory::Window,v);}
 };
 struct DispatchIO {
  MacProgramRuntime &r;
  bool acquire(unsigned j,uint64_t caller){WindowIO io{r};return RtxProgramWindow033::acquire(io,r.state.access.session,caller,r.bootstrap.originalWindow,r.state.access.slots[j].window);}
  bool stage(unsigned j){
   MacProgramStage io(r.context,r.mapping,r.owner,r.bootstrap,r.memory,r.execution,r.host,r.state.access,j);return io.stage();
  }
  bool submit(unsigned j){
   MacProgramSubmit io(r.context,r.mapping,r.owner,r.bootstrap,r.memory,r.execution,r.host,r.state.access,r.state.queue,r.state.submission,j);return io.submit();
  }
  bool capture(unsigned j){
   auto &c=r.state.captures[j];MacProgramCapture io(r.context,r.mapping,r.owner,r.bootstrap,r.memory,r.execution,r.host,r.state.access,c,j);
   return io.capture()&&io.ready()&&ProgramCapture::verifyFull(c.result,c.root,c.children,c.device,r.state.submission,r.host,j+1,j+1)&&io.ready();
  }
  bool restore(unsigned j){WindowIO io{r};return RtxProgramWindow033::restore(io,r.state.access.slots[j].window);}
  bool proof(){return r.retainedProof()&&r.owner.phase()==GSPExecutionOwner::Phase::RuntimeReady&&r.context.read(ChannelMemory::Window)==r.bootstrap.originalWindow;}
  void retain(){r.owner.fail();}
 };
public:
 MacProgramRuntime(MacGSPContext &c,MacChannelMemoryMapping &map,GSPExecutionOwner::Owner &o,MacExecutionMemoryState &b,
  MacProgramMemoryState &m,const ExecutionTransactions::Result &e,const HostFence::Result &h,RtxProgramRuntime033::State &s)
  :context(c),mapping(map),owner(o),bootstrap(b),memory(m),execution(e),host(h),state(s){}
 uint64_t nowNs(){uint64_t absolute=0,ns=0;clock_get_uptime(&absolute);absolutetime_to_nanoseconds(absolute,&ns);return ns;}
 // Startup retains responsibility for its original window until open succeeds.
 bool prepare(MacProgramMemory &initial,uint64_t generation,uint64_t client){
  PrepareIO io{*this,initial};return RtxProgramRuntime033::prepare(io,state,memory.storage,memory.result,execution,host,generation,client);
 }
 // Call after executionMemory.restoreWindow(), while the owner is BootReturned.
 bool open(){OpenIO io{*this};return RtxProgramRuntime033::open(io,state,bootstrap.originalWindow);}
 RtxProgramRuntime033::Error submit(uint64_t caller,const uint8_t *wire,size_t bytes){DispatchIO io{*this};return RtxProgramRuntime033::dispatch(io,state,caller,wire,bytes);}
 bool close(uint64_t caller){DispatchIO io{*this};return RtxProgramRuntime033::close(io,state,caller);}
};
