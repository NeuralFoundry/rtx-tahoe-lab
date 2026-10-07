#pragma once
#include "GSPComputePrepProtocol.hpp"
#include "GSPSequencerProfile.hpp"

// Fixed two-page PRAMIN experiment below the sequencer and GSP heap leases.
// This is an exclusive-driver software lease, not an OS/RM allocation.
// Linux v6.14 nv50_instobj_{rd,wr}32_slow provides the 1MiB window encoding.
namespace GSPVram {
using U64=unsigned long long;
constexpr U64 LeaseStart=0x173a00000ULL,LeaseEnd=GSPSequencer::WorkspaceStart;
constexpr U64 Start=0x173aff000ULL,End=Start+8192;
constexpr unsigned Bytes=8192,Words=Bytes/4,CaptureBytes=Bytes*2,Window=0x1700,Aperture=0x700000;
constexpr U64 BudgetNs=15000000000ULL,CleanupBudgetNs=5000000000ULL;
constexpr unsigned MaxTicks=60000;
static_assert(LeaseStart<Start&&End<LeaseEnd,"VRAM probe must remain below the sequencer lease");
enum Failure:unsigned{None,Owner,Storage,Replay,Clock,Timeout,Read,Write,WindowMismatch,PatternMismatch,Restore};
struct Result{
  Failure failure=None;const char*status="not-run";
  bool validated=false,attempted=false,passed=false,windowSaved=false,modified=false;
  bool originalRestored=false,windowRestored=false,cleanupAttempted=false;
  unsigned savedWords=0,writtenWords=0,checkedWords=0,capturedBytes=0,restoredWords=0;
  unsigned reads=0,writes=0,ticks=0,cleanupReads=0,cleanupWrites=0,windowChanges=0;
  unsigned windowBefore=0,windowCurrent=~0U,windowAfter=~0U,lastAddress=0,lastValue=0,expected=0;
  unsigned pass=0,failedWord=~0U;U64 startNs=0,elapsedNs=0,cleanupNs=0;
};
inline unsigned pattern(unsigned word,unsigned pass){
  unsigned x=0x9e3779b9U^(word*0x45d9f3bU)^unsigned(Start>>12);
  x^=x<<13;x^=x>>17;x^=x<<5;return pass?~x:x;
}
inline unsigned windowValue(unsigned word){return unsigned(((Start+word*4)&~0xfffffULL)>>16);}
inline unsigned address(unsigned word){return Aperture+unsigned((Start+word*4)&0xfffffULL);}
inline void fail(Result&r,Failure f,const char*why){if(r.failure==None){r.failure=f;r.status=why;}r.passed=false;}
template<class IO>bool tick(IO&io,Result&r){
  const U64 now=io.nowNs();
  if(now<r.startNs||now-r.startNs<r.elapsedNs){fail(r,Clock,"vram-clock-regressed");return false;}
  r.elapsedNs=now-r.startNs;
  if(!io.ready()){fail(r,Owner,"vram-owner-lost");return false;}
  if(++r.ticks>MaxTicks||r.elapsedNs>=BudgetNs){fail(r,Timeout,"vram-deadline");return false;}
  return true;
}
template<class IO>bool read(IO&io,Result&r,unsigned off,unsigned&v){
  if(!tick(io,r))return false;++r.reads;r.lastAddress=off;
  if(!io.read(off,v)){fail(r,Read,"vram-read-failed");return false;}r.lastValue=v;return true;
}
template<class IO>bool write(IO&io,Result&r,unsigned off,unsigned v){
  if(!tick(io,r))return false;++r.writes;r.lastAddress=off;r.lastValue=v;
  if(!io.write(off,v)){fail(r,Write,"vram-write-failed");return false;}return true;
}
template<class IO>bool select(IO&io,Result&r,unsigned word){
  const unsigned desired=windowValue(word);
  if(r.windowCurrent==desired)return true;
  ++r.windowChanges;r.windowCurrent=~0U;unsigned actual=0;
  if(!write(io,r,Window,desired)||!read(io,r,Window,actual))return false;
  if(actual!=desired){fail(r,WindowMismatch,"vram-window-readback");return false;}
  r.windowCurrent=desired;return true;
}
template<class IO>void cleanup(IO&io,Result&r,const unsigned char*original){
  r.cleanupAttempted=r.windowSaved;
  const U64 started=io.nowNs();U64 previous=0;
  auto ready=[&](){const U64 now=io.nowNs();if(now<started||now-started<previous)return false;
    previous=now-started;r.cleanupNs=previous;return io.ready()&&previous<CleanupBudgetNs;};
  auto rd=[&](unsigned off,unsigned&v){if(!ready())return false;++r.cleanupReads;return io.read(off,v);};
  auto wr=[&](unsigned off,unsigned v){if(!ready())return false;++r.cleanupWrites;return io.write(off,v);};
  r.windowCurrent=~0U;
  if(r.modified&&r.savedWords==Words){
    for(unsigned i=0;i<Words;++i){
      unsigned actual=0;const unsigned desired=windowValue(i);
      if(r.windowCurrent!=desired){
        if(!wr(Window,desired)||!rd(Window,actual)||actual!=desired)break;
        r.windowCurrent=desired;
      }
      const unsigned expected=GSPContentSeal::get32(original+4*i);
      if(!wr(address(i),expected)||!rd(address(i),actual)||actual!=expected)break;
      ++r.restoredWords;
    }
    r.originalRestored=r.restoredWords==Words;
  }else if(!r.modified)r.originalRestored=true;
  if(r.windowSaved){
    unsigned actual=0;
    if(wr(Window,r.windowBefore)&&rd(Window,actual)){r.windowAfter=actual;r.windowRestored=actual==r.windowBefore;}
  }
  if(!r.originalRestored||!r.windowRestored)fail(r,Restore,"vram-restore-incomplete-retained");
}
template<class IO>void execute(IO&io,unsigned char*original,unsigned char*capture,Result&r){
  r=Result{};r.startNs=io.nowNs();
  if(!original||!capture||original==capture){fail(r,Storage,"vram-storage-unavailable");return;}
  if(!io.ready()){fail(r,Owner,"vram-owner-unavailable");return;}
  r.validated=true;
  if(!io.claim()){fail(r,Replay,"vram-already-attempted");return;}
  r.attempted=true;
  auto run=[&](){
    if(!read(io,r,Window,r.windowBefore))return;
    if(r.windowBefore==~0U){fail(r,WindowMismatch,"vram-window-unavailable");return;}
    r.windowSaved=true;
    for(unsigned i=0;i<Words;++i){unsigned v=0;
      if(!select(io,r,i)||!read(io,r,address(i),v))return;
      GSPComputePrep::put32(original+4*i,v);++r.savedWords;
    }
    for(unsigned pass=0;pass<2;++pass){r.pass=pass;
      for(unsigned i=0;i<Words;++i){
        if(!select(io,r,i))return;
        r.modified=true; // A reported write failure can still have reached the device.
        if(!write(io,r,address(i),pattern(i,pass)))return;++r.writtenWords;
      }
      for(unsigned i=0;i<Words;++i){unsigned v=0;
        if(!select(io,r,i)||!read(io,r,address(i),v))return;
        GSPComputePrep::put32(capture+pass*Bytes+i*4,v);r.capturedBytes+=4;r.expected=pattern(i,pass);
        if(v!=r.expected){r.failedWord=i;fail(r,PatternMismatch,"vram-pattern-mismatch");return;}++r.checkedWords;
      }
    }
  };
  run();
  if(r.windowSaved)cleanup(io,r,original);
  if(r.failure==None&&r.checkedWords==Words*2&&r.originalRestored&&r.windowRestored){
    r.passed=true;r.status="vram-two-page-patterns-and-restoration-verified";
  }
}
}
