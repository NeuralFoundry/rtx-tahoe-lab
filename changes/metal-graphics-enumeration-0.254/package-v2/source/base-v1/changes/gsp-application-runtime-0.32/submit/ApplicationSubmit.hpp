#pragma once
#include "../memory/ApplicationMemory.hpp"
namespace ApplicationSubmit {
namespace C=ApplicationMemory;namespace V=RtxBatch031;namespace H=HostFence;namespace Q=QmdProfile;
namespace P=ExecutionPlan;namespace R=GSPComputePrep;namespace L=GMMULeaves;
constexpr unsigned MaxOperations=65536;
constexpr unsigned long long BudgetNs=5000000000ULL;
constexpr unsigned commandPhysical(unsigned j){return H::Command+64+j*64;}
constexpr unsigned entryPhysical(unsigned j){return H::Ring+8+j*8;}
enum Failure:unsigned {None,Arguments,Proof,Owner,Replay,Clock,Timeout,Read,Write,Initial,Changed,Notify,Unexpected};
struct Result {
 Failure failure=None;bool claimed=false,commandAttempted=false,entryAttempted=false,putAttempted=false,bellAttempted=false;
 bool immutableVerified=false,guardsVerified=false,stable=false,passed=false;
 unsigned job=0,operations=0,reads=0,writes=0,polls=0,token=0,lastAddress=0;
 unsigned initialGet=~0U,initialPut=~0U,initialCompletion=~0U,get=~0U,put=~0U,hostFence=~0U,completion=~0U;
 unsigned initialOutput[64]={},output[64]={},completedElements=0,expectedCount=0;
 unsigned long long started=0,elapsed=0;unsigned char command[32]={},entry[8]={};
};
inline bool fail(Result &r,Failure f){if(r.failure==None)r.failure=f;r.passed=false;return false;}
inline bool command(unsigned job,unsigned char *out){
 if(job>=V::Jobs||!out)return false;
 const unsigned words[]={0x20012000,0xc7c0,0x200125a6,0x1011,0x200120ad,unsigned((Q::QmdVA+job*256)>>8),0x200120b0,9};
 for(unsigned i=0;i<8;++i)R::put32(out+i*4,words[i]);return true;
}
inline bool entry(unsigned job,unsigned char *out){
 unsigned long long value=0;if(job>=V::Jobs||!out||!SubmitCodec::entry(V::commandVA(job),32,value)||value!=V::entry(job))return false;
 L::write64(out,value);return true;
}
inline unsigned sum(const C::Storage &s,unsigned j,unsigned i){return R::get32(s.image+V::cbOffset(j)+512+i*4)+R::get32(s.image+V::cbOffset(j)+768+i*4);}
inline unsigned count(const C::Storage &s,unsigned j){return R::get32(s.image+V::cbOffset(j)+0x178);}
inline bool imageProfile(const C::Storage &s){
 if(!s.image||!s.command||!s.requests)return false;unsigned char first[32];if(!command(0,first)||!ExecutionMemory::equal(first,s.command,32))return false;
 for(unsigned j=0;j<V::Jobs;++j){const auto *cb=s.image+V::cbOffset(j);const auto cbVA=Q::ConstantVA+j*1024;
  const auto &request=s.requests[j];
  if(!RtxApplication032::valid(request)||request.requestId!=j+1||request.generation!=s.requests[0].generation||
   count(s,j)!=request.count||R::get32(s.image+V::fenceOffset(j))||
   L::read64(cb+0x160)!=Q::OutputVA+j*1024||L::read64(cb+0x168)!=cbVA+512||L::read64(cb+0x170)!=cbVA+768)return false;
  for(unsigned i=0;i<64;++i)if(R::get32(cb+512+i*4)!=request.a[i]||R::get32(cb+768+i*4)!=request.b[i]||
   R::get32(s.image+V::outOffset(j)+i*4)!=~sum(s,j,i))return false;
 }return true;
}
template<class IO>bool tick(IO &io,Result &r){
 if(!io.ready())return fail(r,Owner);const auto now=io.nowNs();
 if(now<r.started||now-r.started<r.elapsed)return fail(r,Clock);
 r.elapsed=now-r.started;if(r.elapsed>=BudgetNs||r.operations>=MaxOperations)return fail(r,Timeout);++r.operations;return true;
}
template<class IO>bool read(IO &io,Result &r,unsigned a,unsigned char *out,unsigned n){
 if(!tick(io,r))return false;++r.reads;r.lastAddress=a;if(!io.readMemory(a,out,n))return fail(r,Read);return tick(io,r);
}
template<class IO>bool match(IO &io,Result &r,unsigned a,const unsigned char *expected,unsigned n,Failure why=Changed){
 unsigned char data[256];for(unsigned off=0;off<n;off+=256){const unsigned count=n-off<256?n-off:256;
  if(!read(io,r,a+off,data,count))return false;
  for(unsigned i=0;i<count;++i)if(data[i]!=(expected?expected[off+i]:0)){r.lastAddress=a+off+i;return fail(r,why);}
 }return true;
}
template<class IO>bool write(IO &io,Result &r,unsigned a,const unsigned char *data,unsigned n){
 if(!tick(io,r))return false;++r.writes;r.lastAddress=a;if(!io.writeMemory(a,data,n))return fail(r,Write);return match(io,r,a,data,n);
}
template<class IO>bool observe(IO &io,Result &r){
 unsigned char data[256];if(!read(io,r,H::Get,data,8))return false;r.get=R::get32(data);r.put=R::get32(data+4);
 if(!read(io,r,H::Fence,data,4))return false;r.hostFence=R::get32(data);
 if(!read(io,r,Q::OutputPhysical+r.job*1024,data,256))return false;for(unsigned i=0;i<64;++i)r.output[i]=R::get32(data+i*4);
 if(!read(io,r,Q::FencePhysical+r.job*256,data,4))return false;r.completion=R::get32(data);return true;
}
inline bool initial(const C::Storage &s,const Result &r){
 if(r.get!=r.job+1||r.put!=r.job+1||r.hostFence!=SubmitCodec::FenceValue||r.completion)return false;
 for(unsigned i=0;i<64;++i)if(r.output[i]!=~sum(s,r.job,i))return false;return true;
}
inline bool progress(const C::Storage &s,Result &r){
 if(r.get<r.job+1||r.get>r.job+2||r.put!=r.job+2||r.hostFence!=SubmitCodec::FenceValue||(r.completion&&r.completion!=RtxApplication032::completion(r.job)))return fail(r,Unexpected);
 r.completedElements=0;
 for(unsigned i=0;i<64;++i){const auto expected=sum(s,r.job,i),poison=~expected;
  if(i<count(s,r.job)&&r.output[i]==expected)++r.completedElements;
  else if(r.output[i]!=poison)return fail(r,Unexpected);
 }return true;
}
inline bool done(const Result &r){return r.job<V::Jobs&&r.expectedCount<=64&&r.get==r.job+2&&r.put==r.job+2&&r.hostFence==SubmitCodec::FenceValue&&r.completion==RtxApplication032::completion(r.job)&&r.completedElements==r.expectedCount;}
template<class IO>bool queueGuards(IO &io,Result &r,const H::Result &host,unsigned submitted){
 if(submitted>V::Jobs||!match(io,r,H::Command,host.command,20)||!match(io,r,H::Command+20,nullptr,44)||!match(io,r,H::Ring,host.entry,8)||!match(io,r,H::Fence+4,nullptr,4092))return false;
 unsigned char cmd[32],ent[8];
 for(unsigned j=0;j<V::Jobs;++j){if(!command(j,cmd)||!entry(j,ent))return fail(r,Proof);
  if(!match(io,r,commandPhysical(j),j<submitted?cmd:nullptr,32)||!match(io,r,commandPhysical(j)+32,nullptr,32)||!match(io,r,entryPhysical(j),j<submitted?ent:nullptr,8))return false;
 }
 return match(io,r,H::Command+320,nullptr,4096-320)&&match(io,r,H::Ring+40,nullptr,256-40);
}
template<class IO>bool backing(IO &io,Result &r,const C::Storage &s,unsigned completed){
 if(completed>V::Jobs)return fail(r,Arguments);
 // Preserve code/all inputs, all unsubmitted QMDs and the unused QMD page tail.
 if(!match(io,r,C::Base,s.image,12288)||!match(io,r,Q::QmdPhysical+completed*256,s.image+12288+completed*256,4096-completed*256))return false;
 unsigned char expected[256];
 for(unsigned j=0;j<V::Jobs;++j){
  for(unsigned i=0;i<64;++i)R::put32(expected+i*4,j<completed&&i<count(s,j)?sum(s,j,i):~sum(s,j,i));
  if(!match(io,r,Q::OutputPhysical+j*1024,expected,256)||!match(io,r,Q::OutputPhysical+j*1024+256,s.image+V::outOffset(j)+256,768))return false;
  R::put32(expected,j<completed?RtxApplication032::completion(j):0);
  if(!match(io,r,Q::FencePhysical+j*256,expected,4)||!match(io,r,Q::FencePhysical+j*256+4,s.image+V::fenceOffset(j)+4,252))return false;
 }
 return match(io,r,Q::FencePhysical+1024,s.image+21504,3072);
}
template<class IO>bool execute(IO &io,unsigned job,const C::Storage &s,const C::Result &memory,const ExecutionTransactions::Result &execution,const H::Result &host,Result &r){
 const void *inputs[]={&s,&memory,&execution,&host,s.root,s.children,s.image,s.command,s.requests};
 const size_t sizes[]={sizeof(s),sizeof(memory),sizeof(execution),sizeof(host),12288,L::MaxChildBytes,C::Bytes,32,4*sizeof(RtxApplication032::Request)};
 for(unsigned i=0;i<9;++i)if(inputs[i]&&!P::disjoint(inputs[i],sizes[i],&r,sizeof(r)))return false;
 // Preserve any previous uncertain or successful attempt instead of resetting it.
 if(r.claimed)return false;r={};r.job=job;
 if(job>=V::Jobs||!s.root||!s.children||!s.image||!s.command||s.liveBytes<8192||s.liveBytes>L::MaxChildBytes||s.liveBytes%4096)return fail(r,Arguments);
 if(!C::ready(memory,s.liveBytes)||!C::hostReady(host,execution)||!imageProfile(s))return fail(r,Proof);
 r.expectedCount=count(s,job);
 if(!command(job,r.command)||!entry(job,r.entry))return fail(r,Arguments);
 r.started=io.nowNs();r.token=execution.candidate;if(!tick(io,r))return false;if(!io.claim(job))return fail(r,Replay);r.claimed=true;
 if(!tick(io,r)||!io.physicalMode()||!tick(io,r))return fail(r,Proof);
 if(!match(io,r,unsigned(L::OldBase),s.root,12288)||!match(io,r,unsigned(L::NewBase),s.children,s.liveBytes)||!backing(io,r,s,job)||!queueGuards(io,r,host,job))return false;
 if(!observe(io,r))return false;r.initialGet=r.get;r.initialPut=r.put;r.initialCompletion=r.completion;
 for(unsigned i=0;i<64;++i)r.initialOutput[i]=r.output[i];if(!initial(s,r))return fail(r,Initial);
 r.commandAttempted=true;if(!write(io,r,commandPhysical(job),r.command,32))return false;
 r.entryAttempted=true;if(!write(io,r,entryPhysical(job),r.entry,8))return false;
 if(!observe(io,r))return false;if(!initial(s,r))return fail(r,Changed);
 unsigned char put[4];R::put32(put,job+2);r.putAttempted=true;if(!write(io,r,H::Put,put,4))return false;
 if(!tick(io,r))return false;r.bellAttempted=true;if(!io.notify(r.token))return fail(r,Notify);
 while(tick(io,r)){
  ++r.polls;if(!observe(io,r)||!progress(s,r))return false;
  if(done(r)){
   if(!backing(io,r,s,job+1))return false;r.immutableVerified=true;
   if(!queueGuards(io,r,host,job+1)||!match(io,r,unsigned(L::OldBase),s.root,12288)||!match(io,r,unsigned(L::NewBase),s.children,s.liveBytes))return false;
   r.guardsVerified=true;if(!observe(io,r)||!progress(s,r))return false;if(!done(r))return fail(r,Changed);
   r.stable=true;r.passed=true;return true;
  }
  io.delayUs(100);
 }return false;
}
}
