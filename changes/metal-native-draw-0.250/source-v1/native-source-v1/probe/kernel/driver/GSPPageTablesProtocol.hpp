#pragma once
#include "GSPComputePrepProtocol.hpp"

// Fixed Ampere MMU v2 three-level tree for the firmware's reserved 512MiB VA.
// References: NVIDIA570.144 uvm_ampere_mmu.c, uvm_pascal_mmu.c, dev_mmu_tu102.h,
// ctrl90f1.h; pinned tinygrad33cd373ad353 init_golden_image. No leaf PTE/channel.
namespace GSPPageTables {
using U64=unsigned long long;
using GSPComputePrep::put32;
constexpr unsigned Page=4096,Levels=3,Bytes=Page*Levels,Words=Bytes/4;
constexpr unsigned Start=0x01002000,End=Start+Bytes,Window=0x1704;
constexpr unsigned RootBytes=32,ParamsBytes=184,Control=0x90f10106U;
constexpr U64 VirtualStart=0x1000000000ULL,VirtualBytes=0x20000000ULL;
constexpr U64 VirtualEnd=VirtualStart+VirtualBytes,VaLimit=1ULL<<49;
constexpr unsigned TransportSequence=8,ControlHeader=24;
constexpr U64 BudgetNs=15000000000ULL,CleanupBudgetNs=5000000000ULL;
constexpr unsigned MaxTicks=60000;
static_assert(Start>=0x01002000&&End<=0x01010000,"Fixed lease excludes BAR1 pattern pages");
static_assert(VirtualStart%VirtualBytes==0&&VirtualEnd<VaLimit,"Fixed 49bit VA range");
constexpr unsigned shift(unsigned level){return level==0?47:level==1?38:level==2?29:0;}
constexpr unsigned size(unsigned level){return level==0?RootBytes:level<Levels?Page:0;}
constexpr unsigned physical(unsigned level){return level<Levels?Start+level*Page:0;}
constexpr unsigned index(unsigned level){return level==0?unsigned((VirtualStart>>47)&3):level<Levels?unsigned((VirtualStart>>shift(level))&511):~0U;}
constexpr U64 pde(unsigned address){return (U64(address>>12)<<8)|0x22ULL;} // VID aperture1, NO_ATS1, IS_PTE0.
constexpr U64 entry(unsigned level,unsigned slot){
  return level<2&&slot==index(level)?pde(physical(level+1)):0;
}
inline unsigned word(unsigned i){
  if(i>=Words)return 0;
  const unsigned level=i/(Page/4),slot=(i%(Page/4))/2;
  return unsigned(entry(level,slot)>>((i&1)*32));
}
inline void put64(unsigned char*p,U64 value){put32(p,unsigned(value));put32(p+4,unsigned(value>>32));}
inline U64 get64(const unsigned char*p){return GSPContentSeal::get32(p)|(U64(GSPContentSeal::get32(p+4))<<32);}
inline bool image(unsigned char*out,unsigned bytes){
  if(!out||bytes!=Bytes)return false;
  for(unsigned i=0;i<Words;++i)put32(out+i*4,word(i));
  return true;
}
inline bool imageMatches(const unsigned char*data,unsigned bytes){
  if(!data||bytes!=Bytes)return false;
  for(unsigned i=0;i<Words;++i)if(GSPContentSeal::get32(data+i*4)!=word(i))return false;
  return true;
}
inline bool parameters(unsigned char*out,unsigned bytes){
  if(!out||bytes!=ParamsBytes)return false;
  for(unsigned i=0;i<bytes;++i)out[i]=0;
  put64(out+8,VirtualBytes);put64(out+16,VirtualStart);put64(out+24,VirtualEnd-1);put32(out+32,Levels);
  for(unsigned level=0;level<Levels;++level){
    auto*p=out+40+24*level;put64(p,physical(level));put64(p+8,size(level));put32(p+16,1);p[20]=static_cast<unsigned char>(shift(level));
  }
  return true;
}
inline bool request(unsigned char*out,unsigned bytes){
  if(!out||bytes!=Page)return false;
  for(unsigned i=0;i<Page;++i)out[i]=0;
  put32(out+36,TransportSequence);put32(out+40,1);put32(out+48,0x03000000);put32(out+52,0x43505256);
  put32(out+56,32+ControlHeader+ParamsBytes);put32(out+60,76);put32(out+64,~0U);put32(out+68,~0U);
  put32(out+80,GSPComputePrep::Client);put32(out+84,GSPComputePrep::Vaspace);put32(out+88,Control);put32(out+96,ParamsBytes);
  if(!parameters(out+104,ParamsBytes))return false;
  unsigned sum=0;for(unsigned i=0;i<288;i+=4)sum^=GSPContentSeal::get32(out+i);put32(out+32,sum);return true;
}
inline bool replyIdentity(const unsigned char*p,const GSPInitEvents::Record&row){
  if(!p||row.function!=76||row.result||row.payloadBytes!=ControlHeader+ParamsBytes||
     GSPContentSeal::get32(p+68)||GSPContentSeal::get32(p+72)||GSPContentSeal::get32(p+80)!=GSPComputePrep::Client||
     GSPContentSeal::get32(p+84)!=GSPComputePrep::Vaspace||GSPContentSeal::get32(p+88)!=Control||
     GSPContentSeal::get32(p+92)||GSPContentSeal::get32(p+96)!=ParamsBytes||GSPContentSeal::get32(p+100))return false;
  // All control fields are input-only. Require exact echo, including padding.
  unsigned char expected[ParamsBytes];parameters(expected,ParamsBytes);
  for(unsigned i=0;i<ParamsBytes;++i)if(p[104+i]!=expected[i])return false;
  return true;
}
inline bool vaspaceRange(const unsigned char*params,unsigned bytes){
  // nvos.h: vaSize is VA_LIMIT+1 (not usable length); vaBase is at offset40.
  // Offsets16/24 describe RM-internal VA. They are not the usable base/limit.
  if(!params||bytes!=48)return false;
  const U64 end=get64(params+8),start=get64(params+40);
  return start>=0x4000000&&start<end&&end<=VaLimit&&VirtualStart>=start&&VirtualEnd<=end;
}

enum Failure:unsigned{None,Owner,Storage,Replay,Clock,Timeout,Read,Write,WindowMismatch,ImageMismatch,Restore};
struct Result{
  Failure failure=None;const char*status="not-run";
  bool validated=false,attempted=false,passed=false,windowSaved=false,modified=false,windowRestored=false;
  unsigned inspected=0,written=0,checked=0,captured=0,reads=0,writes=0,ticks=0;
  unsigned windowBefore=0,windowAfter=~0U,lastAddress=0,lastValue=0,expected=0,failedWord=~0U;
  U64 startNs=0,elapsedNs=0,cleanupNs=0;
};
inline void fail(Result&r,Failure f,const char*status){if(r.failure==None){r.failure=f;r.status=status;}r.passed=false;}
template<class IO>bool tick(IO&io,Result&r){
  const U64 now=io.nowNs();
  if(now<r.startNs||now-r.startNs<r.elapsedNs){fail(r,Clock,"page-table-clock-regressed");return false;}
  r.elapsedNs=now-r.startNs;
  if(!io.ready()){fail(r,Owner,"page-table-owner-lost");return false;}
  if(++r.ticks>MaxTicks||r.elapsedNs>=BudgetNs){fail(r,Timeout,"page-table-deadline");return false;}
  return true;
}
template<class IO>bool read(IO&io,Result&r,unsigned off,unsigned&value){
  if(!tick(io,r))return false;++r.reads;r.lastAddress=off;
  if(!io.read(off,value)){fail(r,Read,"page-table-read-failed");return false;}r.lastValue=value;return true;
}
template<class IO>bool write(IO&io,Result&r,unsigned off,unsigned value){
  if(!tick(io,r))return false;++r.writes;r.lastAddress=off;r.lastValue=value;
  if(!io.write(off,value)){fail(r,Write,"page-table-write-failed");return false;}return true;
}
// Staging deliberately retains table bytes. A later firmware publication may
// dereference them even after an uncertain RPC return; never restore/reuse them.
// restoreWindow is separate so post-RPC evidence can still use physical BAR1.
template<class IO>void stage(IO&io,unsigned char*capture,Result&r){
  r=Result{};r.startNs=io.nowNs();
  if(!capture){fail(r,Storage,"page-table-capture-unavailable");return;}
  if(!io.ready()){fail(r,Owner,"page-table-owner-unavailable");return;}
  r.validated=true;
  if(!io.claim()){fail(r,Replay,"page-table-already-attempted");return;}r.attempted=true;
  if(!read(io,r,Window,r.windowBefore))return;
  if(r.windowBefore==~0U){fail(r,WindowMismatch,"page-table-window-unavailable");return;}
  r.windowSaved=true;unsigned actual=0;
  if(!write(io,r,Window,0)||!read(io,r,Window,actual))return;
  if(actual){fail(r,WindowMismatch,"page-table-physical-mode-mismatch");return;}
  for(unsigned i=0;i<Words;++i){
    if(!read(io,r,Start+i*4,actual))return;
    if(actual==~0U||(actual>>16)==0xbad0U||(actual>>16)==0xbadfU){r.failedWord=i;fail(r,Read,"page-table-initial-memory-unreadable");return;}
    ++r.inspected;
  }
  // Child pages become complete before their parent links are written.
  for(unsigned left=Words;left>0;--left){const unsigned i=left-1;
    r.modified=true;if(!write(io,r,Start+i*4,word(i)))return;++r.written;
  }
  for(unsigned i=0;i<Words;++i){
    if(!read(io,r,Start+i*4,actual))return;
    put32(capture+i*4,actual);r.captured+=4;r.expected=word(i);
    if(actual!=r.expected){r.failedWord=i;fail(r,ImageMismatch,"page-table-staged-image-mismatch");return;}++r.checked;
  }
  r.passed=true;r.status="page-table-image-staged-not-published";
}
template<class IO>void restoreWindow(IO&io,Result&r){
  if(!r.windowSaved)return;
  const U64 started=io.nowNs();unsigned actual=0;
  auto ready=[&](){const U64 now=io.nowNs();if(now<started||now-started<r.cleanupNs)return false;
    r.cleanupNs=now-started;return r.cleanupNs<CleanupBudgetNs&&io.ready();};
  if(ready()&&io.write(Window,r.windowBefore)&&ready()&&io.read(Window,actual)){
    r.windowAfter=actual;r.windowRestored=actual==r.windowBefore;
  }
  if(!r.windowRestored)fail(r,Restore,"page-table-window-restore-failed-retained");
}
}
