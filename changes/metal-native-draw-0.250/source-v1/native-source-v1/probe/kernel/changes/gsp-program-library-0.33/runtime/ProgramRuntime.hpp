#pragma once
#include "../queue/ProgramQueueGate.hpp"
#include "../queue/ProgramCapture.hpp"

namespace RtxProgramRuntime033 {
namespace P=RtxProgram033;namespace A=RtxProgramSession033;namespace X=RtxProgramAccess033;
namespace W=RtxProgramWindow033;namespace C=ProgramMemory;namespace Q=ProgramQueueGate;
namespace S=ProgramSubmit;namespace K=ProgramCapture;namespace I=RtxProgramImage033;
enum class Error:unsigned{Ok,Identity,State,Shape,Order,Evidence};
enum class Failure:unsigned{None,Arguments,State,Proof,Owner,Clock,Timeout,Read,Changed};
struct PrepareResult {bool attempted=false,passed=false;Failure failure=Failure::None;unsigned reads=0,bytes=0;uint64_t started=0,elapsed=0;};
struct OpenResult {bool attempted=false,passed=false;Failure failure=Failure::None;unsigned window=~0U;};
// All storage is allocated before firmware. The actual captured pages stay
// outside this private state and are never accepted as user-supplied pointers.
struct State {
 X::State access;Q::State queue;P::Library library;S::Storage submission;K::Storage captures[P::Slots];
 PrepareResult preparation;OpenResult opening;bool captureVerified[P::Slots]={},closed=false;
};
inline bool bound(const State &s,const C::Storage &m){
 const auto &a=s.access;const auto &p=s.submission;
 if(!X::bindingValid(a)||a.storage.library!=m.library||a.storage.code!=m.code||a.storage.canonical!=m.image||
    p.memory!=&m||p.history!=a.history||p.scratch!=a.proofScratch||p.scratchBytes!=4096||p.proofPlan!=&a.proofPlan||p.captureBytes!=I::ImageBytes)return false;
 for(unsigned j=0;j<P::Slots;++j)if(p.plans[j]!=&a.slots[j].stage.plan)return false;
 const struct Span{const void *p;size_t n;} ins[]={{&s,sizeof(s)},{&m,sizeof(m)},{m.library,512},{m.code,4096},
  {m.image,I::ImageBytes},{m.root,12288},{m.children,GMMULeaves::MaxChildBytes}};
 Span outs[14]={{a.storage.scratch,4096},{p.capture,I::ImageBytes}};
 for(unsigned j=0;j<P::Slots;++j){const auto &c=s.captures[j];
  outs[2+j*3]={c.root,12288};outs[3+j*3]={c.children,GMMULeaves::MaxChildBytes};outs[4+j*3]={c.device,K::DeviceBytes};
 }
 for(unsigned i=0;i<14;++i){
  for(const auto &in:ins)if(!P::separate(outs[i].p,outs[i].n,in.p,in.n))return false;
  for(unsigned j=0;j<i;++j)if(!P::separate(outs[i].p,outs[i].n,outs[j].p,outs[j].n))return false;
 }
 return true;
}
inline Error map(A::Error e){
 switch(e){case A::Error::Ok:return Error::Ok;case A::Error::Identity:return Error::Identity;
  case A::Error::State:return Error::State;case A::Error::Shape:return Error::Shape;case A::Error::Order:return Error::Order;}
 return Error::Evidence;
}
template<class IO>bool prepare(IO &io,State &s,const C::Storage &m,const C::Result &memory,
 const ExecutionTransactions::Result &execution,const HostFence::Result &host,uint64_t generation,uint64_t client){
 auto &r=s.preparation;auto &a=s.access;
 if(!bound(s,m)||r.attempted||r.failure!=Failure::None||s.closed||a.prepared||a.opened||a.session.phase()!=A::Phase::Empty)return false;
 auto fail=[&](Failure f){if(r.failure==Failure::None)r.failure=f;return false;};
 if(!generation||!client)return fail(Failure::Arguments);
 if(!C::ready(memory,m.liveBytes)||!C::hostReady(host,execution)||!S::imageProfile(s.submission,0)||
    !P::decode(m.library,512,m.code,4096,s.library))return fail(Failure::Proof);
 r.attempted=true;r.started=io.nowNs();
 auto ready=[&](){
  if(!io.ready(generation))return fail(Failure::Owner);const auto now=io.nowNs();
  if(now<r.started||now-r.started<r.elapsed)return fail(Failure::Clock);r.elapsed=now-r.started;
  return r.elapsed<W::BudgetNs||fail(Failure::Timeout);
 };
 for(unsigned at=0;at<I::ImageBytes;at+=256){
  if(!ready())return false;++r.reads;
  if(!io.readMemory(C::Base+at,s.submission.capture+at,256))return fail(Failure::Read);
  r.bytes+=256;if(!ready())return false;
 }
 if(!I::capture(s.submission.capture,I::ImageBytes,m.image,I::ImageBytes,s.library,a.history,0,0))return fail(Failure::Changed);
 if(!ready())return false;a.generation=generation;a.client=client;a.prepared=true;r.passed=true;return true;
}
template<class IO>bool open(IO &io,State &s,unsigned expectedWindow){
 auto &a=s.access;auto &r=s.opening;
 if(s.closed||r.attempted||r.failure!=Failure::None||!s.preparation.passed||!a.prepared||a.opened||a.session.phase()!=A::Phase::Empty)return false;
 r.attempted=true;
 auto fail=[&](Failure f){r.failure=f;s.closed=true;a.session.ownershipLost();io.retain();return false;};
 if(W::bad(expectedWindow))return fail(Failure::Arguments);
 if(!io.openProof(a.generation))return fail(Failure::Owner);
 if(!io.readWindow(r.window))return fail(Failure::Read);
 if(r.window!=expectedWindow)return fail(Failure::Changed);
 if(!io.openProof(a.generation))return fail(Failure::Owner);
 A::Bootstrap e;e.generation=a.generation;e.client=a.client;e.owner=e.firmware=e.host=e.library=e.storage=e.windowRestored=true;
 if(!a.session.open(e)||!io.beginRuntime(a.generation)||!io.runtimeProof(a.generation))return fail(Failure::Owner);
 a.opened=true;r.passed=true;return true;
}
template<class IO>Error dispatch(IO &io,State &s,uint64_t caller,const uint8_t *wire,size_t bytes){
 auto &a=s.access;
 if(s.closed||!s.preparation.passed||!s.opening.passed||!a.prepared||!a.opened)return Error::State;
 const auto accepted=a.session.accept(caller,wire,bytes,s.library);if(accepted!=A::Error::Ok)return map(accepted);
 const unsigned j=a.session.completed();auto &slot=a.slots[j];a.history[j]=a.session.active();
 bool ok=io.acquire(j,caller);if(ok)ok=io.stage(j);if(ok)ok=io.submit(j);
 // Capture even after a failed operation. Successful diagnostics cannot erase
 // the original failure or manufacture a completed submission.
 if(slot.window.acquired){s.captureVerified[j]=io.capture(j);ok=ok&&s.captureVerified[j];}
 const bool restored=io.restore(j);
 auto fail=[&](){s.closed=true;a.session.ownershipLost();io.retain();return Error::Evidence;};
 if(!ok||!restored||!slot.window.restored||!io.proof())return fail();
 if(!Q::finish(s.queue,j)||!a.session.submitted(caller))return fail();
 const auto &r=s.queue.jobs[j].result;A::Completion e;e.generation=a.generation;e.id=j+1;
 e.get=r.get;e.put=r.put;e.marker=r.completion;e.owner=io.proof();e.capture=s.captureVerified[j];e.windowRestored=slot.window.restored;
 if(!a.session.finish(caller,e)||!io.proof())return fail();return Error::Ok;
}
template<class IO>bool close(IO &io,State &s,uint64_t caller){
 if(s.closed||!s.access.prepared||!s.access.client||caller!=s.access.client)return false;
 s.closed=true;s.access.session.close(caller);io.retain();return true;
}
}
