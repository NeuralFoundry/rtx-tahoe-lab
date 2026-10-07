#pragma once
#include "ProgramStage.hpp"
#include "ProgramWindow.hpp"

// Private, preallocated service state. Both native and simulated I/O use this
// exact write gate. Callers cannot supply or reset these latches via a selector.
namespace RtxProgramAccess033 {
namespace P=RtxProgram033;namespace R=RtxProgramRequest033;namespace I=RtxProgramImage033;
namespace A=RtxProgramSession033;namespace S=RtxProgramStage033;namespace W=RtxProgramWindow033;
struct Slot {W::Result window;S::Result stage;bool stageClaimed=false;unsigned stageWrites=0;};
struct State {
 A::Session session;R::Request history[P::Slots];Slot slots[P::Slots];S::Storage storage;
 bool prepared=false,opened=false;uint64_t generation=0,client=0;
 I::Plan proofPlan;uint8_t proofScratch[4096]={};
};
inline bool bindingValid(const State &s){
 const auto &m=s.storage;
 if(m.libraryBytes!=512||m.codeBytes!=4096||m.canonicalBytes!=I::ImageBytes||m.scratchBytes!=4096)return false;
 const struct Span{const void *p;size_t n;} spans[]={{m.library,m.libraryBytes},{m.code,m.codeBytes},{m.canonical,m.canonicalBytes},{m.scratch,m.scratchBytes}};
 for(unsigned i=0;i<4;++i){
  if(!P::separate(spans[i].p,spans[i].n,&s,sizeof(s)))return false;
  for(unsigned j=0;j<i;++j)if(!P::separate(spans[i].p,spans[i].n,spans[j].p,spans[j].n))return false;
 }
 return true;
}
inline bool accepted(const State &s,unsigned j){
 if(j>=P::Slots||!bindingValid(s))return false;const auto &w=s.slots[j].window;
 return s.prepared&&s.opened&&s.session.phase()==A::Phase::Exposed&&s.session.completed()==j&&
  s.session.generation()==s.generation&&s.session.client()==s.client&&s.session.active().generation==s.generation&&
  s.session.active().id==j+1&&w.acquired&&w.saved&&!w.restoreAttempted&&!w.selected&&w.failure==W::None&&
  w.generation==s.generation&&w.requestId==j+1;
}
inline bool samePlan(const I::Plan &a,const I::Plan &b){
 const auto &x=a.launch,&y=b.launch;
 if(x.program!=y.program||x.slot!=y.slot||x.invocations!=y.invocations||x.codeOffset!=y.codeOffset||x.codeBytes!=y.codeBytes||
    x.registers!=y.registers||x.parameters!=y.parameters||x.programVA!=y.programVA||x.constantVA!=y.constantVA||
    x.qmdVA!=y.qmdVA||x.fenceVA!=y.fenceVA||x.entry!=y.entry)return false;
 for(unsigned i=0;i<P::MaxBindings;++i)if(x.buffers[i]!=y.buffers[i])return false;
 return S::equal(a.data,b.data,2048)&&S::equal(a.constant,b.constant,1024)&&S::equal(a.qmd,b.qmd,256)&&
  S::equal(a.command,b.command,32)&&S::equal(a.entry,b.entry,8);
}
inline bool claimStage(State &s,unsigned j,uint64_t generation){
 if(!accepted(s,j)||generation!=s.generation)return false;
 auto &slot=s.slots[j];const auto &r=slot.stage;
 if(slot.stageClaimed||slot.stageWrites||!r.attempted||r.claimed||r.failure!=S::None||r.slot!=j||r.reads||r.writes||r.committed)return false;
 const auto &m=s.storage;P::Library lib;
 if(!P::decode(m.library,m.libraryBytes,m.code,m.codeBytes,lib)||
    !R::encode(s.session.active(),lib,s.proofScratch,R::WireBytes)||!S::equal(s.proofScratch,r.request,R::WireBytes)||
    !I::plan(m.library,m.libraryBytes,m.code,m.codeBytes,s.session.active(),s.proofScratch,sizeof(s.proofScratch),s.proofPlan)||
    !samePlan(s.proofPlan,r.plan))return false;
 slot.stageClaimed=true;return true;
}
inline bool stageWrite(State &s,unsigned j,unsigned address,const uint8_t *data,unsigned bytes){
 if(!accepted(s,j)||!data)return false;
 auto &slot=s.slots[j];const auto &r=slot.stage;const unsigned phase=slot.stageWrites;
 if(!slot.stageClaimed||phase>=3||!r.attempted||!r.claimed||r.slot!=j||r.failure!=S::None||r.committed||r.passed||
    r.writes!=phase+1||r.verifiedWrites!=phase)return false;
 const unsigned offsets[]={I::dataOffset(j),I::constantOffset(j),I::qmdOffset(j)},lengths[]={2048,1024,256};
 const uint8_t *expected[]={r.plan.data,r.plan.constant,r.plan.qmd};
 const bool attempted[]={r.dataAttempted,r.cbAttempted,r.qmdAttempted};
 if(!attempted[phase]||address!=S::Base+offsets[phase]||bytes!=lengths[phase]||!S::equal(data,expected[phase],bytes))return false;
 ++slot.stageWrites;return true; // Consume before a possibly effective BAR1 write.
}
inline bool staged(const State &s,unsigned j){
 if(!accepted(s,j))return false;const auto &slot=s.slots[j];const auto &r=slot.stage;
 return slot.stageClaimed&&slot.stageWrites==3&&r.attempted&&r.claimed&&r.dataAttempted&&r.cbAttempted&&r.qmdAttempted&&
  r.readback&&r.committed&&r.passed&&r.failure==S::None&&r.slot==j&&r.writes==3&&r.verifiedWrites==3&&r.reads==73;
}
inline bool readable(unsigned j,unsigned address,unsigned bytes){
 if(j>=P::Slots||!bytes||bytes>256||(address&3)||(bytes&3))return false;
 const unsigned offsets[]={0,I::dataOffset(j),I::constantOffset(j),I::qmdOffset(j),I::fenceOffset(j)},lengths[]={4096,2048,1024,256,4};
 for(unsigned i=0;i<5;++i){const unsigned base=S::Base+offsets[i];
  if(address>=base&&address-base<lengths[i]&&bytes<=lengths[i]-(address-base))return true;
 }
 return false;
}
}
