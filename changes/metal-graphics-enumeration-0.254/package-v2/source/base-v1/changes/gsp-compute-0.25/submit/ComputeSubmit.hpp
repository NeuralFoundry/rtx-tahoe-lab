#pragma once
#include "../memory/ComputeMemory.hpp"
namespace ComputeSubmit {
namespace C=ComputeMemory;namespace H=HostFence;namespace P=ExecutionPlan;namespace Q=QmdProfile;namespace R=GSPComputePrep;namespace L=GMMULeaves;
constexpr unsigned Command=H::Command+64,Entry=H::Ring+8,MaxOperations=65536;
constexpr unsigned long long CommandVA=SubmitCodec::CommandVA+64,BudgetNs=5000000000ULL;
enum Failure:unsigned {None,Arguments,Proof,Owner,Replay,Clock,Timeout,Read,Write,Initial,Changed,Notify,Unexpected};
struct Result {
 Failure failure=None;bool claimed=false,commandAttempted=false,entryAttempted=false,putAttempted=false,bellAttempted=false;
 bool immutableVerified=false,guardsVerified=false,stable=false,passed=false;
 unsigned operations=0,reads=0,writes=0,polls=0,token=0,lastAddress=0;
 unsigned initialGet=~0U,initialPut=~0U,initialOutput=~0U,initialCompletion=~0U;
 unsigned get=~0U,put=~0U,hostFence=~0U,output=~0U,completion=~0U;
 unsigned long long started=0,elapsed=0;unsigned char command[32]={},entry[8]={};
};
inline bool fail(Result &r,Failure f){if(r.failure==None)r.failure=f;r.passed=false;return false;}
inline bool commandValid(const unsigned char *p){
 if(!p)return false;
 constexpr unsigned words[]={0x20012000,0xc7c0,0x200125a6,0x1011,0x200120ad,unsigned(Q::QmdVA>>8),0x200120b0,9};
 for(unsigned i=0;i<8;++i)if(R::get32(p+i*4)!=words[i])return false;return true;
}
inline bool entry(unsigned char *out){
 unsigned long long value=0;if(!out||!SubmitCodec::entry(CommandVA,32,value))return false;L::write64(out,value);return true;
}
template<class IO>bool tick(IO &io,Result &r){
 if(!io.ready())return fail(r,Owner);const auto now=io.nowNs();
 if(now<r.started||now-r.started<r.elapsed)return fail(r,Clock);
 r.elapsed=now-r.started;if(r.elapsed>=BudgetNs||r.operations>=MaxOperations)return fail(r,Timeout);
 ++r.operations;return true;
}
template<class IO>bool read(IO &io,Result &r,unsigned a,unsigned char *out,unsigned n){
 if(!tick(io,r))return false;++r.reads;r.lastAddress=a;
 if(!io.readMemory(a,out,n))return fail(r,Read);return tick(io,r);
}
template<class IO>bool match(IO &io,Result &r,unsigned a,const unsigned char *expected,unsigned n,Failure why=Changed){
 unsigned char data[256];
 for(unsigned off=0;off<n;off+=256){const unsigned count=n-off<256?n-off:256;
  if(!read(io,r,a+off,data,count))return false;
  for(unsigned i=0;i<count;++i)if(data[i]!=(expected?expected[off+i]:0)){r.lastAddress=a+off+i;return fail(r,why);}
 }return true;
}
template<class IO>bool write(IO &io,Result &r,unsigned a,const unsigned char *data,unsigned n){
 if(!tick(io,r))return false;++r.writes;r.lastAddress=a;
 if(!io.writeMemory(a,data,n))return fail(r,Write);
 return match(io,r,a,data,n); // Posted write readback before publishing the next field.
}
template<class IO>bool observe(IO &io,Result &r){
 unsigned char data[8];if(!read(io,r,H::Get,data,8))return false;r.get=R::get32(data);r.put=R::get32(data+4);
 if(!read(io,r,H::Fence,data,4))return false;r.hostFence=R::get32(data);
 if(!read(io,r,Q::OutputPhysical,data,4))return false;r.output=R::get32(data);
 if(!read(io,r,Q::FencePhysical,data,4))return false;r.completion=R::get32(data);return true;
}
inline bool initial(const Result &r){return r.get==1&&r.put==1&&r.hostFence==SubmitCodec::FenceValue&&!r.output&&!r.completion;}
template<class IO>bool queueGuards(IO &io,Result &r,const H::Result &host,bool submitted){
 return match(io,r,H::Command,host.command,20)&&match(io,r,H::Command+20,nullptr,44)&&
  match(io,r,Command,submitted?r.command:nullptr,32)&&match(io,r,Command+32,nullptr,4096-96)&&
  match(io,r,H::Ring,host.entry,8)&&match(io,r,Entry,submitted?r.entry:nullptr,8)&&match(io,r,Entry+8,nullptr,240)&&
  match(io,r,H::Fence+4,nullptr,4092);
}
template<class IO>bool execute(IO &io,const C::Storage &s,const C::Result &memory,const ExecutionTransactions::Result &execution,const H::Result &host,Result &r){
 const void *inputs[]={&s,&memory,&execution,&host,s.root,s.children,s.image,s.command};
 const size_t sizes[]={sizeof(s),sizeof(memory),sizeof(execution),sizeof(host),12288,L::MaxChildBytes,C::Bytes,32};
 for(unsigned i=0;i<8;++i)if(inputs[i]&&!P::disjoint(inputs[i],sizes[i],&r,sizeof(r)))return false;
 r={};
 if(!s.root||!s.children||!s.image||!s.command||s.liveBytes<8192||s.liveBytes>L::MaxChildBytes||s.liveBytes%4096)return fail(r,Arguments);
 if(!C::ready(memory,s.liveBytes)||!C::hostReady(host,execution)||!commandValid(s.command)||
    R::get32(s.image+16384)||R::get32(s.image+20480))return fail(r,Proof);
 for(unsigned i=0;i<32;++i)r.command[i]=s.command[i];if(!entry(r.entry))return fail(r,Arguments);
 r.started=io.nowNs();r.token=execution.candidate;
 if(!tick(io,r))return false;if(!io.claim())return fail(r,Replay);r.claimed=true;
 if(!tick(io,r)||!io.physicalMode()||!tick(io,r))return fail(r,Proof);
 if(!match(io,r,unsigned(L::OldBase),s.root,12288)||!match(io,r,unsigned(L::NewBase),s.children,s.liveBytes)||
    !match(io,r,C::Base,s.image,C::Bytes)||!queueGuards(io,r,host,false))return false;
 if(!observe(io,r))return false;
 r.initialGet=r.get;r.initialPut=r.put;r.initialOutput=r.output;r.initialCompletion=r.completion;
 if(!initial(r))return fail(r,Initial);
 r.commandAttempted=true;if(!write(io,r,Command,r.command,32))return false;
 r.entryAttempted=true;if(!write(io,r,Entry,r.entry,8))return false;
 if(!observe(io,r))return false;if(!initial(r))return fail(r,Changed);
 unsigned char two[4];R::put32(two,2);r.putAttempted=true;if(!write(io,r,H::Put,two,4))return false;
 if(!tick(io,r))return false;r.bellAttempted=true;if(!io.notify(r.token))return fail(r,Notify);
 while(tick(io,r)){
  ++r.polls;if(!observe(io,r))return false;
  if(r.get<1||r.get>2||r.put!=2||r.hostFence!=SubmitCodec::FenceValue||(r.output&&r.output!=Q::OutputValue)||
     (r.completion&&r.completion!=Q::FenceValue))return fail(r,Unexpected);
  if(r.get==2&&r.output==Q::OutputValue&&r.completion==Q::FenceValue){
   // Hardware may update QMD state. Only its unused page tail is immutable.
   if(!match(io,r,C::Base,s.image,12288)||!match(io,r,Q::QmdPhysical+256,s.image+12544,3840))return false;
   r.immutableVerified=true;
   if(!match(io,r,Q::OutputPhysical+4,s.image+16388,4092)||!match(io,r,Q::FencePhysical+4,s.image+20484,4092)||
      !queueGuards(io,r,host,true)||!match(io,r,unsigned(L::OldBase),s.root,12288)||
      !match(io,r,unsigned(L::NewBase),s.children,s.liveBytes))return false;
   r.guardsVerified=true;
   if(!observe(io,r))return false;
   if(r.get!=2||r.put!=2||r.hostFence!=SubmitCodec::FenceValue||r.output!=Q::OutputValue||r.completion!=Q::FenceValue)return fail(r,Changed);
   r.stable=true;r.passed=true;return true;
  }
  io.delayUs(100);
 }
 return false;
}
}
