#pragma once
#include "ChannelCodec.hpp"
#include "../../../driver/GSPPageTablesRMProtocol.hpp"

// Portable queue executor candidate. The Mac owner/staging adapter does not yet
// exist. Every gate below must be native-owned; none is a userspace permission.
namespace ChannelTransactions {
namespace R=GSPComputePrep;
namespace C=ChannelCodec;
struct Result {R::Result rpc;C::Plan context;unsigned channelId=~0U,subdeviceMask=~0U;bool contextPrepared=false,contextPreparationAttempted=false;};
inline bool prefix(const R::Result &prep,const unsigned char *prepBytes,const R::Result &pd,const unsigned char *pdBytes){
  if(!GSPPageTablesRM::prefix(prep,prepBytes)||!pdBytes||!pd.passed||pd.failure||pd.completed!=1||pd.sent!=1||
     pd.txWriter!=9||pd.txReader!=9||!pd.count||pd.count>R::MaxRecords||!pd.pages||pd.pages>R::MaxPages||pd.bytes!=pd.pages*R::Page||
     pd.initialReader!=prep.rxReader||pd.initialSequence!=prep.rxSequence||pd.rxReader>=63||pd.rxProducer>=63||
     pd.rxSequence>~0U-R::MaxRecords)return false;
  unsigned offset=0,completed=0;
  for(unsigned i=0;i<pd.count;++i){const auto &row=pd.records[i];GSPInitEvents::Record decoded;
    if(row.offset!=offset||offset>pd.bytes||row.bytes>pd.bytes-offset||row.sequence!=pd.initialSequence+i||row.step!=0||
       !GSPInitEvents::decode(pdBytes+offset,row.bytes,row.sequence,decoded))return false;
    if(decoded.function==76){if(completed||!GSPPageTables::replyIdentity(pdBytes+offset,decoded))return false;++completed;}
    else if(completed||!R::asyncSupported(pdBytes+offset,decoded))return false;
    offset+=row.bytes;
  }
  return completed==1&&offset==pd.bytes&&pd.rxSequence==pd.initialSequence+pd.count&&pd.rxReader==(pd.initialReader+pd.pages)%63;
}
template<class IO>bool receive(IO &io,Result &result,unsigned char *out,unsigned char *scratch){
  auto &r=result.rpc;const auto started=r.elapsedNs;
  while(R::tick(io,r)){
    if(r.elapsedNs-started>=R::ReplyBudgetNs){R::fail(r,R::Timeout,"channel-rm-reply-timeout");return false;}
    ++r.polls;if(!R::geometry(io,r,scratch))return false;
    const unsigned available=(r.rxProducer+63-r.rxReader)%63;
    if(!available){io.delayUs(100);continue;}
    if(r.count>=R::MaxRecords||r.pages>=R::MaxPages){R::fail(r,R::Capacity,"channel-evidence-capacity");return false;}
    const unsigned slot=r.rxReader;auto *p=out+r.bytes;
    if(!R::read(io,r,0x42000+slot*R::Page,p,R::Page))return false;
    const unsigned pages=R::get32(p+40);
    if(!pages||pages>16||pages>R::MaxPages-r.pages){R::fail(r,R::Geometry,"channel-reply-size");return false;}
    if(pages>available){io.delayUs(100);continue;}
    for(unsigned i=1;i<pages;++i)if(!R::read(io,r,0x42000+((slot+i)%63)*R::Page,p+i*R::Page,R::Page))return false;
    if(!R::import(io,r))return false;
    for(unsigned i=0;i<pages;++i){
      if(!R::read(io,r,0x42000+((slot+i)%63)*R::Page,scratch,R::Page))return false;
      for(unsigned j=0;j<R::Page;++j)if(scratch[j]!=p[i*R::Page+j]){R::fail(r,R::Changed,"channel-reply-changed");return false;}
    }
    if(!R::geometry(io,r,scratch))return false;
    GSPInitEvents::Record decoded;
    if(!GSPInitEvents::decode(p,pages*R::Page,r.rxSequence,decoded)){R::fail(r,R::Frame,"channel-reply-framing");return false;}
    r.records[r.count++]={r.bytes,pages*R::Page,decoded.function,decoded.result,r.rxSequence,decoded.payloadBytes,r.step,slot,unsigned(r.elapsedNs/1000)};
    r.pages+=pages;r.bytes+=pages*R::Page;++r.rxSequence;r.lastFunction=decoded.function;r.lastResult=decoded.result;
    const bool expected=decoded.function==C::function(r.step);
    if(expected){
      r.lastParamStatus=decoded.payloadBytes>=20?R::get32(p+(decoded.function==76?92:96)):~0U;
      C::Reply reply;
      if(!C::reply(p,pages*R::Page,decoded.sequence,r.step,result.context,reply)){R::fail(r,R::Parameters,"channel-reply-identity-status-or-backing");return false;}
      if(r.step==0){result.channelId=reply.channelId;result.subdeviceMask=reply.subdeviceMask;}
      if(r.step==1&&!C::plan(p+104,C::GRBytes,result.context)){R::fail(r,R::Parameters,"post-channel-gr-plan-invalid");return false;}
    }else if(!R::asyncSupported(p,decoded)){R::fail(r,R::Unexpected,"channel-unsupported-event");return false;}
    if(!R::consume(io,r,(slot+pages)%63,scratch))return false;
    if(expected){
      if(!R::geometry(io,r,scratch))return false;
      if(r.txReader!=r.txWriter){R::fail(r,R::Progress,"channel-reply-before-command-consumption");return false;}
      return true;
    }
  }
  return false;
}
template<class IO>void execute(IO &io,const R::Result &prep,const unsigned char *prepBytes,
  const R::Result &pd,const unsigned char *pdBytes,unsigned char *out,unsigned char *requests,unsigned char *scratch,Result &result){
  result={};auto &r=result.rpc;r.startNs=io.nowNs();
  if(!out||!requests||!scratch||out==requests||out==scratch||requests==scratch||!io.ready()||!io.ringReady()){
    R::fail(r,R::Owner,"channel-storage-owner-or-ring-not-ready");return;
  }
  if(!prefix(prep,prepBytes,pd,pdBytes)){R::fail(r,R::Prefix,"channel-prefix-invalid");return;}
  r.validated=true;r.txWriter=r.txReader=9;r.rxReader=pd.rxReader;r.rxProducer=pd.rxProducer;
  r.rxSequence=pd.rxSequence;r.initialReader=r.rxReader;r.initialSequence=r.rxSequence;
  if(!R::tick(io,r)||!R::geometry(io,r,scratch))return;
  if(!io.claim()){R::fail(r,R::Replay,"channel-already-attempted");return;}
  r.attempted=r.prefixConsumed=true;
  for(unsigned step=0;step<C::Steps;++step){
    r.step=step;
    if(step==2){
      if(!C::planValid(result.context)||!R::tick(io,r)){if(!r.failure)R::fail(r,R::Parameters,"channel-context-plan-invalid");return;}
      // Called only after the actual channel reply AND fresh GR reply have both
      // been validated and consumed. This adapter must stage/read back backing
      // and mappings, invalidate the PDB, and retain all memory on uncertainty.
      result.contextPreparationAttempted=true;
      if(!io.prepareContext(result.context)){R::fail(r,R::Readback,"channel-context-backing-not-prepared");return;}
      if(!R::tick(io,r)||!io.contextReady(result.context)){if(!r.failure)R::fail(r,R::Readback,"channel-context-state-not-ready");return;}
      result.contextPrepared=true;
    }
    if(!C::request(step,result.context,requests+step*R::Page,R::Page)){R::fail(r,R::Parameters,"channel-canonical-request-invalid");return;}
    if(!R::send(io,r,requests+step*R::Page,scratch)||!ChannelTransactions::receive(io,result,out,scratch)||!R::tick(io,r))return;
    ++r.completed;
  }
  r.passed=true;r.status="channel-context-compute-copy-rm-accepted";
}
}
