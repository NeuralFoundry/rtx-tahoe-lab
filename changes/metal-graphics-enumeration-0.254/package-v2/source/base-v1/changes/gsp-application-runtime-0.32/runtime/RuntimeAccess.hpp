#pragma once
#include "../RuntimeStage.hpp"
#include "../submit/ApplicationQueueGate.hpp"
#include "RuntimeWindow.hpp"

// Address/byte authorization independent of macOS, shared by real and fake I/O.
// Persistent latches are separate from the caller-provided operation results.
namespace RtxRuntimeAccess032 {
namespace A=RtxApplication032;namespace I=RtxRuntimeImage032;namespace S=RtxRuntimeStage032;
namespace W=RtxRuntimeWindow032;namespace B=RtxBatch031;
struct Slot {
 W::Result window;S::Result stage;ApplicationSubmit::Result submit;
 bool stageClaimed=false,submitClaimed=false,capturePassed=false;
 unsigned stageWrites=0;
};
struct State {
 A::Session session;A::Request requests[4];Slot slots[4];ApplicationQueueGate::State queue;
 bool prepared=false,opened=false;uint64_t generation=0,client=0;
};
inline bool accepted(const State &s,unsigned j){
 return j<4&&s.opened&&s.session.phase()==A::Phase::Exposed&&s.session.completed()==j&&
  s.session.generation()==s.generation&&s.session.client()==s.client&&
  s.session.active().generation==s.generation&&s.session.active().requestId==j+1&&
  s.slots[j].window.acquired&&!s.slots[j].window.restoreAttempted;
}
inline bool claimStage(State &s,unsigned j,uint64_t gen){
 if(!accepted(s,j)||gen!=s.generation||s.slots[j].stageClaimed||s.slots[j].stageWrites||s.slots[j].submitClaimed)return false;
 unsigned char expected[A::RequestBytes];
 if(!A::encode(s.session.active(),expected,sizeof(expected))||!ExecutionMemory::equal(expected,s.slots[j].stage.request,sizeof(expected)))return false;
 s.slots[j].stageClaimed=true;return true;
}
inline bool stageWrite(State &s,unsigned j,unsigned address,const unsigned char *data,unsigned bytes){
 if(!accepted(s,j)||!s.slots[j].stageClaimed||!data||s.slots[j].submitClaimed)return false;
 auto &slot=s.slots[j];const auto &r=slot.stage;
 if(!r.attempted||!r.claimed||r.slot!=j||r.failure!=S::None||r.committed||slot.stageWrites>=2)return false;
 const unsigned phase=slot.stageWrites;
 if(address!=(phase?I::outputPhysical(j):I::constantPhysical(j))||bytes!=(phase?256U:1024U))return false;
 if(!ExecutionMemory::equal(data,r.plan+(phase?1024:0),bytes))return false;
 ++slot.stageWrites;return true; // Consumed before a possibly effective write.
}
inline bool staged(const State &s,unsigned j){
 if(!accepted(s,j))return false;const auto &slot=s.slots[j];const auto &r=slot.stage;
 return slot.stageClaimed&&slot.stageWrites==2&&r.attempted&&r.claimed&&r.cbAttempted&&r.outputAttempted&&
  r.readback&&r.committed&&r.passed&&r.failure==S::None&&r.slot==j&&r.writes==2&&r.reads==17;
}
inline bool claimSubmit(State &s,unsigned j){
 if(!staged(s,j)||s.slots[j].submitClaimed||!ApplicationQueueGate::claim(s.queue,j))return false;
 s.slots[j].submitClaimed=true;return true;
}
inline bool readable(unsigned address,unsigned bytes,unsigned childBytes){
 if(!bytes||bytes>4096||(address&3)||(bytes&3)||childBytes<8192||childBytes>GMMULeaves::MaxChildBytes||childBytes%4096)return false;
 struct Span{unsigned base,bytes;};
 const Span spans[]={{unsigned(GMMULeaves::OldBase),12288},{unsigned(GMMULeaves::NewBase),childBytes},
  {ApplicationMemory::Base,ApplicationMemory::Bytes},{HostFence::Ring,4096},{HostFence::Command,4096},
  {HostFence::Fence,4096},{HostFence::Get,8}};
 for(const auto &s:spans)if(address>=s.base&&address-s.base<s.bytes&&bytes<=s.bytes-(address-s.base))return true;
 return false;
}
}
