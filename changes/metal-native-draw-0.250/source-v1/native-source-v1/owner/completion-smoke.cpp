#include "CompletionObservationABI.hpp"
extern "C" bool rtx_completion_snapshot(const RtxReusableRuntime035::State *state,uint64_t generation,uint64_t serial,uint8_t *out,size_t bytes){
 return RtxCompletionObservation037::capture(state,generation,serial,out,bytes);
}
extern "C" bool rtx_completion_call(const RtxReusableRuntime035::State *state,uint64_t generation,unsigned selector,const RtxCompletionObservation037::Call *call){
 return call&&RtxCompletionObservation037::dispatch(state,generation,selector,*call);
}
