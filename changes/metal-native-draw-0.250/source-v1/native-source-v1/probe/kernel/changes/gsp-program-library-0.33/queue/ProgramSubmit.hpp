#pragma once
#include "../memory/ProgramMemory.hpp"
#include "../stage/ProgramAccess.hpp"

// Program-aware bounded publication. Arithmetic belongs to the application;
// this layer proves queue/fence progress and allowed memory modifications.
namespace ProgramSubmit {
namespace C=ProgramMemory;namespace H=HostFence;namespace L=GMMULeaves;namespace E=ExecutionPlan;
namespace P=RtxProgram033;namespace R=RtxProgramRequest033;namespace I=RtxProgramImage033;namespace X=RtxProgramAccess033;
constexpr unsigned MaxOperations=65536;
constexpr uint64_t BudgetNs=UINT64_C(5000000000);
constexpr unsigned commandPhysical(unsigned j){return H::Command+64+j*64;}
constexpr unsigned entryPhysical(unsigned j){return H::Ring+8+j*8;}
enum Failure:unsigned{None,Arguments,Proof,Owner,Replay,Clock,Timeout,Read,Write,Initial,Changed,Notify,Unexpected};
struct Result {
 Failure failure=None;bool attempted=false,claimed=false,commandAttempted=false,entryAttempted=false,putAttempted=false,bellAttempted=false;
 bool immutableVerified=false,guardsVerified=false,stable=false,passed=false;
 unsigned job=0,operations=0,reads=0,writes=0,polls=0,token=0,lastAddress=0;
 unsigned initialGet=~0U,initialPut=~0U,initialCompletion=~0U,get=~0U,put=~0U,hostFence=~0U,completion=~0U;
 uint64_t started=0,elapsed=0;uint8_t command[32]={},entry[8]={};
};
struct Storage {
 const C::Storage *memory=nullptr;const R::Request *history=nullptr;const I::Plan *plans[P::Slots]={};
 uint8_t *capture=nullptr,*scratch=nullptr;I::Plan *proofPlan=nullptr;unsigned captureBytes=0,scratchBytes=0;
};
inline bool fail(Result &r,Failure f){if(r.failure==None)r.failure=f;r.passed=false;return false;}
inline bool storage(const Storage &s,const C::Result &m,const ExecutionTransactions::Result &e,const H::Result &h,const Result &r){
 if(!s.memory||s.captureBytes!=I::ImageBytes||s.scratchBytes!=4096)return false;const auto &a=*s.memory;
 if(a.liveBytes<8192||a.liveBytes>L::MaxChildBytes||a.liveBytes%4096)return false;
 const struct Span{const void *p;size_t n;} ins[]={{&s,sizeof(s)},{s.memory,sizeof(*s.memory)},{&m,sizeof(m)},{&e,sizeof(e)},{&h,sizeof(h)},
  {s.history,P::Slots*sizeof(R::Request)},{a.root,12288},{a.children,L::MaxChildBytes},{a.image,I::ImageBytes},{a.library,512},{a.code,4096},
  {s.plans[0],sizeof(I::Plan)},{s.plans[1],sizeof(I::Plan)},{s.plans[2],sizeof(I::Plan)},{s.plans[3],sizeof(I::Plan)}},
  outs[]={{&r,sizeof(r)},{s.capture,s.captureBytes},{s.scratch,s.scratchBytes},{s.proofPlan,sizeof(I::Plan)}};
 for(unsigned i=0;i<4;++i){
  for(const auto &in:ins)if(!P::separate(outs[i].p,outs[i].n,in.p,in.n))return false;
  for(unsigned j=0;j<i;++j)if(!P::separate(outs[i].p,outs[i].n,outs[j].p,outs[j].n))return false;
 }
 return true;
}
inline bool imageProfile(const Storage &s,unsigned staged){
 if(staged>P::Slots)return false;const auto &m=*s.memory;P::Library lib;
 if(!P::decode(m.library,512,m.code,4096,lib)||!I::initial(m.library,512,m.code,4096,s.capture,s.captureBytes))return false;
 for(unsigned j=0;j<staged;++j){const auto &request=s.history[j];
  if(!R::valid(request,lib)||request.id!=j+1||request.generation!=s.history[0].generation||
     !I::plan(m.library,512,m.code,4096,request,s.scratch,s.scratchBytes,*s.proofPlan)||
     !X::samePlan(*s.proofPlan,*s.plans[j])||!I::commit(*s.proofPlan,s.capture,s.captureBytes))return false;
 }
 return RtxProgramStage033::equal(s.capture,m.image,I::ImageBytes);
}
template<class IO>bool tick(IO &io,Result &r){
 if(!io.ready())return fail(r,Owner);const auto now=io.nowNs();
 if(now<r.started||now-r.started<r.elapsed)return fail(r,Clock);
 r.elapsed=now-r.started;if(r.elapsed>=BudgetNs||r.operations>=MaxOperations)return fail(r,Timeout);++r.operations;return true;
}
template<class IO>bool read(IO &io,Result &r,unsigned a,uint8_t *out,unsigned n){
 if(!tick(io,r))return false;++r.reads;r.lastAddress=a;if(!io.readMemory(a,out,n))return fail(r,Read);return tick(io,r);
}
template<class IO>bool match(IO &io,Result &r,unsigned a,const uint8_t *expected,unsigned n,Failure why=Changed){
 uint8_t data[256];for(unsigned off=0;off<n;off+=256){const unsigned count=n-off<256?n-off:256;
  if(!read(io,r,a+off,data,count))return false;
  for(unsigned i=0;i<count;++i)if(data[i]!=(expected?expected[off+i]:0)){r.lastAddress=a+off+i;return fail(r,why);}
 }
 return true;
}
template<class IO>bool write(IO &io,Result &r,unsigned a,const uint8_t *data,unsigned n){
 if(!tick(io,r))return false;++r.writes;r.lastAddress=a;if(!io.writeMemory(a,data,n))return fail(r,Write);return match(io,r,a,data,n);
}
template<class IO>bool observe(IO &io,Result &r){
 uint8_t data[8];if(!read(io,r,H::Get,data,8))return false;r.get=P::get32(data);r.put=P::get32(data+4);
 if(!read(io,r,H::Fence,data,4))return false;r.hostFence=P::get32(data);
 if(!read(io,r,P::Q::FencePhysical+r.job*256,data,4))return false;r.completion=P::get32(data);return true;
}
inline bool initial(const Result &r){return r.get==r.job+1&&r.put==r.job+1&&r.hostFence==SubmitCodec::FenceValue&&!r.completion;}
inline bool progress(Result &r){
 return (r.get>=r.job+1&&r.get<=r.job+2&&r.put==r.job+2&&r.hostFence==SubmitCodec::FenceValue&&
  (!r.completion||r.completion==P::completion(r.job)))||fail(r,Unexpected);
}
inline bool done(const Result &r){return r.job<P::Slots&&r.get==r.job+2&&r.put==r.job+2&&r.hostFence==SubmitCodec::FenceValue&&r.completion==P::completion(r.job);}
template<class IO>bool queueGuards(IO &io,Result &r,const Storage &s,const H::Result &host,unsigned submitted){
 if(submitted>P::Slots||!match(io,r,H::Command,host.command,20)||!match(io,r,H::Command+20,nullptr,44)||
    !match(io,r,H::Ring,host.entry,8)||!match(io,r,H::Fence+4,nullptr,4092))return false;
 for(unsigned j=0;j<P::Slots;++j){
  if(j<submitted&&!s.plans[j])return fail(r,Proof);
  if(!match(io,r,commandPhysical(j),j<submitted?s.plans[j]->command:nullptr,32)||!match(io,r,commandPhysical(j)+32,nullptr,32)||
     !match(io,r,entryPhysical(j),j<submitted?s.plans[j]->entry:nullptr,8))return false;
 }
 return match(io,r,H::Command+320,nullptr,4096-320)&&match(io,r,H::Ring+40,nullptr,256-40);
}
template<class IO>bool backing(IO &io,Result &r,const Storage &s,unsigned staged,unsigned completed){
 const auto &m=*s.memory;P::Library lib;
 if(!P::decode(m.library,512,m.code,4096,lib))return fail(r,Proof);
 for(unsigned off=0;off<I::ImageBytes;off+=256)if(!read(io,r,C::Base+off,s.capture+off,256))return false;
 return I::capture(s.capture,s.captureBytes,m.image,I::ImageBytes,lib,s.history,staged,completed)||fail(r,Changed);
}
template<class IO>bool execute(IO &io,unsigned job,const Storage &s,const C::Result &memory,
 const ExecutionTransactions::Result &execution,const H::Result &host,Result &r){
 if(!storage(s,memory,execution,host,r))return false;
 if(r.attempted||r.failure!=None)return false;r.job=job;
 if(job>=P::Slots)return fail(r,Arguments);
 if(!C::ready(memory,s.memory->liveBytes)||!C::hostReady(host,execution)||!imageProfile(s,job+1))return fail(r,Proof);
 for(unsigned i=0;i<32;++i)r.command[i]=s.plans[job]->command[i];for(unsigned i=0;i<8;++i)r.entry[i]=s.plans[job]->entry[i];
 r.attempted=true;r.started=io.nowNs();r.token=execution.candidate;
 if(!tick(io,r))return false;if(!io.claim(job))return fail(r,Replay);r.claimed=true;
 if(!tick(io,r)||!io.physicalMode()||!tick(io,r))return fail(r,Proof);
 if(!match(io,r,unsigned(L::OldBase),s.memory->root,12288)||!match(io,r,unsigned(L::NewBase),s.memory->children,s.memory->liveBytes)||
    !backing(io,r,s,job+1,job)||!queueGuards(io,r,s,host,job))return false;
 if(!observe(io,r))return false;r.initialGet=r.get;r.initialPut=r.put;r.initialCompletion=r.completion;
 if(!initial(r))return fail(r,Initial);
 r.commandAttempted=true;if(!write(io,r,commandPhysical(job),r.command,32))return false;
 r.entryAttempted=true;if(!write(io,r,entryPhysical(job),r.entry,8))return false;
 if(!observe(io,r))return false;if(!initial(r))return fail(r,Changed);
 uint8_t put[4];P::Q::put32(put,job+2);r.putAttempted=true;if(!write(io,r,H::Put,put,4))return false;
 if(!tick(io,r))return false;r.bellAttempted=true;if(!io.notify(r.token))return fail(r,Notify);
 while(tick(io,r)){
  ++r.polls;if(!observe(io,r)||!progress(r))return false;
  if(done(r)){
   if(!backing(io,r,s,job+1,job+1))return false;r.immutableVerified=true;
   if(!queueGuards(io,r,s,host,job+1)||!match(io,r,unsigned(L::OldBase),s.memory->root,12288)||
      !match(io,r,unsigned(L::NewBase),s.memory->children,s.memory->liveBytes))return false;
   r.guardsVerified=true;if(!observe(io,r)||!progress(r))return false;if(!done(r))return fail(r,Changed);
   r.stable=true;r.passed=true;return true;
  }
  io.delayUs(100);
 }
 return false;
}
}
