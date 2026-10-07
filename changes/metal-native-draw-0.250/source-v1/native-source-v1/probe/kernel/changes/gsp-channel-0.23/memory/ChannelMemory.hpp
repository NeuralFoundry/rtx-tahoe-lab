#pragma once
#include "../transactions/ChannelCodec.hpp"
#include "../GMMUInvalidate.hpp"

namespace ChannelMemory {
namespace L=GMMULeaves;namespace C=ChannelCodec;
using U64=unsigned long long;
constexpr unsigned Page=4096,Window=0x1704,RingStart=0x1100000,RingBytes=0x7000;
constexpr U64 BudgetNs=90000000000ULL,CleanupNs=5000000000ULL;
constexpr unsigned MaxOperations=100000;
enum class Failure:unsigned {None,Storage,Owner,Replay,Clock,Timeout,Read,Write,Unreadable,Changed,Plan,WindowState,Invalidate,Cleanup};
struct Result {
  bool attempted=false,passed=false,modified=false,parentAttempted=false,backingVerified=false,childrenVerified=false;
  bool windowSaved=false,windowRestored=false;
  Failure failure=Failure::None;unsigned operations=0,reads=0,writes=0,inspectedBytes=0,zeroedBytes=0,verifiedBackingBytes=0;
  unsigned childBytes=0,verifiedChildBytes=0,linksPublished=0,lastAddress=0,lastValue=0,windowBefore=0,windowAfter=~0U;
  U64 start=0,elapsed=0,cleanupElapsed=0;GMMUInvalidate::Result invalidation;
};
inline void fail(Result &r,Failure why){if(r.failure==Failure::None)r.failure=why;r.passed=false;}
inline bool disjoint(const void *a,size_t an,const void *b,size_t bn){
  const auto x=reinterpret_cast<uintptr_t>(a),y=reinterpret_cast<uintptr_t>(b);
  return a&&b&&x<=UINTPTR_MAX-an&&y<=UINTPTR_MAX-bn&&!(x<y+bn&&y<x+an);
}
template<class IO>bool tick(IO &io,Result &r){
  const U64 now=io.nowNs();
  if(now<r.start||now-r.start<r.elapsed){fail(r,Failure::Clock);return false;}r.elapsed=now-r.start;
  if(r.elapsed>=BudgetNs||r.operations>=MaxOperations){fail(r,Failure::Timeout);return false;}
  if(!io.ready()){fail(r,Failure::Owner);return false;}return true;
}
template<class IO>bool read(IO &io,Result &r,unsigned address,unsigned char *out,unsigned bytes){
  if(!tick(io,r))return false;++r.operations;++r.reads;r.lastAddress=address;
  if(!io.readMemory(address,out,bytes)){fail(r,Failure::Read);return false;}return true;
}
template<class IO>bool write(IO &io,Result &r,unsigned address,const unsigned char *data,unsigned bytes){
  if(!tick(io,r))return false;++r.operations;++r.writes;r.lastAddress=address;r.modified=true;
  if(!io.writeMemory(address,data,bytes)){fail(r,Failure::Write);return false;}return true;
}
template<class IO>bool regRead(IO &io,Result &r,unsigned address,unsigned &value){
  if(!tick(io,r))return false;++r.operations;++r.reads;r.lastAddress=address;
  if(!io.readRegister(address,value)){fail(r,Failure::Read);return false;}r.lastValue=value;
  if(GMMUInvalidate::unreadable(value)){fail(r,Failure::Unreadable);return false;}return true;
}
template<class IO>bool regWrite(IO &io,Result &r,unsigned address,unsigned value){
  if(!tick(io,r))return false;++r.operations;++r.writes;r.lastAddress=address;r.lastValue=value;
  if(!io.writeRegister(address,value)){fail(r,Failure::Write);return false;}return true;
}
template<class IO>bool select(IO &io,Result &r){
  unsigned value=0;if(!regRead(io,r,Window,r.windowBefore))return false;r.windowSaved=true;
  if(!regWrite(io,r,Window,0)||!regRead(io,r,Window,value))return false;
  if(value){fail(r,Failure::WindowState);return false;}return true;
}
template<class IO>bool compare(IO &io,Result &r,unsigned base,const unsigned char *expected,unsigned bytes,unsigned char *scratch){
  for(unsigned off=0;off<bytes;off+=Page){const unsigned count=bytes-off<Page?bytes-off:Page;
    if(!read(io,r,base+off,scratch,count))return false;
    for(unsigned i=0;i<count;++i)if(scratch[i]!=expected[off+i]){r.lastAddress=base+off+i;fail(r,Failure::Changed);return false;}
  }return true;
}
template<class IO>bool oldCompare(IO &io,Result &r,const unsigned char *old,bool published,unsigned char *scratch){
  for(unsigned off=0;off<L::OldBytes;off+=Page){
    if(!read(io,r,unsigned(L::OldBase)+off,scratch,Page))return false;
    for(unsigned i=0;i<Page;++i){const unsigned at=off+i;unsigned char expected=old[at];
      if(published&&at>=L::ParentOffset&&at<L::ParentOffset+8)expected=static_cast<unsigned char>(L::ParentValue>>((at-L::ParentOffset)*8));
      if(scratch[i]!=expected){r.lastAddress=unsigned(L::OldBase)+at;fail(r,Failure::Changed);return false;}
    }
  }return true;
}
template<class IO>bool inspect(IO &io,Result &r,unsigned base,unsigned bytes,unsigned char *scratch){
  for(unsigned off=0;off<bytes;off+=Page){const unsigned count=bytes-off<Page?bytes-off:Page;
    if(!read(io,r,base+off,scratch,count))return false;
    for(unsigned i=0;i<count;i+=4)if(GMMUInvalidate::unreadable(C::R::get32(scratch+i))){r.lastAddress=base+off+i;fail(r,Failure::Unreadable);return false;}
    r.inspectedBytes+=count;
  }return true;
}
template<class IO>bool zero(IO &io,Result &r,unsigned base,unsigned bytes,unsigned char *scratch){
  for(unsigned i=0;i<Page;++i)scratch[i]=0;
  for(unsigned off=0;off<bytes;off+=Page){const unsigned count=bytes-off<Page?bytes-off:Page;
    if(!write(io,r,base+off,scratch,count))return false;r.zeroedBytes+=count;
  }
  for(unsigned off=0;off<bytes;off+=Page){const unsigned count=bytes-off<Page?bytes-off:Page;
    if(!read(io,r,base+off,scratch,count))return false;
    for(unsigned i=0;i<count;++i)if(scratch[i]){r.lastAddress=base+off+i;fail(r,Failure::Changed);return false;}
    r.verifiedBackingBytes+=count;
  }return true;
}
template<class IO>bool childPages(IO &io,Result &r,const unsigned char *image,unsigned begin,unsigned end,unsigned char *scratch){
  for(unsigned left=end;left>begin;left-=Page){const unsigned off=left-Page;
    if(!write(io,r,unsigned(L::NewBase)+off,image+off,Page)||!compare(io,r,unsigned(L::NewBase)+off,image+off,Page,scratch))return false;
    r.verifiedChildBytes+=Page;
  }return true;
}
template<class IO>class Invalidator {
  IO &io;Result &outer;
public:
  Invalidator(IO &i,Result &r):io(i),outer(r){}
  uint64_t now(){const U64 value=io.nowNs();
    if(value<outer.start||value-outer.start<outer.elapsed)fail(outer,Failure::Clock);
    else {outer.elapsed=value-outer.start;if(outer.elapsed>=BudgetNs)fail(outer,Failure::Timeout);}return value;
  }
  bool read(uint32_t addr,uint32_t &value){if(outer.failure!=Failure::None||!io.ready())return false;outer.lastAddress=addr;return io.readRegister(addr,value);}
  bool write(uint32_t addr,uint32_t value){if(outer.failure!=Failure::None||!io.ready())return false;outer.lastAddress=addr;outer.lastValue=value;return io.writeRegister(addr,value);}
};
template<class IO>bool invalidate(IO &io,Result &r){
  Invalidator<IO> adapter(io,r);const GMMUInvalidate::Preconditions p={true,r.childrenVerified,r.parentAttempted,true};
  if(!GMMUInvalidate::run(adapter,p,r.invalidation)){fail(r,Failure::Invalidate);return false;}
  return tick(io,r);
}
template<class IO>void ring(IO &io,const unsigned char *old,unsigned char *children,unsigned char *scratch,Result &r){
  r={};r.start=io.nowNs();
  if(!disjoint(old,L::OldBytes,children,2*Page)||!disjoint(old,L::OldBytes,scratch,Page)||!disjoint(children,2*Page,scratch,Page)){fail(r,Failure::Storage);return;}
  L::Result built;const L::Range range={L::VABase,RingStart,Page};
  if(!L::build(old,L::OldBytes,&range,1,children,2*Page,built)){fail(r,Failure::Plan);return;}
  if(!io.ready()){fail(r,Failure::Owner);return;}if(!io.claimRing()){fail(r,Failure::Replay);return;}r.attempted=true;r.childBytes=2*Page;
  if(!select(io,r)||!oldCompare(io,r,old,false,scratch)||!inspect(io,r,unsigned(L::NewBase),2*Page,scratch)||!inspect(io,r,RingStart,RingBytes,scratch))return;
  if(!zero(io,r,RingStart,RingBytes,scratch))return;r.backingVerified=true;
  if(!childPages(io,r,children,0,2*Page,scratch))return;r.childrenVerified=true;
  // Recheck the old tree just before publishing. Only one known empty entry changes.
  if(!oldCompare(io,r,old,false,scratch))return;
  unsigned char link[8];L::write64(link,L::ParentValue);r.parentAttempted=true;
  // High word before low word: the valid aperture bits are committed last.
  if(!write(io,r,unsigned(L::OldBase+L::ParentOffset+4),link+4,4)||!write(io,r,unsigned(L::OldBase+L::ParentOffset),link,4))return;
  ++r.linksPublished;
  if(!oldCompare(io,r,old,true,scratch)||!invalidate(io,r))return;
  r.passed=true;
}
template<class IO>void contexts(IO &io,const C::Plan &plan,const unsigned char *old,const unsigned char *ringImage,
  unsigned char *children,unsigned char *scratch,const Result &prior,Result &r){
  r={};r.start=io.nowNs();
  if(!disjoint(old,L::OldBytes,children,L::MaxChildBytes)||!disjoint(ringImage,2*Page,children,L::MaxChildBytes)||
     !disjoint(old,L::OldBytes,scratch,Page)||!disjoint(ringImage,2*Page,scratch,Page)||!disjoint(children,L::MaxChildBytes,scratch,Page)){
    fail(r,Failure::Storage);return;
  }
  if(!prior.passed||!prior.attempted||!prior.modified||!prior.parentAttempted||!prior.windowSaved||prior.failure!=Failure::None||
     !prior.backingVerified||!prior.childrenVerified||!prior.invalidation.passed||!prior.invalidation.completed||
     !prior.invalidation.commandAttempted||prior.windowRestored||prior.childBytes!=2*Page||prior.verifiedChildBytes!=2*Page||
     prior.verifiedBackingBytes!=RingBytes||prior.linksPublished!=1){fail(r,Failure::Owner);return;}
  L::Range ranges[10];L::Result built;
  if(!C::mappingRanges(plan,ranges)||!L::build(old,L::OldBytes,ranges,10,children,L::MaxChildBytes,built)){fail(r,Failure::Plan);return;}
  // Same ring leaf table and entry0; all other prior PDE0 entries were empty.
  for(unsigned i=0;i<2*Page;++i){const unsigned char expected=i<16||i>=Page?children[i]:0;
    if(ringImage[i]!=expected){fail(r,Failure::Changed);return;}
  }
  if(!io.ready()){fail(r,Failure::Owner);return;}if(!io.claimContexts(plan)){fail(r,Failure::Replay);return;}
  r.attempted=true;r.childBytes=unsigned(built.childBytes);
  unsigned value=~0U;if(!regRead(io,r,Window,value))return;if(value){fail(r,Failure::WindowState);return;}
  if(!oldCompare(io,r,old,true,scratch)||!compare(io,r,unsigned(L::NewBase),ringImage,2*Page,scratch)||
     !inspect(io,r,unsigned(L::NewBase)+2*Page,r.childBytes-2*Page,scratch))return;
  // Inspect all proposed backing before the first context-memory write.
  for(const auto &b:plan.buffers)if(!inspect(io,r,unsigned(b.physical),unsigned(b.allocated),scratch))return;
  for(const auto &b:plan.buffers)if(!zero(io,r,unsigned(b.physical),unsigned(b.allocated),scratch))return;
  r.backingVerified=true;
  if(!childPages(io,r,children,2*Page,r.childBytes,scratch)||!compare(io,r,unsigned(L::NewBase),ringImage,2*Page,scratch)||!oldCompare(io,r,old,true,scratch))return;
  // Publish only new dual-PDE small links. Existing ring entry0 is untouched.
  for(unsigned group=1;group<256;++group){const auto *entry=children+group*16;
    if(L::read64(entry)==0&&L::read64(entry+8)==0)continue;
    const unsigned address=unsigned(L::NewBase)+group*16;
    if(!read(io,r,address,scratch,16))return;
    for(unsigned i=0;i<16;++i)if(scratch[i]){fail(r,Failure::Changed);return;}
    r.parentAttempted=true;
    // NO_ATS and high/reserved words first; valid small-aperture word last.
    if(!write(io,r,address,entry,4)||!write(io,r,address+4,entry+4,4)||!write(io,r,address+12,entry+12,4)||!write(io,r,address+8,entry+8,4)||
       !compare(io,r,address,entry,16,scratch))return;
    ++r.linksPublished;
  }
  if(!compare(io,r,unsigned(L::NewBase),children,r.childBytes,scratch)||!oldCompare(io,r,old,true,scratch))return;
  r.verifiedChildBytes=r.childBytes;r.childrenVerified=true;
  if(!invalidate(io,r))return;r.passed=true;
}
// Window cleanup is independent of the main deadline. VRAM/tables are retained,
// never restored or reused after publication or an uncertain command return.
template<class IO>void restoreWindow(IO &io,Result &ringResult){
  if(!ringResult.windowSaved)return;
  const U64 started=io.nowNs();U64 previous=0;unsigned actual=~0U;
  auto ready=[&](){const U64 now=io.nowNs();if(now<started||now-started<previous)return false;previous=now-started;ringResult.cleanupElapsed=previous;return previous<CleanupNs&&io.ready();};
  if(ready()&&io.writeRegister(Window,ringResult.windowBefore)&&ready()&&io.readRegister(Window,actual)&&ready()){
    ringResult.windowAfter=actual;ringResult.windowRestored=actual==ringResult.windowBefore;
  }
  if(!ringResult.windowRestored)fail(ringResult,Failure::Cleanup);
}
}
