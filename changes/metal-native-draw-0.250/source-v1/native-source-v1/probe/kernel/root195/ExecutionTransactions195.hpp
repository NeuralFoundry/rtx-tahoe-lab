#pragma once
#include "../changes/gsp-submit-0.24/runtime/ExecutionTransactions.hpp"
namespace ExecutionTransactions195 {
namespace E=ExecutionCodec;namespace P=ExecutionPlan;namespace C=ChannelCodec;namespace G=ChannelTransactions;namespace R=GSPComputePrep;
using Result=ExecutionTransactions::Result;

inline bool samePlan(const C::Plan &a,const C::Plan &b){
  if(!C::planValid(a)||!C::planValid(b))return false;
  for(unsigned i=0;i<9;++i){const auto &x=a.buffers[i],&y=b.buffers[i];
    if(x.id!=y.id||x.kind!=y.kind||x.bytes!=y.bytes||x.allocated!=y.allocated||x.alignment!=y.alignment||x.physical!=y.physical||
      x.virtualAddress!=y.virtualAddress||x.usePhysical!=y.usePhysical||x.useVirtual!=y.useVirtual)return false;
  }return true;
}
inline bool prefix(const R::Result &prep,const unsigned char *prepBytes,const R::Result &pd,const unsigned char *pdBytes,
 const G::Result &golden,const unsigned char *goldenBytes){
  const auto &r=golden.rpc;
  if(prep.failure||pd.failure||!G::prefix(prep,prepBytes,pd,pdBytes)||!goldenBytes||!r.passed||r.failure||r.completed!=5||r.sent!=5||
     r.txWriter!=14||r.txReader!=14||!r.count||r.count>R::MaxRecords||!r.pages||r.pages>R::MaxPages||r.bytes!=r.pages*4096||
     r.initialReader!=pd.rxReader||r.initialSequence!=pd.rxSequence||r.rxReader>=63||r.rxProducer>=63||r.rxSequence>~0U-R::MaxRecords||
     golden.channelId!=3||golden.subdeviceMask>1||!golden.contextPrepared||!golden.contextPreparationAttempted||!P::goldenFits(golden.context))return false;
  C::Plan derived;unsigned offset=0,step=0,pages=0;
  for(unsigned i=0;i<r.count;++i){const auto &row=r.records[i];GSPInitEvents::Record decoded;
    if(step>=5||row.offset!=offset||offset>r.bytes||row.bytes>r.bytes-offset||row.sequence!=r.initialSequence+i||row.step!=step||
       row.slot!=(r.initialReader+pages)%63||!GSPInitEvents::decode(goldenBytes+offset,row.bytes,row.sequence,decoded)||
       row.function!=decoded.function||row.result!=decoded.result||row.payload!=decoded.payloadBytes)return false;
    if(decoded.function==C::function(step)){
      C::Reply reply;if(!C::reply(goldenBytes+offset,row.bytes,row.sequence,step,derived,reply))return false;
      if(step==0&&(reply.channelId!=golden.channelId||reply.subdeviceMask!=golden.subdeviceMask))return false;
      if(step==1&&!C::plan(goldenBytes+offset+104,1664,derived))return false;
      ++step;
    }else if(!R::asyncSupported(goldenBytes+offset,decoded))return false;
    offset+=row.bytes;pages+=row.bytes/4096;
  }
  return step==5&&offset==r.bytes&&pages==r.pages&&r.rxSequence==r.initialSequence+r.count&&r.rxReader==(r.initialReader+pages)%63&&samePlan(derived,golden.context);
}
template<class IO>bool receive(IO &io,const G::Result &golden,Result &result,unsigned char *out,unsigned char *scratch){
  auto &r=result.rpc;const auto started=r.elapsedNs;
  while(R::tick(io,r)){
    if(r.elapsedNs-started>=R::ReplyBudgetNs){R::fail(r,R::Timeout,"execution-rm-reply-timeout");return false;}
    ++r.polls;if(!R::geometry(io,r,scratch))return false;
    const unsigned available=(r.rxProducer+63-r.rxReader)%63;
    if(!available){io.delayUs(100);continue;}
    if(r.count>=R::MaxRecords||r.pages>=R::MaxPages){R::fail(r,R::Capacity,"execution-evidence-capacity");return false;}
    const unsigned slot=r.rxReader;auto *p=out+r.bytes;
    if(!R::read(io,r,0x42000+slot*4096,p,4096))return false;
    const unsigned pages=R::get32(p+40);
    if(!pages||pages>16||pages>R::MaxPages-r.pages){R::fail(r,R::Geometry,"execution-reply-size");return false;}
    if(pages>available){io.delayUs(100);continue;}
    for(unsigned i=1;i<pages;++i)if(!R::read(io,r,0x42000+((slot+i)%63)*4096,p+i*4096,4096))return false;
    if(!R::import(io,r))return false;
    for(unsigned i=0;i<pages;++i){
      if(!R::read(io,r,0x42000+((slot+i)%63)*4096,scratch,4096))return false;
      for(unsigned j=0;j<4096;++j)if(scratch[j]!=p[i*4096+j]){R::fail(r,R::Changed,"execution-reply-changed");return false;}
    }
    if(!R::geometry(io,r,scratch))return false;
    GSPInitEvents::Record decoded;
    if(!GSPInitEvents::decode(p,pages*4096,r.rxSequence,decoded)){R::fail(r,R::Frame,"execution-reply-frame");return false;}
    r.records[r.count++]={r.bytes,pages*4096,decoded.function,decoded.result,r.rxSequence,decoded.payloadBytes,r.step,slot,unsigned(r.elapsedNs/1000)};
    r.pages+=pages;r.bytes+=pages*4096;++r.rxSequence;r.lastFunction=decoded.function;r.lastResult=decoded.result;
    const bool expected=decoded.function==E::function(r.step);
    if(expected){
      r.lastParamStatus=decoded.payloadBytes>=20?R::get32(p+(decoded.function==76?92:96)):~0U;
      E::Reply reply;if(!E::reply(p,pages*4096,decoded.sequence,r.step,golden.context,golden.channelId,result.context,reply)){
        R::fail(r,R::Parameters,"execution-reply-identity-status-or-backing");return false;
      }
      if(r.step==E::ChannelStep){result.channelId=reply.channelId;result.subdeviceMask=reply.subdeviceMask;}
      if(r.step==E::SizeStep)result.context=reply.privatePlan;
      if(r.step==E::FifoStep)result.runlist=reply.runlist;
      if(r.step==E::TokenStep)result.rawToken=reply.rawToken;
      if(r.step==E::GraphicsStep)result.graphicsCaps=reply.graphicsCaps;
    }else if(!R::asyncSupported(p,decoded)){R::fail(r,R::Unexpected,"execution-unsupported-event");return false;}
    if(!R::consume(io,r,(slot+pages)%63,scratch))return false;
    if(expected){
      if(!R::geometry(io,r,scratch))return false;
      if(r.txReader!=r.txWriter){R::fail(r,R::Progress,"execution-reply-before-command-consumption");return false;}
      if(!io.replyConsumed(r.step)){R::fail(r,R::Owner,"execution-consumed-reply-owner");return false;}
      return true;
    }
  }return false;
}
template<class IO>bool execute(IO &io,const R::Result &prep,const unsigned char *prepBytes,const R::Result &pd,const unsigned char *pdBytes,
 const G::Result &golden,const unsigned char *goldenBytes,unsigned char *out,unsigned char *requests,unsigned char *scratch,Result &result,
 unsigned char *externalRecords,unsigned char *externalRequests){
  struct Span{const void *p;size_t bytes;};
  const Span inputs[]={{&prep,sizeof(prep)},{prepBytes,prep.bytes},{&pd,sizeof(pd)},{pdBytes,pd.bytes},{&golden,sizeof(golden)},{goldenBytes,golden.rpc.bytes}};
  const Span outputs[]={{out,R::MaxBytes},{requests,E::RequestBytes},{scratch,4096},{externalRecords,R::MaxBytes},{externalRequests,ExternalVAS::RequestBytes}};
  for(const auto &a:inputs)if(a.p&&!P::disjoint(&result,sizeof(result),a.p,a.bytes))return false;
  for(const auto &a:outputs)if(a.p&&!P::disjoint(&result,sizeof(result),a.p,a.bytes))return false;
  result={};auto &r=result.rpc;r.startNs=io.nowNs();
  for(const auto &a:outputs)for(const auto &b:inputs)if(!P::disjoint(a.p,a.bytes,b.p,b.bytes)){R::fail(r,R::Owner,"execution-storage-overlap");return false;}
  for(unsigned i=0;i<5;++i)for(unsigned j=0;j<i;++j)if(!P::disjoint(outputs[i].p,outputs[i].bytes,outputs[j].p,outputs[j].bytes)){R::fail(r,R::Owner,"execution-output-overlap");return false;}
  if(!io.ready()){R::fail(r,R::Owner,"execution-owner-not-ready");return false;}
  if(!prefix(prep,prepBytes,pd,pdBytes,golden,goldenBytes)){R::fail(r,R::Prefix,"execution-prefix-invalid");return false;}
  r.validated=true;r.txWriter=r.txReader=14;r.rxReader=golden.rpc.rxReader;r.rxProducer=golden.rpc.rxProducer;
  r.rxSequence=golden.rpc.rxSequence;r.initialReader=r.rxReader;r.initialSequence=r.rxSequence;
  if(!R::tick(io,r)||!R::geometry(io,r,scratch))return false;
  if(!io.claim()){R::fail(r,R::Replay,"execution-already-attempted");return false;}r.attempted=r.prefixConsumed=true;
  result.fixedPreparationAttempted=true;
  if(!io.prepareFixed()){R::fail(r,R::Readback,"execution-fixed-backing-not-prepared");return false;}
  if(!R::tick(io,r)||!io.ringReady()){if(!r.failure)R::fail(r,R::Readback,"execution-fixed-state-not-ready");return false;}result.fixedPrepared=true;
  if(!io.retainRootJournal(golden.rpc,externalRequests,externalRecords,result.external)){
    R::fail(r,R::Owner,"execution-initial-root-journal-not-retained");return false;
  }
  if(!ExternalSetup::execute(io,golden.rpc,externalRecords,externalRequests,scratch,result.external,io.externalRoot())){
    R::fail(r,R::Prefix,"execution-external-va-setup-failed");return false;
  }
  if(!io.confirmRoot(golden.rpc,externalRequests,externalRecords,result.external,scratch)){
    R::fail(r,R::Prefix,"execution-initial-owned-root-not-confirmed");return false;
  }
  r.txWriter=r.txReader=E::FirstSequence;r.rxReader=result.external.rpc.rxReader;r.rxProducer=result.external.rpc.rxProducer;
  r.rxSequence=result.external.rpc.rxSequence;r.initialReader=r.rxReader;r.initialSequence=r.rxSequence;
  if(!R::tick(io,r)||!R::geometry(io,r,scratch))return false;
  for(unsigned step=0;step<E::Steps;++step){r.step=step;
    if(step==E::PhysicalStep){
      if(!P::valid(result.context,golden.context)||!R::tick(io,r)){if(!r.failure)R::fail(r,R::Parameters,"execution-private-plan-invalid");return false;}
      result.contextPreparationAttempted=true;
      if(!io.prepareContext(result.context)){R::fail(r,R::Readback,"execution-private-backing-not-prepared");return false;}
      if(!R::tick(io,r)||!io.contextReady(result.context)){if(!r.failure)R::fail(r,R::Readback,"execution-private-state-not-ready");return false;}result.contextPrepared=true;
    }
    if(!E::request(step,E::FirstSequence+step,golden.context,golden.channelId,result.context,requests+step*4096,4096)){R::fail(r,R::Parameters,"execution-request-invalid");return false;}
    if(!R::send(io,r,requests+step*4096,scratch)||!ExecutionTransactions195::receive(io,golden,result,out,scratch)||!R::tick(io,r))return false;++r.completed;
  }
  if(!WorkSubmitToken::compose(result.runlist,P::HardwareChannelId,result.rawToken,result.candidate)){R::fail(r,R::Parameters,"execution-token-routing-mismatch");return false;}
  ExecutionTranscript::Result proof;
  if(!ExecutionTranscript::verify(requests,E::RequestBytes,out,r.bytes,r.initialSequence,golden.context,golden.channelId,scratch,proof)||
     proof.records!=r.count||proof.pages!=r.pages||proof.nextSequence!=r.rxSequence||proof.channelId!=result.channelId||proof.subdeviceMask!=result.subdeviceMask||proof.rawToken!=result.rawToken||proof.candidate!=result.candidate||proof.graphicsCaps!=result.graphicsCaps){R::fail(r,R::Prefix,"execution-final-journal-invalid");return false;}
  if(!R::tick(io,r))return false;r.passed=true;r.status="execution-channel-rm-ready-retained";return true;
}
}
