#pragma once
#include "GSPContentSeal.hpp"

// Capture ONLY the first published firmware status record. Never advance a
// queue index, acknowledge events, execute a sequencer or infer full startup.
namespace GSPFirstStatus {
constexpr unsigned StatusOffset=0x41000, EntriesOffset=0x42000, MaximumBytes=16*4096;
struct Result {
  const char *status="not-run";
  unsigned header[8]={},polls=0,recordBytes=0,function=0,result=0xffffffffU;
  unsigned payloadBytes=0,sequence=0xffffffffU;
  bool headerValid=false,captured=false,checksumValid=false,initDone=false,sequencer=false;
};
inline bool header(const unsigned *h){
  return h[0]==0 && h[1]==0x40000 && h[2]==4096 && h[3]==63 && h[4]<63 &&
    h[5]==1 && h[6]==32 && h[7]==4096;
}
inline bool record(const unsigned char *p,unsigned bytes,Result &r){
  using GSPContentSeal::get32;
  if(!p || bytes<4096 || bytes>MaximumBytes || bytes%4096)return false;
  for(unsigned i=0;i<32;++i)if(p[i])return false;
  const unsigned count=get32(p+40),length=get32(p+56);
  if(count<1 || count>16 || bytes!=count*4096 || get32(p+36)!=0 || get32(p+44) ||
     get32(p+48)!=0x03000000 || get32(p+52)!=0x43505256 || length<32 || length>=MaximumBytes-48 ||
     (48+length+4095)/4096!=count || get32(p+76) || get32(p+60)==71)return false;
  const unsigned end=48+length,aligned=(end+7)&~7U;
  for(unsigned i=end;i<aligned;++i)if(p[i])return false;
  unsigned sum=0;for(unsigned i=0;i<aligned;i+=4)sum^=get32(p+i);
  if(sum)return false;
  r.recordBytes=bytes;r.function=get32(p+60);r.result=get32(p+64);r.sequence=get32(p+36);
  r.payloadBytes=length-32;r.checksumValid=r.captured=true;
  r.initDone=r.function==0x1001 && r.result==0 && r.payloadBytes==4;
  r.sequencer=r.function==0x1002;
  return true;
}
template<class IO> void capture(IO &io,unsigned char *out,unsigned char *scratch,Result &r){
  r=Result{};
  if(!out || !scratch || !io.ready()){r.status="status-owner-not-ready";return;}
  bool available=false;
  for(unsigned i=0;i<20000;++i){
    ++r.polls;
    if(!io.ready() || !io.import() || !io.read(StatusOffset,scratch,32)){
      r.status="status-import-failed";return;
    }
    for(unsigned k=0;k<8;++k)r.header[k]=GSPContentSeal::get32(scratch+k*4);
    if(r.header[7]==4096){
      if(!header(r.header)){r.status="status-header-invalid";return;}
      r.headerValid=true;if(r.header[4]){available=true;break;}
    }
    io.delayUs(100);
  }
  if(!available){r.status="status-first-record-timeout";return;}
  if(!io.read(EntriesOffset,out,4096)){r.status="status-read-failed";return;}
  const unsigned count=GSPContentSeal::get32(out+40);
  if(count<1 || count>16 || count>r.header[4]){r.status="status-incomplete-record";return;}
  for(unsigned page=1;page<count;++page)
    if(!io.read(EntriesOffset+page*4096,out+page*4096,4096)){r.status="status-read-failed";return;}
  if(!io.import()){r.status="status-reimport-failed";return;}
  // With readPtr left zero, the published first slot cannot legally be reused.
  // A second sample must match before exposing the capture as valid evidence.
  for(unsigned page=0;page<count;++page){
    if(!io.read(EntriesOffset+page*4096,scratch,4096)){r.status="status-reread-failed";return;}
    for(unsigned i=0;i<4096;++i)if(scratch[i]!=out[page*4096+i]){r.status="status-record-changed";return;}
  }
  if(!io.ready() || !record(out,count*4096,r)){r.status="status-record-invalid";return;}
  r.status=r.initDone?"first-event-init-done":r.sequencer?"first-event-sequencer-awaiting-handler":"first-event-captured-unhandled";
}
}
