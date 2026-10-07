#pragma once
#include "MacReusableMemory.hpp"
#include "ReusableRuntime.hpp"
#include "driver/GSPExecutionOwner.hpp"

class MacReusableBackend035 {
 MacGSPContext &context;MacChannelMemoryMapping &mapping;GSPExecutionOwner::Owner &owner;
 MacExecutionMemoryState &bootstrap;MacProgramMemoryState &memory;
 const ExecutionTransactions::Result &execution;const HostFence::Result &host;
 RtxReusableRuntime035::State &state;const uint64_t generation_,client_;
 bool held(uint64_t generation,uint64_t client)const{
  const auto &s=state.storage;const auto &m=memory.storage;
  return generation&&client&&generation==generation_&&client==client_&&generation==state.generation&&client==state.client&&
   context.owned()&&context.command()==6&&mapping.mapped()&&mapping.barBase==context.system.bar1&&
   owner.ledger().owned()&&owner.ledger().requiresPin()&&owner.ledger().generation()==generation&&owner.startMask()==3&&
   context.identity.identity==0x252010de&&context.identity.subsystem==0x104c1043&&context.identity.targetBDF&&context.boot0==0xb76000a1U&&
   s.root==m.root&&s.children==m.children&&s.childBytes==m.liveBytes&&s.library==m.library&&s.code==m.code&&RtxReusableRuntime035::bound(state,s);
 }
 bool proof(uint64_t generation,uint64_t client,bool restored)const{
  const auto &m=memory.storage;const auto &b=bootstrap.storage;const auto &fixed=bootstrap.fixed;
  if(!held(generation,client)||!bootstrap.leaseOwned||!bootstrap.fixedClaimed||!bootstrap.contextsClaimed||!bootstrap.windowObserved||
     !bootstrap.executionPrefixConsumed||bootstrap.executionRepliesConsumed!=ExecutionCodec::Steps||!fixed.passed||
     fixed.failure!=ChannelMemory::Failure::None||!fixed.windowSaved||fixed.windowBefore!=bootstrap.originalWindow||
     !bootstrap.contexts.passed||bootstrap.contexts.failure!=ChannelMemory::Failure::None||!memory.leaseOwned||!memory.claimed||
     memory.writePhase!=18||memory.registerPhase!=3||m.root!=b.root||m.goldenChildren!=b.goldenChildren||m.goldenBytes!=b.goldenBytes||
     m.liveChildren!=b.fullChildren||m.liveBytes!=bootstrap.contexts.childBytes||
     !ProgramMemory::ready(memory.result,m.liveBytes)||!ProgramMemory::hostReady(host,execution))return false;
  return restored?(fixed.windowRestored&&fixed.windowAfter==bootstrap.originalWindow):(!fixed.windowRestored&&bootstrap.physicalMode);
 }
 bool phaseProof()const{
  return owner.phase()==GSPExecutionOwner::Phase::BootReturned?proof(generation_,client_,false):
   owner.phase()==GSPExecutionOwner::Phase::RuntimeReady&&proof(generation_,client_,true);
 }
public:
 MacReusableBackend035(MacGSPContext &c,MacChannelMemoryMapping &map,GSPExecutionOwner::Owner &o,MacExecutionMemoryState &b,
  MacProgramMemoryState &m,const ExecutionTransactions::Result &e,const HostFence::Result &h,RtxReusableRuntime035::State &s,
  uint64_t generation,uint64_t client):context(c),mapping(map),owner(o),bootstrap(b),memory(m),execution(e),host(h),state(s),generation_(generation),client_(client){}
 uint64_t nowNs(){uint64_t absolute=0,ns=0;clock_get_uptime(&absolute);absolutetime_to_nanoseconds(absolute,&ns);return ns;}
 bool bootReady(uint64_t generation,uint64_t client,bool restored){
  return owner.phase()==GSPExecutionOwner::Phase::BootReturned&&proof(generation,client,restored);
 }
 bool runtimeReady(uint64_t generation,uint64_t client){return owner.phase()==GSPExecutionOwner::Phase::RuntimeReady&&proof(generation,client,true);}
 unsigned originalWindow()const{return bootstrap.originalWindow;}
 bool beginRuntime(uint64_t generation){return bootReady(generation,client_,true)&&owner.beginRuntime(generation);}
 bool readWindow(unsigned &v){
  return held(generation_,client_)&&(owner.phase()==GSPExecutionOwner::Phase::BootReturned||owner.phase()==GSPExecutionOwner::Phase::RuntimeReady)&&
   GSPVirtualRegisters::read(context,ChannelMemory::Window,v);
 }
 bool writeWindow(unsigned v){
  return runtimeReady(generation_,client_)&&(!v||v==bootstrap.originalWindow)&&GSPVirtualRegisters::write(context,ChannelMemory::Window,v);
 }
 bool readMemory(unsigned address,uint8_t *out,unsigned bytes){
  return phaseProof()&&context.read(ChannelMemory::Window)==0&&RtxReusableRuntime035::readable(state,address,bytes)&&
   mapping.span(address,bytes)&&mapping.read(address,out,bytes);
 }
 bool writeMemory(unsigned address,const uint8_t *data,unsigned bytes){
  return runtimeReady(generation_,client_)&&state.core.phase()==RtxReusable035::Phase::Exposed&&state.window.acquired&&!state.window.restoreAttempted&&
   state.backing.phase()==RtxReusableBacking035::Phase::Claimed&&state.backing.writes()>=1&&state.backing.writes()<=7&&
   context.read(ChannelMemory::Window)==0&&RtxReusableRuntime035::readable(state,address,bytes)&&mapping.span(address,bytes)&&mapping.write(address,data,bytes);
 }
 bool notify(){
  unsigned token=0;
  return runtimeReady(generation_,client_)&&state.core.phase()==RtxReusable035::Phase::Exposed&&state.window.acquired&&!state.window.restoreAttempted&&
   state.backing.phase()==RtxReusableBacking035::Phase::Published&&state.backing.writes()==7&&context.read(ChannelMemory::Window)==0&&
   WorkSubmitToken::compose(execution.runlist,ExecutionPlan::HardwareChannelId,execution.rawToken,token)&&token==execution.candidate&&
   context.write(HostFence::Doorbell,token);
 }
 void delayUs(unsigned us){context.delayUs(us);}
 void retain(){owner.fail();}
};
using MacReusableRuntime035=RtxReusableRuntime035::Runtime<MacReusableBackend035>;
