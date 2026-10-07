#pragma once
#include "RuntimeAccess.hpp"

// The real Mac backend and CPU fault model execute this same call-boundary
// coordinator. Backend methods perform actual I/O or explicit simulation.
namespace RtxRuntimeDispatch032 {
namespace A=RtxApplication032;namespace X=RtxRuntimeAccess032;
template<class Backend>A::Error execute(Backend &io,X::State &state,uint64_t caller,const unsigned char *bytes,size_t size){
 const auto error=state.session.accept(caller,bytes,size);if(error!=A::Error::Ok)return error;
 const unsigned j=state.session.completed();auto &slot=state.slots[j];
 state.requests[j]=state.session.active();
 bool ok=io.acquire(j,caller);
 if(ok)ok=io.stage(j);
 if(ok)ok=io.submit(j);
 // Preserve diagnostic captures after uncertain staging/publication too. A
 // later valid snapshot cannot erase the original execution failure.
 if(slot.window.acquired){slot.capturePassed=io.capture(j);ok=ok&&slot.capturePassed;}
 // Every accepted call reaches cleanup, including uncertain register writes.
 const bool restored=io.restore(j);
 if(!ok||!restored||!slot.window.restored||!io.proof()){
  state.session.ownershipLost();io.retain();return A::Error::Evidence;
 }
 if(!ApplicationQueueGate::finish(state.queue,j,state.session.active(),slot.submit)||!state.session.submitted(caller)){
  state.session.ownershipLost();io.retain();return A::Error::Evidence;
 }
 A::CompletionEvidence evidence;evidence.generation=state.generation;evidence.requestId=j+1;
 evidence.gpGet=slot.submit.get;evidence.gpPut=slot.submit.put;evidence.marker=slot.submit.completion;
 evidence.ownerHeld=io.proof();evidence.immutableVerified=slot.submit.immutableVerified;
 evidence.guardsVerified=slot.submit.guardsVerified;evidence.windowRestored=slot.window.restored;
 if(!state.session.finish(caller,evidence,slot.submit.output,64)){
  state.session.ownershipLost();io.retain();return A::Error::Evidence;
 }
 return A::Error::Ok;
}
}
