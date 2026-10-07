#pragma once
#include "OwnedRootRpc169.hpp"
#include "../driver/GSPComputePrepProtocol.hpp"
#include "../changes/gsp-submit-0.24/transactions/ExecutionCodec.hpp"

// One fixed RPC54 at an idle, pinned post-program bootstrap boundary. TX queue
// indices and RX message sequences are separate streams. IO owns a one-shot
// claim and must independently restrict writes to this request and its indices.
namespace RTXRootTransition170 {
namespace R=GSPComputePrep;
struct Result {R::Result rpc;unsigned assertions=0;bool acknowledged=false,verified=false;};
inline bool origin(const R::Result&p){
 return p.passed&&!p.failure&&p.validated&&p.attempted&&p.prefixConsumed&&p.sent==ExecutionCodec::Steps&&p.completed==ExecutionCodec::Steps&&
  p.txWriter==ExecutionCodec::FinalProducer&&p.txReader==ExecutionCodec::FinalProducer&&p.doorbells==ExecutionCodec::Steps&&p.count>=ExecutionCodec::Steps&&p.count<=R::MaxRecords&&
  p.pages>=p.count&&p.pages<=R::MaxPages&&p.bytes==p.pages*4096&&p.rxReader<63&&p.rxProducer<63&&
  p.rxSequence&&p.rxSequence<UINT32_MAX-R::MaxRecords;
}
inline bool separate(const void*a,size_t an,const void*b,size_t bn){
 return RTXPageTree167::validRange(a,an)&&RTXPageTree167::validRange(b,bn)&&!RTXPageTree167::overlap(a,an,b,bn);
}
inline bool verify(const RTXTreeBacking168::Info&t,const R::Result&prior,const unsigned char*request,
 const unsigned char*records,const Result&result,unsigned char*scratch){
 const auto&r=result.rpc;
 if(!origin(prior)||!RTXRootRpc169::retained(t)||!r.passed||r.failure||!r.validated||!r.attempted||!r.prefixConsumed||
  !result.acknowledged||result.assertions||r.sent!=1||r.completed!=1||r.doorbells!=1||r.step||
  r.txWriter!=prior.txWriter+1||r.txReader!=r.txWriter||r.count<1||r.count>R::MaxRecords||
  r.pages<r.count||r.pages>R::MaxPages||r.bytes!=r.pages*4096||r.consumerWrites!=r.count||
  r.initialReader!=prior.rxReader||r.initialSequence!=prior.rxSequence||r.rxSequence!=r.initialSequence+r.count||
  r.rxReader!=(r.initialReader+r.pages)%63||r.rxProducer>=63||r.lastFunction!=54||r.lastResult||r.lastParamStatus||
  r.elapsedNs>=R::BudgetNs||r.ticks>R::MaxTicks)return false;
 const struct Span{const void*p;size_t n;}spans[]={{&t,sizeof(t)},{&prior,sizeof(prior)},
  {request,4096},{records,R::MaxBytes},{&result,sizeof(result)},{scratch,4096}};
 for(unsigned i=0;i<6;++i)for(unsigned j=0;j<i;++j)if(!separate(spans[i].p,spans[i].n,spans[j].p,spans[j].n))return false;
 if(!RTXRootRpc169::request(t,prior.txWriter,scratch,4096))return false;
 for(unsigned i=0;i<4096;++i)if(request[i]!=scratch[i])return false;
 unsigned offset=0,pages=0;
 for(unsigned i=0;i<r.count;++i){const auto&row=r.records[i];GSPInitEvents::Record packet;
  if(row.offset!=offset||!row.bytes||row.bytes>r.bytes-offset||row.step||row.sequence!=r.initialSequence+i||
   row.slot!=(r.initialReader+pages)%63||!GSPInitEvents::decode(records+offset,row.bytes,row.sequence,packet)||
   row.function!=packet.function||row.result!=packet.result||row.payload!=packet.payloadBytes)return false;
  if(i+1==r.count){if(!RTXRootRpc169::reply(t,row.sequence,records+offset,row.bytes))return false;}
  else if(packet.function==54||packet.function==0x1020||!R::asyncSupported(records+offset,packet))return false;
  offset+=row.bytes;pages+=row.bytes/4096;
 }
 return offset==r.bytes&&pages==r.pages;
}
template<class IO>bool receive(IO&io,const RTXTreeBacking168::Info&t,Result&result,unsigned char*records,unsigned char*scratch){
 auto&r=result.rpc;const auto started=r.elapsedNs;
 while(R::tick(io,r)){
  if(r.elapsedNs-started>=R::ReplyBudgetNs){R::fail(r,R::Timeout,"root-reply-timeout");return false;}
  ++r.polls;if(!R::geometry(io,r,scratch))return false;
  const unsigned available=(r.rxProducer+63-r.rxReader)%63;
  if(!available){io.delayUs(100);continue;}
  if(r.count>=R::MaxRecords||r.pages>=R::MaxPages){R::fail(r,R::Capacity,"root-evidence-capacity");return false;}
  const unsigned slot=r.rxReader;auto*raw=records+r.bytes;
  if(!R::read(io,r,0x42000+slot*4096,raw,4096))return false;
  const unsigned pages=R::get32(raw+40);
  if(!pages||pages>16||pages>R::MaxPages-r.pages){R::fail(r,R::Geometry,"root-reply-size");return false;}
  if(pages>available){io.delayUs(100);continue;}
  for(unsigned i=1;i<pages;++i)if(!R::read(io,r,0x42000+((slot+i)%63)*4096,raw+i*4096,4096))return false;
  if(!R::import(io,r))return false;
  for(unsigned i=0;i<pages;++i){
   if(!R::read(io,r,0x42000+((slot+i)%63)*4096,scratch,4096))return false;
   for(unsigned j=0;j<4096;++j)if(raw[i*4096+j]!=scratch[j]){R::fail(r,R::Changed,"root-reply-changed");return false;}
  }
  if(!R::geometry(io,r,scratch))return false;
  GSPInitEvents::Record packet;
  if(!GSPInitEvents::decode(raw,pages*4096,r.rxSequence,packet)){R::fail(r,R::Frame,"root-reply-frame");return false;}
  r.records[r.count++]={r.bytes,pages*4096,packet.function,packet.result,packet.sequence,packet.payloadBytes,0,slot,unsigned(r.elapsedNs/1000)};
  r.pages+=pages;r.bytes+=pages*4096;++r.rxSequence;r.lastFunction=packet.function;r.lastResult=packet.result;
  const bool expected=packet.function==54;
  if(expected){
   if(packet.result){R::fail(r,R::RpcResult,"root-rm-result");return false;}
   if(!RTXRootRpc169::reply(t,packet.sequence,raw,pages*4096)){R::fail(r,R::Parameters,"root-reply-parameters");return false;}
   result.acknowledged=true;r.lastParamStatus=0;
  }else if(!R::asyncSupported(raw,packet)){R::fail(r,R::Unexpected,"root-unsupported-event");return false;}
  if(packet.function==0x1020)++result.assertions;
  if(!R::consume(io,r,(slot+pages)%63,scratch))return false;
  // Retain diagnostic evidence, but do not continue to a GPU job after assert.
  if(result.assertions){R::fail(r,R::Unexpected,"root-firmware-assertion");return false;}
  if(expected){
   if(!R::geometry(io,r,scratch))return false;
   if(r.txReader!=r.txWriter){R::fail(r,R::Progress,"root-command-not-consumed");return false;}
   return true;
  }
 }
 return false;
}
template<class IO>bool execute(IO&io,const RTXTreeBacking168::Info&t,const R::Result&prior,
 unsigned char*request,unsigned char*records,unsigned char*scratch,Result&result){
 const struct Span{const void*p;size_t n;}spans[]={{&t,sizeof(t)},{&prior,sizeof(prior)},
  {request,4096},{records,R::MaxBytes},{scratch,4096},{&result,sizeof(result)}};
 // Check all aliases before modifying result or any caller storage.
 for(unsigned i=0;i<6;++i)for(unsigned j=0;j<i;++j)if(!separate(spans[i].p,spans[i].n,spans[j].p,spans[j].n))return false;
 result={};auto&r=result.rpc;r.startNs=io.nowNs();
 if(!origin(prior)||!RTXRootRpc169::retained(t)){R::fail(r,R::Prefix,"root-origin");return false;}
 r.validated=true;r.txWriter=prior.txWriter;r.txReader=prior.txReader;r.rxReader=prior.rxReader;r.rxProducer=prior.rxProducer;
 r.rxSequence=prior.rxSequence;r.initialReader=r.rxReader;r.initialSequence=r.rxSequence;
 if(!R::tick(io,r)||!R::geometry(io,r,scratch))return false;
 if(!io.claim()){R::fail(r,R::Replay,"root-already-attempted");return false;}
 r.attempted=r.prefixConsumed=true;
 if(!RTXRootRpc169::request(t,prior.txWriter,request,4096)){R::fail(r,R::Parameters,"root-request");return false;}
 if(!R::send(io,r,request,scratch)||!receive(io,t,result,records,scratch)||!R::tick(io,r))return false;
 r.completed=1;r.passed=true;
 if(!verify(t,prior,request,records,result,scratch)){R::fail(r,R::Prefix,"root-final-journal");return false;}
 result.verified=true;r.status="owned-root-acknowledged-retained";return true;
}
}
