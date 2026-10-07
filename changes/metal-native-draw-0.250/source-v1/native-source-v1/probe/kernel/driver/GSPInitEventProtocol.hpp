#pragma once
#include "GSPFirstStatus.hpp"

// Passive collection after a validated prefix has been consumed. readPtr
// stays at startSlot during this observation; at most one wrap is possible.
// No ACK, queue index write, RPC reply, sequencer or arbitrary MMIO exists.
namespace GSPInitEvents {
using U64=unsigned long long;
constexpr unsigned Page=4096, MaxPages=62, MaxRecords=62, MaxBytes=MaxPages*Page;
constexpr unsigned MaxPolls=50000;
// RX header belongs to the peer TX queue. Host status readPtr is in the
// command allocation; status allocation +32 is GSP's command readPtr.
constexpr unsigned HostStatusReadOffset=0x1000+32;
constexpr U64 DurationNs=5000000000ULL;
enum Stop : unsigned { NotRun, InitDone, Sequencer, QueueFull, Deadline, Error };
enum Failure : unsigned { None, Owner, Import, Read, Header, ReaderChanged,
  ProducerRegressed, RecordGeometry, RecordChanged, RecordInvalid, ClockRegressed };
struct Record {
  unsigned offset=0,bytes=0,function=0,result=0,sequence=0,payloadBytes=0,flags=0,slot=0,elapsedUs=0;
};
struct Result {
  const char *status="not-run";
  Stop stop=NotRun;Failure failure=None;
  unsigned header[8]={},reader=0,producer=0,polls=0,count=0,pages=0,bytes=0;
  unsigned startSlot=0,startSequence=0,published=0;
  unsigned failedSlot=~0U,nocatCount=0,partialPolls=0,pendingPages=0;
  U64 startNs=0,elapsedNs=0;
  bool headerValid=false,passed=false,initDone=false,sequencer=false;
  Record records[MaxRecords];
};
inline bool decode(const unsigned char *p,unsigned bytes,unsigned sequence,Record &r){
  using GSPContentSeal::get32;
  if(!p || bytes<Page || bytes>GSPFirstStatus::MaximumBytes || bytes%Page)return false;
  for(unsigned i=0;i<32;++i)if(p[i])return false;
  const unsigned count=get32(p+40),length=get32(p+56);
  if(count<1 || count>16 || bytes!=count*Page || get32(p+36)!=sequence || get32(p+44) ||
     get32(p+48)!=0x03000000 || get32(p+52)!=0x43505256 || length<32 ||
     length>=GSPFirstStatus::MaximumBytes-48 || (48+length+Page-1)/Page!=count ||
     get32(p+76) || get32(p+60)==71)return false;
  const unsigned end=48+length,aligned=(end+7)&~7U;
  for(unsigned i=end;i<aligned;++i)if(p[i])return false;
  unsigned sum=0;for(unsigned i=0;i<aligned;i+=4)sum^=get32(p+i);
  if(sum)return false;
  r.bytes=bytes;r.function=get32(p+60);r.result=get32(p+64);
  r.sequence=sequence;r.payloadBytes=length-32;
  r.flags=8; // Valid framing/checksum, not cryptographic authentication.
  if(r.function==0x1001 && !r.result && (r.payloadBytes==0 || r.payloadBytes==4))r.flags|=1;
  if(r.function==0x1002)r.flags|=2;
  if(r.function==0x1020)r.flags|=4;
  return true;
}
inline void fail(Result &r,Failure why,const char *status){
  r.stop=Error;r.failure=why;r.status=status;r.failedSlot=(r.startSlot+r.pages)%63;r.passed=false;
}
template<class IO> bool tick(IO &io,Result &r){
  const U64 now=io.nowNs();
  if(now<r.startNs || now-r.startNs<r.elapsedNs){fail(r,ClockRegressed,"event-clock-regressed");return false;}
  r.elapsedNs=now-r.startNs;
  if(!io.ready()){fail(r,Owner,"event-owner-lost");return false;}
  if(r.elapsedNs>=DurationNs || r.polls>=MaxPolls){
    r.stop=Deadline;r.status=r.count?"events-captured-observation-deadline":"event-first-message-timeout";
    r.passed=r.count!=0;return false;
  }
  return true;
}
template<class IO> bool header(IO &io,unsigned char *scratch,Result &r){
  if(!io.import()){fail(r,Import,"event-import-failed");return false;}
  if(!io.read(HostStatusReadOffset,scratch,4)){fail(r,Read,"event-reader-read-failed");return false;}
  r.reader=GSPContentSeal::get32(scratch);
  if(!io.read(GSPFirstStatus::StatusOffset,scratch,32)){fail(r,Read,"event-header-read-failed");return false;}
  for(unsigned i=0;i<8;++i)r.header[i]=GSPContentSeal::get32(scratch+4*i);
  if(r.reader!=r.startSlot){fail(r,ReaderChanged,"event-reader-index-changed");return false;}
  if(!r.headerValid && r.header[7]==0 && r.header[4]==0)return true;
  if(!GSPFirstStatus::header(r.header)){fail(r,Header,"event-header-invalid");return false;}
  r.headerValid=true;
  const unsigned distance=(r.header[4]+63-r.startSlot)%63;
  if(distance<r.published){fail(r,ProducerRegressed,"event-producer-regressed");return false;}
  r.producer=r.header[4];r.published=distance;return true;
}
template<class IO> void capture(IO &io,unsigned char *out,unsigned char *scratch,Result &r,unsigned startSlot,unsigned startSequence){
  r=Result{};r.startNs=io.nowNs();
  if(startSlot>=63 || startSequence>0xffffffffU-MaxRecords){fail(r,Header,"event-origin-invalid");return;}
  r.startSlot=startSlot;r.startSequence=startSequence;r.producer=startSlot;
  if(!out || !scratch || out==scratch || !io.ready()){fail(r,Owner,"event-owner-not-ready");return;}
  while(tick(io,r)){
    ++r.polls;
    if(!header(io,scratch,r))return;
    if(!r.headerValid || r.pages==r.published){io.delayUs(100);continue;}
    if(r.count>=MaxRecords || r.pages>=MaxPages){fail(r,RecordGeometry,"event-capacity-inconsistent");return;}
    unsigned char *p=out+r.bytes;
    const unsigned slot=(r.startSlot+r.pages)%63;
    const unsigned offset=GSPFirstStatus::EntriesOffset+slot*Page;
    if(!io.read(offset,p,Page)){fail(r,Read,"event-record-read-failed");return;}
    const unsigned count=GSPContentSeal::get32(p+40);
    if(count<1 || count>16 || count>MaxPages-r.pages){fail(r,RecordGeometry,"event-record-geometry-invalid");return;}
    if(count>r.published-r.pages){r.pendingPages=count;++r.partialPolls;io.delayUs(100);continue;}
    r.pendingPages=0;
    for(unsigned page=1;page<count;++page){
      if(!tick(io,r))return;
      if(!io.read(GSPFirstStatus::EntriesOffset+((slot+page)%63)*Page,p+page*Page,Page)){fail(r,Read,"event-record-read-failed");return;}
    }
    if(!io.import()){fail(r,Import,"event-record-reimport-failed");return;}
    for(unsigned page=0;page<count;++page){
      if(!tick(io,r))return;
      if(!io.read(GSPFirstStatus::EntriesOffset+((slot+page)%63)*Page,scratch,Page)){fail(r,Read,"event-record-reread-failed");return;}
      for(unsigned i=0;i<Page;++i)if(scratch[i]!=p[page*Page+i]){fail(r,RecordChanged,"event-record-changed");return;}
    }
    // Check the producer and host-owned readPtr again before committing the
    // sample. Later publication is allowed; backwards movement is not.
    if(!tick(io,r) || !header(io,scratch,r))return;
    Record row;
    if(!decode(p,count*Page,r.startSequence+r.count,row)){fail(r,RecordInvalid,"event-record-invalid");return;}
    row.offset=r.bytes;row.slot=slot;row.elapsedUs=unsigned(r.elapsedNs/1000);
    r.records[r.count++]=row;r.pages+=count;r.bytes+=count*Page;
    if(row.flags&4)++r.nocatCount;
    if(row.flags&1){r.initDone=r.passed=true;r.stop=InitDone;r.status="events-init-done-observed";return;}
    if(row.flags&2){r.sequencer=r.passed=true;r.stop=Sequencer;r.status="events-sequencer-handler-required";return;}
    if(r.pages==MaxPages){r.passed=true;r.stop=QueueFull;r.status="events-queue-capacity-reached";return;}
  }
}
}
