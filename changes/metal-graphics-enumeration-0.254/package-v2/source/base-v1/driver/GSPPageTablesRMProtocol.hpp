#pragma once
#include "GSPPageTablesProtocol.hpp"
namespace GSPPageTablesRM {
namespace R=GSPComputePrep;
using Result=R::Result;
// Revalidate the retained six-reply transcript, including the current VASPACE
// bounds. The claimed software VA reservation is not an RM allocation proof.
inline bool prefix(const R::Result&prep,const unsigned char*bytes){
  if(!bytes||!prep.passed||prep.completed!=6||prep.sent!=6||prep.txWriter!=8||prep.txReader!=8||
     !prep.count||prep.count>R::MaxRecords||!prep.pages||prep.pages>R::MaxPages||prep.bytes!=prep.pages*R::Page||
     prep.rxReader>=63||prep.rxProducer>=63||prep.rxSequence>~0U-R::MaxRecords)return false;
  unsigned offset=0,step=0;bool vas=false;
  for(unsigned i=0;i<prep.count;++i){
    const auto&row=prep.records[i];GSPInitEvents::Record decoded;
    if(row.offset!=offset||offset>prep.bytes||row.bytes>prep.bytes-offset||row.sequence!=prep.initialSequence+i||
       row.step!=step||!GSPInitEvents::decode(bytes+offset,row.bytes,row.sequence,decoded))return false;
    if(decoded.function==R::function(step)){
      Result check;check.step=step;
      if(step>=6||!R::reply(bytes+offset,decoded,check))return false;
      if(step==3)vas=GSPPageTables::vaspaceRange(bytes+offset+112,48);
      ++step;
    }else if(!R::asyncSupported(bytes+offset,decoded))return false;
    offset+=row.bytes;
  }
  return vas&&step==6&&offset==prep.bytes&&prep.rxSequence==prep.initialSequence+prep.count&&
    prep.rxReader==(prep.initialReader+prep.pages)%63;
}
template<class IO>bool receive(IO&io,Result&r,unsigned char*out,unsigned char*scratch){
  const auto started=r.elapsedNs;
  while(R::tick(io,r)){
    if(r.elapsedNs-started>=R::ReplyBudgetNs){R::fail(r,R::Timeout,"page-table-rm-reply-timeout");return false;}
    ++r.polls;if(!R::geometry(io,r,scratch))return false;
    const unsigned available=(r.rxProducer+63-r.rxReader)%63;
    if(!available){io.delayUs(100);continue;}
    if(r.count>=R::MaxRecords||r.pages>=R::MaxPages){R::fail(r,R::Capacity,"page-table-rm-evidence-capacity");return false;}
    const unsigned slot=r.rxReader;unsigned char*p=out+r.bytes;
    if(!R::read(io,r,0x42000+slot*R::Page,p,R::Page))return false;
    const unsigned pages=R::get32(p+40);
    if(!pages||pages>16||pages>R::MaxPages-r.pages){R::fail(r,R::Geometry,"page-table-rm-reply-size");return false;}
    if(pages>available){io.delayUs(100);continue;}
    for(unsigned i=1;i<pages;++i)if(!R::read(io,r,0x42000+((slot+i)%63)*R::Page,p+i*R::Page,R::Page))return false;
    if(!R::import(io,r))return false;
    for(unsigned i=0;i<pages;++i){
      if(!R::read(io,r,0x42000+((slot+i)%63)*R::Page,scratch,R::Page))return false;
      for(unsigned j=0;j<R::Page;++j)if(scratch[j]!=p[i*R::Page+j]){R::fail(r,R::Changed,"page-table-rm-reply-changed");return false;}
    }
    if(!R::geometry(io,r,scratch))return false;
    GSPInitEvents::Record decoded;
    if(!GSPInitEvents::decode(p,pages*R::Page,r.rxSequence,decoded)){R::fail(r,R::Frame,"page-table-rm-framing");return false;}
    r.records[r.count++]={r.bytes,pages*R::Page,decoded.function,decoded.result,r.rxSequence,decoded.payloadBytes,0,slot,unsigned(r.elapsedNs/1000)};
    r.pages+=pages;r.bytes+=pages*R::Page;++r.rxSequence;r.lastFunction=decoded.function;r.lastResult=decoded.result;
    const bool expected=decoded.function==76;
    if(expected){
      r.lastParamStatus=decoded.payloadBytes>=16?R::get32(p+92):~0U;
      if(!GSPPageTables::replyIdentity(p,decoded)){R::fail(r,R::Parameters,"page-table-rm-reply-identity-or-status");return false;}
    }else if(!R::asyncSupported(p,decoded)){R::fail(r,R::Unexpected,"page-table-rm-unsupported-event");return false;}
    if(!R::consume(io,r,(slot+pages)%63,scratch))return false;
    if(expected){
      if(!R::geometry(io,r,scratch))return false;
      if(r.txReader!=r.txWriter){R::fail(r,R::Progress,"page-table-rm-reply-before-command-consumption");return false;}
      return true;
    }
  }
  return false;
}
template<class IO>void execute(IO&io,const R::Result&prep,const unsigned char*prepBytes,
  const GSPPageTables::Result&tables,unsigned char*out,unsigned char*request,unsigned char*scratch,Result&r){
  r=Result{};r.startNs=io.nowNs();
  if(!out||!request||!scratch||out==request||out==scratch||request==scratch||!io.ready()){
    R::fail(r,R::Owner,"page-table-rm-storage-or-owner");return;
  }
  if(!tables.passed||tables.failure||!tables.modified||!tables.windowSaved||tables.windowRestored||
     tables.inspected!=GSPPageTables::Words||tables.written!=GSPPageTables::Words||tables.checked!=GSPPageTables::Words||
     tables.captured!=GSPPageTables::Bytes||!prefix(prep,prepBytes)){
    R::fail(r,R::Prefix,"page-table-rm-prerequisite-invalid");return;
  }
  r.validated=true;r.txWriter=r.txReader=8;r.rxReader=prep.rxReader;r.rxProducer=prep.rxProducer;
  r.rxSequence=prep.rxSequence;r.initialReader=r.rxReader;r.initialSequence=r.rxSequence;
  if(!R::tick(io,r)||!R::geometry(io,r,scratch))return;
  if(!io.claim()){R::fail(r,R::Replay,"page-table-rm-already-attempted");return;}
  r.attempted=r.prefixConsumed=true;
  if(!GSPPageTables::request(request,R::Page)){R::fail(r,R::Parameters,"page-table-rm-canonical-request");return;}
  if(!R::send(io,r,request,scratch)||!GSPPageTablesRM::receive(io,r,out,scratch)||!R::tick(io,r))return;
  r.completed=1;r.passed=true;r.status="server-reserved-pde-control-accepted";
}
}
