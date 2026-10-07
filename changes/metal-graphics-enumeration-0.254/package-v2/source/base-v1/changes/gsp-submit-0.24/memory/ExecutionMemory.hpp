#pragma once
#include "../layout/ExecutionTables.hpp"
#include "../../gsp-channel-0.23/memory/ChannelMemory.hpp"
namespace ExecutionMemory {
namespace P=ExecutionPlan;namespace T=ExecutionTables;namespace C=ChannelCodec;namespace L=GMMULeaves;namespace M=ChannelMemory;
using Result=M::Result;
constexpr unsigned FixedBytes=unsigned(P::FixedEnd-P::Base);
struct Storage {
  const unsigned char *root=nullptr,*goldenChildren=nullptr;unsigned goldenBytes=0;
  unsigned char *rootScratch=nullptr,*expectedScratch=nullptr,*fixedChildren=nullptr,*fullChildren=nullptr,*scratch=nullptr;
};
inline bool equal(const unsigned char *a,const unsigned char *b,unsigned n){for(unsigned i=0;i<n;++i)if(a[i]!=b[i])return false;return true;}
inline bool separate(const Storage &s,const void *p,size_t bytes){
  const void *outputs[]={s.rootScratch,s.expectedScratch,s.fixedChildren,s.fullChildren,s.scratch};
  const size_t lengths[]={L::OldBytes,L::MaxChildBytes,L::MaxChildBytes,L::MaxChildBytes,4096};
  for(unsigned i=0;i<5;++i)if(outputs[i]&&!P::disjoint(p,bytes,outputs[i],lengths[i]))return false;return true;
}
inline bool resetResult(const Storage &s,const C::Plan &g,Result &r){
  if(!separate(s,&r,sizeof(r))||!P::disjoint(&r,sizeof(r),&s,sizeof(s))||!P::disjoint(&r,sizeof(r),&g,sizeof(g))||
     (s.root&&!P::disjoint(s.root,L::OldBytes,&r,sizeof(r)))||(s.goldenChildren&&!P::disjoint(s.goldenChildren,s.goldenBytes,&r,sizeof(r))))return false;
  r={};return true;
}
inline bool storage(const Storage &s,const C::Plan &g,const Result &r){
  if(s.goldenBytes<8192||s.goldenBytes>L::MaxChildBytes||s.goldenBytes%4096)return false;
  struct Span{const void *p;size_t bytes;};
  const Span inputs[]={{s.root,L::OldBytes},{s.goldenChildren,s.goldenBytes},{&g,sizeof(g)},{&s,sizeof(s)}};
  const Span outputs[]={{s.rootScratch,L::OldBytes},{s.expectedScratch,L::MaxChildBytes},{s.fixedChildren,L::MaxChildBytes},
    {s.fullChildren,L::MaxChildBytes},{s.scratch,4096},{&r,sizeof(r)}};
  for(const auto &a:outputs)for(const auto &b:inputs)if(!P::disjoint(a.p,a.bytes,b.p,b.bytes))return false;
  for(unsigned i=0;i<6;++i)for(unsigned j=0;j<i;++j)if(!P::disjoint(outputs[i].p,outputs[i].bytes,outputs[j].p,outputs[j].bytes))return false;
  return P::disjoint(s.root,L::OldBytes,s.goldenChildren,s.goldenBytes);
}
inline bool goldenImage(const Storage &s,const C::Plan &golden){
  if(!P::goldenFits(golden)||L::read64(s.root)!=0x100322||L::read64(s.root+4096)!=0x100422||L::read64(s.root+L::ParentOffset)!=L::ParentValue)return false;
  for(unsigned i=0;i<L::OldBytes;++i)s.rootScratch[i]=s.root[i];L::write64(s.rootScratch+L::ParentOffset,0);
  L::Range ranges[10];L::Result built;
  return C::mappingRanges(golden,ranges)&&L::build(s.rootScratch,L::OldBytes,ranges,10,s.expectedScratch,L::MaxChildBytes,built)&&
    built.childBytes==s.goldenBytes&&equal(s.expectedScratch,s.goldenChildren,s.goldenBytes);
}
inline bool buildFixed(const Storage &s,const C::Plan &golden){
  if(!goldenImage(s,golden))return false;
  // Canonical golden tree puts its existing ring leaf in the first PT page.
  if(L::read64(s.goldenChildren)!=0x20||L::read64(s.goldenChildren+8)!=((L::NewBase+4096)>>4|2))return false;
  for(unsigned slot=1;slot<=3;++slot)if(L::read64(s.goldenChildren+4096+slot*8))return false;
  for(unsigned i=0;i<s.goldenBytes;++i)s.fixedChildren[i]=s.goldenChildren[i];
  constexpr unsigned pa[3]={unsigned(P::Base)+0x2000,unsigned(P::Base)+0x3000,unsigned(P::Base)};
  for(unsigned i=0;i<3;++i)L::write64(s.fixedChildren+4096+(i+1)*8,(L::Kind<<56)|(pa[i]>>4)|1);
  return true;
}
template<class IO>bool rootCompare(IO &io,Result &r,const Storage &s){return M::compare(io,r,unsigned(L::OldBase),s.root,unsigned(L::OldBytes),s.scratch);}
template<class IO>bool fixed(IO &io,const C::Plan &golden,unsigned predecessor,const Storage &s,Result &r){
  // A malformed/aliased result reference must not corrupt captured inputs.
  if(!resetResult(s,golden,r))return false;
  if(!storage(s,golden,r)){M::fail(r,M::Failure::Storage);return false;}r.start=io.nowNs();
  if(predecessor!=3||!buildFixed(s,golden)){M::fail(r,M::Failure::Plan);return false;}
  if(!io.ready()){M::fail(r,M::Failure::Owner);return false;}
  if(!io.claimFixed()){M::fail(r,M::Failure::Replay);return false;}
  r.attempted=true;r.childBytes=s.goldenBytes;
  if(!M::select(io,r)||!rootCompare(io,r,s)||!M::compare(io,r,unsigned(L::NewBase),s.goldenChildren,s.goldenBytes,s.scratch)||
     !M::inspect(io,r,unsigned(P::Base),FixedBytes,s.scratch))return false;
  if(!M::zero(io,r,unsigned(P::Base),FixedBytes,s.scratch))return false;r.backingVerified=true;
  if(!rootCompare(io,r,s)||!M::compare(io,r,unsigned(L::NewBase),s.goldenChildren,s.goldenBytes,s.scratch))return false;
  // Existing PT0 is live: only three checked empty PTEs are published. Their
  // high words precede the low valid words. Never rewrite the rest of the PT.
  for(unsigned slot=1;slot<=3;++slot){const unsigned off=4096+slot*8,address=unsigned(L::NewBase)+off;
    if(!M::read(io,r,address,s.scratch,8))return false;
    if(L::read64(s.scratch)){M::fail(r,M::Failure::Changed);return false;}
    r.parentAttempted=true;
    if(!M::write(io,r,address+4,s.fixedChildren+off+4,4)||!M::write(io,r,address,s.fixedChildren+off,4)||
       !M::compare(io,r,address,s.fixedChildren+off,8,s.scratch))return false;
    ++r.linksPublished;
  }
  if(!M::compare(io,r,unsigned(L::NewBase),s.fixedChildren,s.goldenBytes,s.scratch)||!rootCompare(io,r,s))return false;
  r.verifiedChildBytes=s.goldenBytes;r.childrenVerified=true;
  if(!M::invalidate(io,r))return false;r.passed=true;return true;
}
inline bool fixedReady(const Result &r,unsigned bytes){
  return r.passed&&r.attempted&&r.modified&&r.failure==M::Failure::None&&r.windowSaved&&!r.windowRestored&&
    r.backingVerified&&r.verifiedBackingBytes==FixedBytes&&r.zeroedBytes==FixedBytes&&r.childrenVerified&&r.childBytes==bytes&&
    r.verifiedChildBytes==bytes&&r.parentAttempted&&r.linksPublished==3&&r.invalidation.passed&&r.invalidation.completed&&r.invalidation.commandAttempted;
}
template<class IO>bool contexts(IO &io,const C::Plan &golden,const P::Plan &plan,const Storage &s,const Result &prior,Result &r){
  if(!P::disjoint(&prior,sizeof(prior),&r,sizeof(r))||!P::disjoint(&plan,sizeof(plan),&r,sizeof(r))||!resetResult(s,golden,r))return false;
  if(!storage(s,golden,r)||!separate(s,&plan,sizeof(plan))||!separate(s,&prior,sizeof(prior))){M::fail(r,M::Failure::Storage);return false;}r.start=io.nowNs();
  if(!fixedReady(prior,s.goldenBytes)||!P::valid(plan,golden)){M::fail(r,M::Failure::Owner);return false;}
  // The merge validates the original capture, not a synthesized current root.
  // Re-derive fixed leaves separately so changed staging metadata fails closed.
  T::Result merged;
  if(!T::merge(s.root,unsigned(L::OldBytes),s.goldenChildren,s.goldenBytes,golden,plan,s.rootScratch,s.expectedScratch,s.fullChildren,unsigned(L::MaxChildBytes),merged)){
    M::fail(r,M::Failure::Plan);return false;
  }
  for(unsigned i=0;i<s.goldenBytes;++i){unsigned char expected=s.goldenChildren[i];
    if(i>=4096+8&&i<4096+32)expected=s.fullChildren[i];
    if(s.fixedChildren[i]!=expected){M::fail(r,M::Failure::Changed);return false;}
  }
  for(unsigned group=0;group<256;++group){const auto *old=s.fixedChildren+group*16,*next=s.fullChildren+group*16;
    if(!equal(old,next,16)&&(L::read64(old)||L::read64(old+8))){M::fail(r,M::Failure::Changed);return false;}
  }
  if(!io.ready()){M::fail(r,M::Failure::Owner);return false;}
  if(!io.claimContexts(plan)){M::fail(r,M::Failure::Replay);return false;}
  r.attempted=true;r.childBytes=merged.childBytes;
  unsigned window=~0U;if(!M::regRead(io,r,M::Window,window))return false;
  if(window){M::fail(r,M::Failure::WindowState);return false;}
  if(!rootCompare(io,r,s)||!M::compare(io,r,unsigned(L::NewBase),s.fixedChildren,s.goldenBytes,s.scratch)||
     !M::inspect(io,r,unsigned(L::NewBase)+s.goldenBytes,r.childBytes-s.goldenBytes,s.scratch))return false;
  // Every private range is inspected before the first context/backing write.
  for(const auto &b:plan.buffers)if(!M::inspect(io,r,unsigned(b.physical),unsigned(b.allocated),s.scratch))return false;
  for(const auto &b:plan.buffers)if(!M::zero(io,r,unsigned(b.physical),unsigned(b.allocated),s.scratch))return false;r.backingVerified=true;
  // No ring/USERD/instance/method write appears in this phase.
  if(!M::childPages(io,r,s.fullChildren,s.goldenBytes,r.childBytes,s.scratch)||!rootCompare(io,r,s)||
     !M::compare(io,r,unsigned(L::NewBase),s.fixedChildren,s.goldenBytes,s.scratch))return false;
  for(unsigned group=0;group<256;++group){const auto *entry=s.fullChildren+group*16;
    if(equal(s.fixedChildren+group*16,entry,16))continue;
    const unsigned address=unsigned(L::NewBase)+group*16;
    if(!M::read(io,r,address,s.scratch,16))return false;
    if(L::read64(s.scratch)||L::read64(s.scratch+8)){M::fail(r,M::Failure::Changed);return false;}
    r.parentAttempted=true;
    if(!M::write(io,r,address,entry,4)||!M::write(io,r,address+4,entry+4,4)||!M::write(io,r,address+12,entry+12,4)||
       !M::write(io,r,address+8,entry+8,4)||!M::compare(io,r,address,entry,16,s.scratch))return false;
    ++r.linksPublished;
  }
  if(!M::compare(io,r,unsigned(L::NewBase),s.fullChildren,r.childBytes,s.scratch)||!rootCompare(io,r,s))return false;
  r.verifiedChildBytes=r.childBytes;r.childrenVerified=true;
  if(!M::invalidate(io,r))return false;r.passed=true;return true;
}
}
