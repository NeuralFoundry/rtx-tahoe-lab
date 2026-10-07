#pragma once
#include "GSPInitEventProtocol.hpp"
// Three fixed NVIDIA570.144 RM allocations. Internal native driver flow only;
// no caller-selected handles, RPC function, payload, pointer or MMIO operation.
namespace GSPRm {
using U64=unsigned long long;
using GSPContentSeal::get32;
constexpr unsigned Page=4096,MaxPages=32,MaxRecords=16,MaxBytes=Page*MaxPages;
constexpr unsigned Requests=3,RequestBytes=Requests*Page,MaxTicks=150000;
constexpr U64 BudgetNs=15000000000ULL,ReplyBudgetNs=5000000000ULL;
constexpr unsigned Client=0xc1e00004U,RootObject=0xcf000000U,Device=0xcf000001U,Subdevice=0xcf000002U;
constexpr unsigned Doorbell=0x110c00;
enum Failure:unsigned{None,Owner,Prefix,Read,Write,Import,Publish,Header,Progress,Geometry,
  Changed,Frame,Timeout,Clock,Unexpected,RpcResult,Parameters,Capacity,Replay,Bell,Readback};
struct Record{unsigned offset=0,bytes=0,function=0,result=0,sequence=0,payload=0,step=0,slot=0,elapsedUs=0;};
struct Result{
  Failure failure=None;const char *status="not-run";
  bool validated=false,attempted=false,passed=false,prefixConsumed=false;
  unsigned step=0,completed=0,sent=0,doorbells=0,count=0,pages=0,bytes=0;
  unsigned txWriter=2,txReader=2,rxReader=0,rxProducer=0,rxSequence=0;
  unsigned initialReader=0,initialSequence=0,ticks=0,polls=0,imports=0,reads=0,writes=0,publishes=0,consumerWrites=0;
  unsigned lastFunction=0,lastResult=~0U,lastParamStatus=~0U,lastAddress=0,lastValue=0;
  U64 startNs=0,elapsedNs=0;Record records[MaxRecords];
};
inline void put32(unsigned char*p,unsigned v){for(unsigned i=0;i<4;++i)p[i]=static_cast<unsigned char>(v>>(8*i));}
inline unsigned paramSize(unsigned step){return step==0?120:step==1?56:step==2?4:0;}
inline unsigned object(unsigned step){return step==0?RootObject:step==1?Device:Subdevice;}
inline unsigned parent(unsigned step){return step==0?0:step==1?Client:Device;}
inline unsigned klass(unsigned step){return step==0?0:step==1?0x80:0x2080;}
inline bool request(unsigned step,unsigned char *out){
  if(step>=Requests||!out)return false;
  for(unsigned i=0;i<Page;++i)out[i]=0;
  put32(out+36,step+2);put32(out+40,1);put32(out+48,0x03000000);put32(out+52,0x43505256);
  put32(out+56,64+paramSize(step));put32(out+60,103);put32(out+64,~0U);put32(out+68,~0U);
  put32(out+80,Client);put32(out+84,parent(step));put32(out+88,object(step));put32(out+92,klass(step));
  put32(out+100,paramSize(step));
  // NV0080 hClientShare at offset4; other alloc parameters explicitly zero.
  if(step==1)put32(out+116,Client);
  unsigned sum=0;for(unsigned i=0;i<((112+paramSize(step)+7)&~7U);i+=4)sum^=get32(out+i);
  put32(out+32,sum);return true;
}
inline bool asyncSupported(const unsigned char*p,const GSPInitEvents::Record &r){
  if(r.result)return false;
  if(r.function==0x1020)return r.payloadBytes==1212; // Retain/journal; do not declare diagnostics resolved.
  if(r.function==0x100c)return r.payloadBytes>=8 && get32(p+84)==r.payloadBytes-8;
  return false;
}
inline bool prefix(const GSPInitEvents::Result&r,const unsigned char *bytes){
  if(!bytes||!r.passed||!r.initDone||r.stop!=GSPInitEvents::InitDone||r.startSlot!=3||r.startSequence!=2||
     !r.count||r.count>GSPInitEvents::MaxRecords||r.pages>62||r.bytes!=r.pages*Page)return false;
  unsigned offset=0;
  for(unsigned i=0;i<r.count;++i){
    const auto &row=r.records[i];GSPInitEvents::Record decoded;
    if(row.offset!=offset||row.bytes>r.bytes-offset||
       !GSPInitEvents::decode(bytes+offset,row.bytes,i+2,decoded))return false;
    if(i+1==r.count){if(!(decoded.flags&1))return false;}
    else if(!asyncSupported(bytes+offset,decoded))return false;
    offset+=row.bytes;
  }
  return offset==r.bytes;
}
inline void fail(Result&r,Failure f,const char*status){r.failure=f;r.status=status;r.passed=false;}
template<class IO>bool tick(IO&io,Result&r){
  const U64 now=io.nowNs();
  if(now<r.startNs||now-r.startNs<r.elapsedNs){fail(r,Clock,"rm-clock-regressed");return false;}
  r.elapsedNs=now-r.startNs;
  if(!io.ready()){fail(r,Owner,"rm-owner-lost");return false;}
  if(++r.ticks>MaxTicks||r.elapsedNs>=BudgetNs){fail(r,Timeout,"rm-deadline");return false;}
  return true;
}
template<class IO>bool import(IO&io,Result&r){if(!tick(io,r))return false;++r.imports;if(io.import())return true;fail(r,Import,"rm-import-failed");return false;}
template<class IO>bool read(IO&io,Result&r,unsigned off,unsigned char*p,unsigned n){
  if(!tick(io,r))return false;++r.reads;r.lastAddress=off;
  if(io.read(off,p,n)){if(n>=4)r.lastValue=get32(p);return true;}fail(r,Read,"rm-read-failed");return false;
}
template<class IO>bool write(IO&io,Result&r,unsigned off,const unsigned char*p,unsigned n){
  if(!tick(io,r))return false;++r.writes;r.lastAddress=off;r.lastValue=get32(p);
  if(io.write(off,p,n))return true;fail(r,Write,"rm-write-failed");return false;
}
template<class IO>bool publish(IO&io,Result&r){if(!tick(io,r))return false;++r.publishes;if(io.publish())return true;fail(r,Publish,"rm-publish-failed");return false;}
template<class IO>bool geometry(IO&io,Result&r,unsigned char*scratch){
  if(!import(io,r)||!read(io,r,0x1000,scratch,36))return false;
  unsigned h[8];for(unsigned i=0;i<8;++i)h[i]=get32(scratch+i*4);
  if(!GSPFirstStatus::header(h)||h[4]!=r.txWriter||get32(scratch+32)!=r.rxReader){fail(r,Header,"rm-command-header-or-host-index");return false;}
  if(!read(io,r,0x41000,scratch,36))return false;
  for(unsigned i=0;i<8;++i)h[i]=get32(scratch+i*4);
  if(!GSPFirstStatus::header(h)){fail(r,Header,"rm-status-header-invalid");return false;}
  const unsigned commandReader=get32(scratch+32);
  // No prior command remains outstanding when a reply completes. During a
  // wait, firmware may consume exactly the single newly published command.
  if(commandReader>r.txWriter||commandReader<r.txReader||r.txWriter-commandReader>1){fail(r,Progress,"rm-command-consumer-progress");return false;}
  const unsigned oldDistance=(r.rxProducer+63-r.rxReader)%63,newDistance=(h[4]+63-r.rxReader)%63;
  if(newDistance<oldDistance){fail(r,Progress,"rm-status-producer-regressed");return false;}
  r.txReader=commandReader;r.rxProducer=h[4];return true;
}
template<class IO>bool consume(IO&io,Result&r,unsigned newReader,unsigned char*scratch){
  put32(scratch,newReader);++r.consumerWrites;
  if(!write(io,r,0x1020,scratch,4))return false;
  // Write may have become visible even when subsequent sync fails. Record it.
  r.rxReader=newReader;
  if(!publish(io,r)||!import(io,r)||!read(io,r,0x1020,scratch,4))return false;
  if(get32(scratch)!=newReader){fail(r,Readback,"rm-consumer-readback");return false;}
  return true;
}
template<class IO>bool send(IO&io,Result&r,const unsigned char*record,unsigned char*scratch){
  if(!geometry(io,r,scratch))return false;
  if(r.txReader!=r.txWriter){fail(r,Progress,"rm-command-queue-not-drained");return false;}
  const unsigned off=0x2000+r.txWriter*Page;
  if(!write(io,r,off,record,Page)||!publish(io,r)||!import(io,r)||!read(io,r,off,scratch,Page))return false;
  for(unsigned i=0;i<Page;++i)if(record[i]!=scratch[i]){fail(r,Readback,"rm-request-readback");return false;}
  if(!geometry(io,r,scratch))return false;
  put32(scratch,r.txWriter+1);
  if(!write(io,r,0x1010,scratch,4))return false;
  ++r.txWriter;++r.sent;
  if(!publish(io,r)||!import(io,r)||!read(io,r,0x1010,scratch,4))return false;
  if(get32(scratch)!=r.txWriter){fail(r,Readback,"rm-producer-readback");return false;}
  if(!tick(io,r))return false;
  ++r.doorbells;r.lastAddress=Doorbell;r.lastValue=0;
  if(!io.doorbell()){fail(r,Bell,"rm-doorbell-failed");return false;}
  return true;
}
inline bool reply(const unsigned char*p,const GSPInitEvents::Record&row,Result&r){
  r.lastResult=row.result;
  if(row.result){fail(r,RpcResult,"rm-rpc-returned-error");return false;}
  if(row.payloadBytes!=32+paramSize(r.step)||get32(p+68)||get32(p+72)||get32(p+80)!=Client||get32(p+84)!=parent(r.step)||
     get32(p+88)!=object(r.step)||get32(p+92)!=klass(r.step)||get32(p+100)!=paramSize(r.step)||
     get32(p+104)||get32(p+108)){fail(r,Parameters,"rm-reply-identity-or-parameters");return false;}
  r.lastParamStatus=get32(p+96);
  if(r.lastParamStatus){fail(r,RpcResult,"rm-allocation-status-failed");return false;}
  return true;
}
template<class IO>bool receive(IO&io,Result&r,unsigned char*out,unsigned char*scratch){
  const U64 started=r.elapsedNs;
  while(tick(io,r)){
    if(r.elapsedNs-started>=ReplyBudgetNs){fail(r,Timeout,"rm-reply-timeout");return false;}
    ++r.polls;if(!geometry(io,r,scratch))return false;
    const unsigned available=(r.rxProducer+63-r.rxReader)%63;
    if(!available){io.delayUs(100);continue;}
    if(r.count>=MaxRecords||r.pages>=MaxPages){fail(r,Capacity,"rm-evidence-capacity");return false;}
    const unsigned slot=r.rxReader;unsigned char*p=out+r.bytes;
    if(!read(io,r,0x42000+slot*Page,p,Page))return false;
    const unsigned pages=get32(p+40);
    if(!pages||pages>16||pages>MaxPages-r.pages){fail(r,Geometry,"rm-reply-record-size");return false;}
    if(pages>available){io.delayUs(100);continue;}
    for(unsigned i=1;i<pages;++i)if(!read(io,r,0x42000+((slot+i)%63)*Page,p+i*Page,Page))return false;
    if(!import(io,r))return false;
    for(unsigned i=0;i<pages;++i){
      if(!read(io,r,0x42000+((slot+i)%63)*Page,scratch,Page))return false;
      for(unsigned j=0;j<Page;++j)if(scratch[j]!=p[i*Page+j]){fail(r,Changed,"rm-reply-changed");return false;}
    }
    if(!geometry(io,r,scratch))return false;
    GSPInitEvents::Record decoded;
    if(!GSPInitEvents::decode(p,pages*Page,r.rxSequence,decoded)){fail(r,Frame,"rm-reply-framing");return false;}
    r.records[r.count++]={r.bytes,pages*Page,decoded.function,decoded.result,r.rxSequence,decoded.payloadBytes,r.step,slot,unsigned(r.elapsedNs/1000)};
    r.pages+=pages;r.bytes+=pages*Page;++r.rxSequence;r.lastFunction=decoded.function;r.lastResult=decoded.result;
    const bool expected=decoded.function==103;
    if(!expected&&!asyncSupported(p,decoded)){fail(r,Unexpected,"rm-unsupported-event");return false;}
    if(expected&&!reply(p,decoded,r))return false;
    if(!consume(io,r,(slot+pages)%63,scratch))return false;
    if(expected){
      if(!geometry(io,r,scratch))return false;
      if(r.txReader!=r.txWriter){fail(r,Progress,"rm-reply-before-command-consumption");return false;}
      return true;
    }
  }
  return false;
}
template<class IO>void execute(IO&io,const GSPInitEvents::Result&init,const unsigned char*initBytes,
  unsigned char*out,unsigned char*requests,unsigned char*scratch,Result&r){
  r=Result{};r.startNs=io.nowNs();
  if(!out||!requests||!scratch||out==requests||out==scratch||requests==scratch||!io.ready()){
    fail(r,Owner,"rm-storage-or-owner-unavailable");return;
  }
  if(!prefix(init,initBytes)){fail(r,Prefix,"rm-init-prefix-invalid");return;}
  r.validated=true;r.initialReader=(init.startSlot+init.pages)%63;r.initialSequence=init.startSequence+init.count;
  r.rxReader=3;r.rxProducer=init.producer;r.rxSequence=r.initialSequence;
  if(!tick(io,r)||!geometry(io,r,scratch))return;
  if(r.txReader!=2){fail(r,Progress,"rm-startup-commands-not-consumed");return;}
  if(!io.claim()){fail(r,Replay,"rm-experiment-already-attempted");return;}
  r.attempted=true;
  if(!consume(io,r,r.initialReader,scratch))return;
  r.prefixConsumed=true;
  for(unsigned step=0;step<Requests;++step){
    r.step=step;
    if(!request(step,requests+step*Page)){fail(r,Parameters,"rm-canonical-request-invalid");return;}
    if(!send(io,r,requests+step*Page,scratch)||!receive(io,r,out,scratch))return;
    ++r.completed;
  }
  if(!tick(io,r))return;
  r.passed=true;r.status="rm-root-device-subdevice-created";
}
}
