#pragma once
#include "ExternalVAS.hpp"
#include "../../driver/GSPComputePrepProtocol.hpp"

// Five setup transactions inside the same native execution owner. The caller
// must first verify the complete golden transcript and stage fixed backing.
// IO.externalClaim/Directory/Consumed/Finish bind this to fresh pinned memory.
namespace ExternalSetup {
namespace E=ExternalVAS;namespace R=GSPComputePrep;
struct Result {E::Directory directory=E::LegacyDirectory;R::Result rpc;E::Reply vas;unsigned directoryChecks=0;bool directoryAcknowledged=false;};
inline bool separate(const void *a,size_t an,const void *b,size_t bn){
 const uintptr_t x=reinterpret_cast<uintptr_t>(a),y=reinterpret_cast<uintptr_t>(b);
 return a&&b&&x<=UINTPTR_MAX-an&&y<=UINTPTR_MAX-bn&&!(x<y+bn&&y<x+an);
}
inline bool origin(const R::Result &p){
 return p.passed&&!p.failure&&p.sent==5&&p.completed==5&&p.txWriter==E::FirstSequence&&p.txReader==E::FirstSequence&&
  p.count>=5&&p.count<=R::MaxRecords&&p.pages>=p.count&&p.pages<=R::MaxPages&&p.bytes==p.pages*4096&&
  p.rxReader<63&&p.rxProducer<63&&p.rxSequence<=~0U-R::MaxRecords;
}
inline bool verify(const R::Result &prior,const unsigned char *requests,const unsigned char *records,
 const Result &result,unsigned char *scratch){
 const auto &r=result.rpc;
 if(!E::validDirectory(result.directory)||!origin(prior)||!requests||!records||!scratch||!r.validated||!r.attempted||!r.prefixConsumed||!r.passed||r.failure||r.sent!=E::Steps||r.completed!=E::Steps||
    r.doorbells!=E::Steps||r.step!=E::Steps-1||r.rxProducer>=63||r.elapsedNs>=R::BudgetNs||r.ticks>R::MaxTicks||
    r.lastFunction!=54||r.lastResult||r.lastParamStatus||
    r.txWriter!=E::FinalProducer||r.txReader!=E::FinalProducer||r.count<E::Steps||r.count>R::MaxRecords||r.pages>R::MaxPages||
    r.bytes!=r.pages*4096||r.consumerWrites!=r.count||r.initialReader!=prior.rxReader||r.initialSequence!=prior.rxSequence||
    r.rxSequence!=r.initialSequence+r.count||r.rxReader!=(r.initialReader+r.pages)%63||
    result.directoryChecks!=2||!result.directoryAcknowledged||!result.vas.accepted)return false;
 if(!separate(requests,E::RequestBytes,scratch,4096)||!separate(records,r.bytes,scratch,4096)||
    !separate(&prior,sizeof(prior),scratch,4096)||!separate(&result,sizeof(result),scratch,4096))return false;
 unsigned offset=0,step=0,pages=0;E::Reply va;
 for(unsigned i=0;i<r.count;++i){const auto &row=r.records[i];GSPInitEvents::Record packet;
  if(step>=E::Steps||row.offset!=offset||row.bytes>r.bytes-offset||row.step!=step||row.sequence!=r.initialSequence+i||
     row.slot!=(r.initialReader+pages)%63||!GSPInitEvents::decode(records+offset,row.bytes,row.sequence,packet)||
     row.function!=packet.function||row.result!=packet.result||row.payload!=packet.payloadBytes)return false;
  if(packet.function==E::function(step)){
   E::Reply parsed;if(!E::request(step,scratch,4096,result.directory)||!E::reply(step,row.sequence,records+offset,row.bytes,parsed))return false;
   for(unsigned j=0;j<4096;++j)if(requests[step*4096+j]!=scratch[j])return false;
   if(step==3)va=parsed;++step;
  }else if((result.directory.flags==9&&packet.function==0x1020)||!R::asyncSupported(records+offset,packet))return false;
  offset+=row.bytes;pages+=row.bytes/4096;
 }
 return step==E::Steps&&offset==r.bytes&&pages==r.pages&&va.accepted&&va.base==result.vas.base&&va.size==result.vas.size&&
  va.internalLo==result.vas.internalLo&&va.internalHi==result.vas.internalHi&&va.bigPage==result.vas.bigPage;
}
template<class IO>bool receive(IO &io,Result &result,unsigned char *records,unsigned char *scratch){
 auto &r=result.rpc;const auto started=r.elapsedNs;
 while(R::tick(io,r)){
  if(r.elapsedNs-started>=R::ReplyBudgetNs){R::fail(r,R::Timeout,"external-va-reply-timeout");return false;}
  ++r.polls;if(!R::geometry(io,r,scratch))return false;
  const unsigned available=(r.rxProducer+63-r.rxReader)%63;
  if(!available){io.delayUs(100);continue;}
  if(r.count>=R::MaxRecords||r.pages>=R::MaxPages){R::fail(r,R::Capacity,"external-va-evidence-capacity");return false;}
  const unsigned slot=r.rxReader;auto *raw=records+r.bytes;
  if(!R::read(io,r,0x42000+slot*4096,raw,4096))return false;
  const unsigned pages=R::get32(raw+40);
  if(!pages||pages>16||pages>R::MaxPages-r.pages){R::fail(r,R::Geometry,"external-va-reply-size");return false;}
  if(pages>available){io.delayUs(100);continue;}
  for(unsigned i=1;i<pages;++i)if(!R::read(io,r,0x42000+((slot+i)%63)*4096,raw+i*4096,4096))return false;
  if(!R::import(io,r))return false;
  for(unsigned i=0;i<pages;++i){
   if(!R::read(io,r,0x42000+((slot+i)%63)*4096,scratch,4096))return false;
   for(unsigned j=0;j<4096;++j)if(raw[i*4096+j]!=scratch[j]){R::fail(r,R::Changed,"external-va-reply-changed");return false;}
  }
  if(!R::geometry(io,r,scratch))return false;
  GSPInitEvents::Record packet;
  if(!GSPInitEvents::decode(raw,pages*4096,r.rxSequence,packet)){R::fail(r,R::Frame,"external-va-frame");return false;}
  r.records[r.count++]={r.bytes,pages*4096,packet.function,packet.result,r.rxSequence,packet.payloadBytes,r.step,slot,unsigned(r.elapsedNs/1000)};
  r.pages+=pages;r.bytes+=pages*4096;++r.rxSequence;r.lastFunction=packet.function;r.lastResult=packet.result;
  const bool expected=packet.function==E::function(r.step);
  if(expected){
   r.lastParamStatus=packet.function==103&&packet.payloadBytes>=20?R::get32(raw+96):0;
   if(packet.result){R::fail(r,R::RpcResult,"external-va-rm-result");return false;}
   E::Reply parsed;if(!E::reply(r.step,packet.sequence,raw,pages*4096,parsed)){R::fail(r,R::Parameters,"external-va-reply-parameters");return false;}
   if(r.step==3)result.vas=parsed;if(r.step==4)result.directoryAcknowledged=true;
  }else if(!R::asyncSupported(raw,packet)){R::fail(r,R::Unexpected,"external-va-unsupported-event");return false;}
  if(!R::consume(io,r,(slot+pages)%63,scratch))return false;
  if(result.directory.flags==9&&packet.function==0x1020){R::fail(r,R::Unexpected,"initial-owned-root-diagnostic-event");return false;}
  if(expected){
   if(!R::geometry(io,r,scratch))return false;
   if(r.txReader!=r.txWriter){R::fail(r,R::Progress,"external-va-command-not-consumed");return false;}
   if(!io.externalConsumed(r.step)){R::fail(r,R::Owner,"external-va-consumed-owner");return false;}
   return true;
  }
 }return false;
}
template<class IO>bool execute(IO &io,const R::Result &prior,unsigned char *records,unsigned char *requests,unsigned char *scratch,Result &result,E::Directory directory=E::LegacyDirectory){
 struct Span{const void *p;size_t n;};const Span in[]={{&prior,sizeof(prior)}};
 const Span out[]={{records,R::MaxBytes},{requests,E::RequestBytes},{scratch,4096}};
 for(const auto &x:in)if(x.p&&!separate(x.p,x.n,&result,sizeof(result)))return false;
 for(const auto &x:out)if(x.p&&!separate(x.p,x.n,&result,sizeof(result)))return false;
 result={};result.directory=directory;auto &r=result.rpc;r.startNs=io.nowNs();
 for(const auto &x:out)for(const auto &y:in)if(!separate(x.p,x.n,y.p,y.n)){R::fail(r,R::Owner,"external-va-storage-overlap");return false;}
 for(unsigned i=0;i<3;++i)for(unsigned j=i+1;j<3;++j)if(!separate(out[i].p,out[i].n,out[j].p,out[j].n)){R::fail(r,R::Owner,"external-va-output-overlap");return false;}
 if(!E::validDirectory(directory)||!origin(prior)){R::fail(r,R::Prefix,"external-va-origin");return false;}
 r.validated=true;r.txWriter=r.txReader=E::FirstSequence;r.rxReader=prior.rxReader;r.rxProducer=prior.rxProducer;
 r.rxSequence=prior.rxSequence;r.initialReader=r.rxReader;r.initialSequence=r.rxSequence;
 if(!R::tick(io,r)||!R::geometry(io,r,scratch))return false;
 if(!io.externalClaim()){R::fail(r,R::Replay,"external-va-already-attempted");return false;}r.attempted=r.prefixConsumed=true;
 if(!io.externalDirectory()){R::fail(r,R::Readback,"external-va-directory-precondition");return false;}++result.directoryChecks;
 for(unsigned step=0;step<E::Steps;++step){r.step=step;
  if(!E::request(step,requests+step*4096,4096,directory)){R::fail(r,R::Parameters,"external-va-request");return false;}
  if(!R::send(io,r,requests+step*4096,scratch)||!receive(io,result,records,scratch)||!R::tick(io,r))return false;++r.completed;
 }
 if(!io.externalDirectory()){R::fail(r,R::Readback,"external-va-directory-after-ack");return false;}++result.directoryChecks;
 if(!R::tick(io,r))return false;r.passed=true;
 if(!verify(prior,requests,records,result,scratch)){r.passed=false;R::fail(r,R::Prefix,"external-va-final-journal");return false;}
 if(!io.externalFinish()){r.passed=false;R::fail(r,R::Owner,"external-va-finish-owner");return false;}
 r.status="external-va-directory-acknowledged-retained";return true;
}
}
