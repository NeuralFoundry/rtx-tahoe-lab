#pragma once
#include "GSPEventProtocol.hpp"
#include "GSPSequencerProfile.hpp"

namespace GSPStartup075 {
constexpr unsigned MaxRecords=8,MaxPages=MaxRecords+1;
struct Prefix {unsigned records=0,pages=0,sequenceOffset=0,sequenceBytes=0;bool valid=false;};
inline bool nocat(const unsigned char*p,unsigned n){
 if(!p||(n!=1208&&n!=1212)||p[16]==5||GSPContentSeal::get32(p+176)>1024)return false;
 for(unsigned field=0;field<2;++field){const unsigned off=field?104:24;bool terminated=false;for(unsigned i=0;i<65;++i)terminated=terminated||!p[off+i];if(!terminated)return false;}
 return true;
}
// Re-decode every immutable record and its native index, then validate the
// exact sequencer payload. At most seven non-ASSERT journals may precede it.
inline bool select(const GSPEvents::Result&e,const unsigned char*raw,unsigned capacity,Prefix&out){
 out=Prefix{};
 if(!raw||!e.passed||!e.headerValid||e.failure||!e.sequencer||e.initDone||e.stop!=GSPEvents::Sequencer||e.reader||
    !e.count||e.count>MaxRecords||e.pages!=e.count+1||e.pages>MaxPages||e.bytes!=e.pages*4096||e.bytes>capacity)return false;
 unsigned offset=0;
 for(unsigned i=0;i<e.count;++i){
  const auto &r=e.records[i];GSPEvents::Record d;
  if(r.offset!=offset||offset>e.bytes||r.bytes>e.bytes-offset||!GSPEvents::decode(raw+offset,r.bytes,i,d)||
     r.slot!=offset/4096||r.sequence!=d.sequence||r.function!=d.function||r.result!=d.result||r.payloadBytes!=d.payloadBytes||r.flags!=d.flags)return false;
  if(i+1==e.count){
   GSPSequencer::Profile profile;
   if(d.function!=0x1002||d.result||d.bytes!=8192||d.payloadBytes!=GSPSequencer::PayloadBytes||
      !GSPSequencer::profile(raw+offset+80,d.payloadBytes,profile))return false;
   out.sequenceOffset=offset;out.sequenceBytes=d.bytes;
  }else if(d.function!=0x1020||d.result||d.bytes!=4096||!nocat(raw+offset+80,d.payloadBytes))return false;
  offset+=r.bytes;
 }
 if(offset!=e.bytes||e.nocatCount!=e.count-1)return false;
 out.records=e.count;out.pages=e.pages;out.valid=true;return true;
}
}
