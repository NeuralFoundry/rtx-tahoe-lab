#pragma once
#include "../runtime/ExecutionTransactions.hpp"
// One fixed HOST semaphore release. This does not execute compute or Metal.
namespace HostFence {
namespace R=GSPComputePrep;namespace P=ExecutionPlan;namespace T=ExecutionTransactions;
constexpr unsigned Ring=0x03400000,Command=0x03402000,Fence=0x03403000,Get=0x03400888,Put=Get+4;
constexpr unsigned Doorbell=0x00bb0090,CommandBytes=20,MaxOperations=65536;
constexpr unsigned long long BudgetNs=5000000000ULL;
enum Failure {None,Arguments,Proof,Owner,Timeout,Read,Write,Initial,Changed,Notify,Unexpected};
struct Result {
  Failure failure=None;bool claimed=false,commandAttempted=false,entryAttempted=false,putAttempted=false,bellAttempted=false,passed=false;
  unsigned operations=0,polls=0,reads=0,writes=0,initialGet=~0U,initialPut=~0U,initialFence=~0U,lastGet=~0U,lastPut=~0U,lastFence=~0U,token=0;
  unsigned long long started=0,elapsed=0;unsigned char command[20]={},entry[8]={};
};
inline bool fail(Result &r,Failure f){if(!r.failure)r.failure=f;return false;}
inline bool samePrivate(const P::Plan &a,const P::Plan &b){
  if(a.valid!=b.valid||a.physicalEnd!=b.physicalEnd||a.virtualEnd!=b.virtualEnd||a.backingBytes!=b.backingBytes)return false;
  for(unsigned i=0;i<3;++i){const auto &x=a.buffers[i],&y=b.buffers[i];if(x.id!=y.id||x.kind!=y.kind||x.bytes!=y.bytes||x.allocated!=y.allocated||x.alignment!=y.alignment||x.physical!=y.physical||x.va!=y.va)return false;}return true;
}
inline bool proof(const ChannelCodec::Plan &golden,const T::Result &execution,const unsigned char *requests,const unsigned char *records,unsigned char *scratch){
  const auto &r=execution.rpc;
  const auto &x=execution.external;
  if(!x.rpc.passed||x.rpc.failure||x.rpc.sent!=ExternalVAS::Steps||x.rpc.completed!=ExternalVAS::Steps||
     x.rpc.txWriter!=ExternalVAS::FinalProducer||x.rpc.txReader!=ExternalVAS::FinalProducer||x.directoryChecks!=2||!x.directoryAcknowledged||!x.vas.accepted||
     r.initialReader!=x.rpc.rxReader||r.initialSequence!=x.rpc.rxSequence)return false;
  if(!r.passed||r.failure||r.sent!=ExecutionCodec::Steps||r.completed!=ExecutionCodec::Steps||r.txWriter!=ExecutionCodec::FinalProducer||r.txReader!=ExecutionCodec::FinalProducer||!execution.fixedPreparationAttempted||!execution.fixedPrepared||
     !execution.contextPreparationAttempted||!execution.contextPrepared||!P::sessionId(execution.channelId)||execution.subdeviceMask>1||
     r.count<ExecutionCodec::Steps||r.count>R::MaxRecords||r.pages<ExecutionCodec::Steps||r.pages>R::MaxPages||r.bytes!=r.pages*4096||r.initialReader>=63||r.rxReader>=63||r.rxProducer>=63)return false;
  ExecutionTranscript::Result decoded;
  if(!ExecutionTranscript::verify(requests,ExecutionCodec::RequestBytes,records,r.bytes,r.initialSequence,golden,3,scratch,decoded)||decoded.channelId!=execution.channelId||
     decoded.subdeviceMask!=execution.subdeviceMask||decoded.rawToken!=execution.rawToken||decoded.candidate!=execution.candidate||
     decoded.nextSequence!=r.rxSequence||decoded.records!=r.count||decoded.pages!=r.pages||!samePrivate(decoded.privatePlan,execution.context))return false;
  const auto &a=decoded.runlist,&b=execution.runlist;
  if(a.valid!=b.valid||a.sequence!=b.sequence||a.entry!=b.entry||a.id!=b.id||a.pbdmas!=b.pbdmas)return false;
  for(unsigned i=0;i<2;++i)if(a.pbdma[i]!=b.pbdma[i]||a.fault[i]!=b.fault[i])return false;
  unsigned offset=0,pages=0,step=0;
  for(unsigned i=0;i<r.count;++i){const auto &row=r.records[i];GSPInitEvents::Record d;
    if(row.offset!=offset||row.offset>r.bytes||row.bytes>r.bytes-offset||row.sequence!=r.initialSequence+i||row.slot!=(r.initialReader+pages)%63||
       row.step!=step||!GSPInitEvents::decode(records+offset,row.bytes,row.sequence,d)||row.function!=d.function||row.result!=d.result||row.payload!=d.payloadBytes)return false;
    if(d.function==ExecutionCodec::function(step))++step;offset+=row.bytes;pages+=row.bytes/4096;
  }
  return step==ExecutionCodec::Steps&&offset==r.bytes&&r.rxReader==(r.initialReader+pages)%63;
}
template<class IO>bool tick(IO &io,Result &r){
  if(!io.ready())return fail(r,Owner);const auto now=io.nowNs();
  if(now<r.started)return fail(r,Timeout);r.elapsed=now-r.started;
  if(r.elapsed>=BudgetNs||++r.operations>MaxOperations)return fail(r,Timeout);return true;
}
template<class IO>bool read(IO &io,Result &r,unsigned address,unsigned char *out,unsigned bytes){
  if(!tick(io,r))return false;++r.reads;if(!io.readMemory(address,out,bytes))return fail(r,Read);return tick(io,r);
}
template<class IO>bool match(IO &io,Result &r,unsigned address,const unsigned char *expected,unsigned bytes,bool initial=false){
  unsigned char data[64];
  for(unsigned off=0;off<bytes;off+=64){const unsigned n=bytes-off<64?bytes-off:64;
    if(!read(io,r,address+off,data,n))return false;
    for(unsigned i=0;i<n;++i)if(data[i]!=(expected?expected[off+i]:0))return fail(r,initial?Initial:Changed);
  }return true;
}
template<class IO>bool write(IO &io,Result &r,unsigned address,const unsigned char *data,unsigned bytes){
  if(!tick(io,r))return false;++r.writes;if(!io.writeMemory(address,data,bytes))return fail(r,Write);
  // Same BAR1 target readback drains posted writes before publication/notification.
  return match(io,r,address,data,bytes);
}
template<class IO>bool observe(IO &io,Result &r){
  unsigned char word[8];if(!read(io,r,Get,word,8))return false;r.lastGet=R::get32(word);r.lastPut=R::get32(word+4);
  if(!read(io,r,Fence,word,4))return false;r.lastFence=R::get32(word);return true;
}
template<class IO>bool execute(IO &io,const ChannelCodec::Plan &golden,const T::Result &execution,const unsigned char *requests,
 const unsigned char *records,unsigned char *scratch,Result &r){
  const void *inputs[]={&golden,&execution,requests,records,scratch};const size_t sizes[]={sizeof(golden),sizeof(execution),ExecutionCodec::RequestBytes,execution.rpc.bytes,4096};
  for(unsigned i=0;i<5;++i)if(inputs[i]&&!P::disjoint(inputs[i],sizes[i],&r,sizeof(r)))return false;
  r={};if(!requests||!records||!scratch)return fail(r,Arguments);
  for(unsigned i=0;i<4;++i)if(!P::disjoint(inputs[i],sizes[i],scratch,4096))return fail(r,Arguments);
  if(!proof(golden,execution,requests,records,scratch))return fail(r,Proof);
  r.started=io.nowNs();r.token=execution.candidate;
  if(!tick(io,r)||!io.verifyMappings()||!tick(io,r))return fail(r,Proof);
  if(!io.claim())return fail(r,Owner);r.claimed=true;
  unsigned long long entry=0;
  if(!SubmitCodec::fence(r.command,20)||!SubmitCodec::entry(SubmitCodec::CommandVA,20,entry))return fail(r,Arguments);
  GMMULeaves::write64(r.entry,entry);
  if(!observe(io,r))return false;r.initialGet=r.lastGet;r.initialPut=r.lastPut;r.initialFence=r.lastFence;
  if(r.initialGet||r.initialPut||r.initialFence)return fail(r,Initial);
  if(!match(io,r,Ring,nullptr,256,true)||!match(io,r,Command,nullptr,4096,true)||!match(io,r,Fence,nullptr,4096,true))return false;
  r.commandAttempted=true;if(!write(io,r,Command,r.command,20))return false;
  r.entryAttempted=true;if(!write(io,r,Ring,r.entry,8))return false;
  if(!observe(io,r))return false;if(r.lastGet||r.lastPut||r.lastFence)return fail(r,Changed);
  unsigned char one[4];R::put32(one,1);r.putAttempted=true;if(!write(io,r,Put,one,4))return false;
  if(!tick(io,r))return false;r.bellAttempted=true;
  if(!io.notify(r.token))return fail(r,Notify);
  while(tick(io,r)){
    ++r.polls;if(!observe(io,r))return false;
    if(r.lastGet>1||r.lastPut!=1||(r.lastFence&&r.lastFence!=SubmitCodec::FenceValue))return fail(r,Unexpected);
    if(r.lastGet==1&&r.lastFence==SubmitCodec::FenceValue){
      if(!match(io,r,Command,r.command,20)||!match(io,r,Ring,r.entry,8)||!match(io,r,Fence+4,nullptr,4092))return false;
      // Confirm the completion pair remains stable after all guard reads.
      if(!observe(io,r))return false;
      if(r.lastGet!=1||r.lastPut!=1||r.lastFence!=SubmitCodec::FenceValue)return fail(r,Changed);
      r.passed=true;return true;
    }
    io.delayUs(100);
  }return false;
}
}
