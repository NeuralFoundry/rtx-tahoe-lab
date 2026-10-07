#pragma once
#include <stdint.h>
#include <stddef.h>
#include "DispatchGeometry164.hpp"
#include "changes/gsp-program-library-0.33/ProgramLibrary.hpp"

// Native-owner core; serialized calls only, not a user-client ABI.
// admitMapping is a TRUSTED backend callback after allocation, DMA preparation
// and GPU mapping have succeeded. It neither allocates nor verifies that work.
// Completion/retirement callbacks likewise require actual backend evidence.
// Untrusted dispatches contain session-scoped handles and spans, never GPU VAs.
namespace RTXSpans165 {
constexpr uint64_t Page=4096;
enum Access : uint32_t { Read=1, Write=2 };
struct Mapping {
 uint64_t allocation=0, mapping=0, gpuVA=0, logicalBytes=0, mappedBytes=0;
 uint32_t access=0;
};
struct Binding { uint64_t handle=0, offset=0, bytes=0; uint32_t index=0; };
struct Plan {
 uint64_t session=0,ticket=0,invocations=0;
 uint64_t addresses[8]={},bytes[8]={};
 uint32_t count=0;
 RTXGeometry164::Size groups{},threads{};
};
inline bool access(uint32_t n){return n && !(n&~3u);}
inline bool span(uint64_t off,uint64_t len,uint64_t cap){return len&&off<=cap&&len<=cap-off;}
inline bool overlap(uint64_t a,uint64_t an,uint64_t b,uint64_t bn){return a<b+bn&&b<a+an;}
inline bool patch(const Plan&,const RtxProgram164::Program&,uint8_t*,size_t,uint8_t*,size_t);

template<size_t Buffers=64,size_t Jobs=16> class Owner {
 static_assert(Buffers>0&&Buffers<=UINT32_MAX&&Jobs>0,"bounded owner tables");
 enum class State { Empty, Ready, Retiring };
 enum class JobState { Empty, Prepared, Submitted, Faulted };
 struct Record {Mapping value{};uint32_t generation=0,refs=0;State state=State::Empty;};
 struct Job {uint64_t ticket=0;uint32_t slots[8]={};uint32_t count=0;JobState state=JobState::Empty;bool graphics=false;Plan plan{};RtxProgram164::Program program{};};
 Record records[Buffers]{};Job jobs[Jobs]{};
 const uint64_t session,vaBegin,vaEnd;
 uint64_t sequence=0;
 Owner(const Owner&)=delete;Owner& operator=(const Owner&)=delete;
 bool valid()const{return session&&vaBegin<vaEnd&&!(vaBegin%Page)&&!(vaEnd%Page);}
 uint64_t id(size_t i)const{return (uint64_t(records[i].generation)<<32)|(i+1);}
 bool resolve(uint64_t handle,size_t &i)const{
  const uint32_t low=uint32_t(handle);if(!low||low>Buffers)return false;i=low-1;
  return records[i].state==State::Ready&&id(i)==handle;
 }
 Job* job(uint64_t ticket){if(!ticket)return nullptr;for(auto &j:jobs)if(j.state!=JobState::Empty&&j.ticket==ticket)return &j;return nullptr;}
 void unpin(Job &j){for(uint32_t n=0;n<j.count;++n)--records[j.slots[n]].refs;j=Job{};}
public:
 Owner(uint64_t ownerSession,uint64_t begin,uint64_t end):session(ownerSession),vaBegin(begin),vaEnd(end){}
 // Scope belongs to this owner lifetime. The caller must never reuse a session
 // for a replacement owner; handles alone are deliberately not capabilities.
 bool admitMapping(const Mapping &m,uint64_t &handle){
  if(!valid()||!m.allocation||!m.mapping||!m.gpuVA||m.gpuVA%Page||!access(m.access)||
     !m.logicalBytes||m.logicalBytes>m.mappedBytes||!m.mappedBytes||m.mappedBytes%Page||
     m.gpuVA<vaBegin||m.gpuVA>=vaEnd||m.mappedBytes>vaEnd-m.gpuVA)return false;
  size_t free=Buffers;
  for(size_t i=0;i<Buffers;++i){const auto &r=records[i];
   if(r.state==State::Empty){if(free==Buffers&&r.generation!=UINT32_MAX)free=i;continue;}
   if(r.value.allocation==m.allocation||r.value.mapping==m.mapping||
      overlap(m.gpuVA,m.mappedBytes,r.value.gpuVA,r.value.mappedBytes))return false;
  }
  if(free==Buffers)return false;auto &r=records[free];++r.generation;r.value=m;r.state=State::Ready;handle=id(free);return true;
 }
 bool acquire(uint64_t ownerSession,const RtxProgram164::Program &p,const Binding *bindings,size_t count,
              RTXGeometry164::Size groups,RTXGeometry164::Size threads,Plan &out){
  if(!valid()||ownerSession!=session||!bindings||!p.parameters||p.parameters>8||count!=p.parameters||
     !RtxProgram164::separate(&out,sizeof(out),this,sizeof(*this))||
     !RtxProgram164::separate(&out,sizeof(out),&p,sizeof(p))||
     !RtxProgram164::separate(&out,sizeof(out),bindings,count*sizeof(Binding))||
     p.constantBytes!=0x160+p.parameters*8||!p.writeMask||sequence==UINT64_MAX)return false;
  RTXGeometry164::Shape shape;
  if(threads.x!=p.localX||threads.y!=p.localY||threads.z!=p.localZ||!RTXGeometry164::dispatch(groups,threads,shape))return false;
  // A pending/uncertain graphics command owns the shared queue transition.
  for(const auto&j:jobs)if(j.state!=JobState::Empty&&j.graphics)return false;
  Job *free=nullptr;for(auto &j:jobs)if(j.state==JobState::Empty){free=&j;break;}if(!free)return false;
  Plan next;next.session=session;next.ticket=sequence+1;next.count=p.parameters;next.groups=groups;next.threads=threads;next.invocations=shape.invocations;
  Job reservation;reservation.count=p.parameters;uint32_t mask=0;
  for(uint32_t n=0;n<p.parameters;++n){
   if(p.bindings[n]>=32||(n&&p.bindings[n]<=p.bindings[n-1]))return false;
   const uint32_t bit=1u<<p.bindings[n];mask|=bit;
   const Binding *b=nullptr;for(size_t k=0;k<count;++k)if(bindings[k].index==p.bindings[n]){if(b)return false;b=&bindings[k];}
   if(!b)return false;size_t i=0;if(!resolve(b->handle,i))return false;
   const auto &r=records[i];uint32_t needed=((p.readMask&bit)?Read:0u)|((p.writeMask&bit)?Write:0u);
   if((needed&~r.value.access)||!span(b->offset,b->bytes,r.value.logicalBytes)||r.refs>UINT32_MAX-8)return false;
   // No shader footprint is inferred here. Bounds protect the binding record;
   // hardware mappings/compiler robustness must enforce actual shader access.
   next.addresses[n]=r.value.gpuVA+b->offset;next.bytes[n]=b->bytes;reservation.slots[n]=uint32_t(i);
  }
  if((p.readMask|p.writeMask)&~mask)return false;
  // After this point nothing may fail. Multiple aliases retain the same record
  // once per binding; every completion/cancellation releases the same counts.
  ++sequence;reservation.ticket=sequence;reservation.state=JobState::Prepared;
  for(uint32_t n=0;n<reservation.count;++n)++records[reservation.slots[n]].refs;
  reservation.plan=next;reservation.program=p;*free=reservation;out=next;return true;
 }
 // Graphics ABI240 roles: program, vertex, color, fence, command. This uses
 // the same handle generations and retirement references as compute, with
 // an explicitly different job kind. No fake compute program or QMD exists.
 bool acquireGraphics(uint64_t ownerSession,const Binding*bindings,size_t count,Plan&out){
  if(!valid()||ownerSession!=session||!bindings||count!=5||sequence==UINT64_MAX||
     !RtxProgram164::separate(&out,sizeof(out),this,sizeof(*this))||
     !RtxProgram164::separate(&out,sizeof(out),bindings,count*sizeof(Binding)))return false;
  // Until a shared multi-engine scheduler is qualified, reserve the queue
  // transition exclusively, including during preparation and fault retention.
  for(const auto&j:jobs)if(j.state!=JobState::Empty)return false;
  Job*free=nullptr;for(auto&j:jobs)if(j.state==JobState::Empty){free=&j;break;}if(!free)return false;
  Plan next;next.session=session;next.ticket=sequence+1;next.count=5;
  Job reservation;reservation.count=5;reservation.graphics=true;
  for(uint32_t n=0;n<5;++n){
   const auto&b=bindings[n];size_t i=0;const uint32_t needed=(n==2||n==3)?Write:Read;
   if(b.index!=n||!resolve(b.handle,i))return false;
   const auto&r=records[i];
   if((needed&~r.value.access)||r.refs||!span(b.offset,b.bytes,r.value.logicalBytes))return false;
   for(uint32_t k=0;k<n;++k)if(reservation.slots[k]==i)return false;
   next.addresses[n]=r.value.gpuVA+b.offset;next.bytes[n]=b.bytes;reservation.slots[n]=uint32_t(i);
  }
  // Atomic commit: no generation, reference or output mutation on rejection.
  ++sequence;reservation.ticket=sequence;reservation.state=JobState::Prepared;
  for(uint32_t n=0;n<5;++n)++records[reservation.slots[n]].refs;
  reservation.plan=next;*free=reservation;out=next;return true;
 }
 bool patchPrepared(uint64_t ticket,uint8_t *qmd,size_t qmdBytes,uint8_t *constants,size_t constantBytes){
  auto *j=job(ticket);if(!j||j->state!=JobState::Prepared||j->graphics||
    !RtxProgram164::separate(qmd,qmdBytes,this,sizeof(*this))||
    !RtxProgram164::separate(constants,constantBytes,this,sizeof(*this)))return false;
  return patch(j->plan,j->program,qmd,qmdBytes,constants,constantBytes);
 }
 bool cancelPrepared(uint64_t ticket){auto *j=job(ticket);if(!j||j->state!=JobState::Prepared)return false;unpin(*j);return true;}
 bool markSubmitted(uint64_t ticket){auto *j=job(ticket);if(!j||j->state!=JobState::Prepared)return false;j->state=JobState::Submitted;return true;}
 bool completeVerified(uint64_t ticket){auto *j=job(ticket);if(!j||j->state!=JobState::Submitted)return false;unpin(*j);return true;}
 bool markFaulted(uint64_t ticket){auto *j=job(ticket);if(!j||j->state!=JobState::Submitted)return false;j->state=JobState::Faulted;return true;}
 // Trusted graphics backend after a potentially visible write or uncertain
 // window transition, even when PUT/doorbell were never reached.
 bool retainGraphicsUncertain(uint64_t ticket){
  auto*j=job(ticket);
  if(!j||!j->graphics||(j->state!=JobState::Prepared&&j->state!=JobState::Submitted))return false;
  j->state=JobState::Faulted;return true;
 }
 bool beginRetire(uint64_t ownerSession,uint64_t handle,Mapping &out){
  size_t i=0;if(ownerSession!=session||!resolve(handle,i)||records[i].refs)return false;
  auto &r=records[i];r.state=State::Retiring;out=r.value;return true;
 }
 // Only after confirmed unmap/TLB completion and backing release. A failed or
 // ambiguous backend cleanup leaves Retiring ownership intact indefinitely.
 bool retireVerified(uint64_t handle,uint64_t allocation,uint64_t mapping){
  uint32_t low=uint32_t(handle);if(!low||low>Buffers)return false;auto &r=records[low-1];
  if(r.state!=State::Retiring||id(low-1)!=handle||r.refs||r.value.allocation!=allocation||r.value.mapping!=mapping)return false;
  r.value=Mapping{};r.state=State::Empty;return true;
 }
 bool inspect(uint64_t handle,Mapping &out,uint32_t &refs)const{
  size_t i=0;if(!resolve(handle,i))return false;out=records[i].value;refs=records[i].refs;return true;
 }
};

// CPU-only command preparation. Caller supplies the previously admitted QMD
// and constant-bank templates. The Plan must still be pinned by its owner.
// Only geometry and pointer parameter bytes change; never submit legacy ABI1.
inline bool patch(const Plan &plan,const RtxProgram164::Program &p,
                  uint8_t *qmd,size_t qmdBytes,uint8_t *constants,size_t constantBytes){
 if(!plan.session||!plan.ticket||!plan.count||plan.count>8||plan.count!=p.parameters||
    p.constantBytes!=0x160+p.parameters*8||constantBytes<p.constantBytes||qmdBytes!=256||
    !RtxProgram164::separate(qmd,qmdBytes,constants,constantBytes)||
    !RtxProgram164::separate(qmd,qmdBytes,&plan,sizeof(plan))||!RtxProgram164::separate(constants,constantBytes,&plan,sizeof(plan))||
    !RtxProgram164::separate(qmd,qmdBytes,&p,sizeof(p))||!RtxProgram164::separate(constants,constantBytes,&p,sizeof(p)))return false;
 for(uint32_t n=0;n<plan.count;++n)if(!plan.addresses[n]||!plan.bytes[n]||plan.bytes[n]>UINT64_MAX-plan.addresses[n])return false;
 RTXGeometry164::Shape shape;
 if(!RTXGeometry164::dispatch(plan.groups,plan.threads,shape)||shape.invocations!=plan.invocations)return false;
 if(!RTXGeometry164::patch(qmd,qmdBytes,plan.groups,plan.threads,{p.localX,p.localY,p.localZ}))return false;
 for(uint32_t n=0;n<plan.count;++n)for(unsigned b=0;b<8;++b)constants[0x160+n*8+b]=uint8_t(plan.addresses[n]>>(8*b));
 return true;
}
}
