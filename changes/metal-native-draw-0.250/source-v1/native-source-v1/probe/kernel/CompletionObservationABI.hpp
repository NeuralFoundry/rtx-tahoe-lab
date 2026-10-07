#pragma once
#include "ReusableRuntime.hpp"
namespace RtxCompletionObservation037 {
namespace R=RtxReusableRuntime035;namespace N=R::N;namespace P=N::P;
constexpr unsigned Selector=77,Bytes=512;
constexpr uint64_t Magic=UINT64_C(0x5254584f42533337);
inline void word(uint8_t *out,unsigned index,uint64_t value){P::Q::put64(out+index*8,value);}
inline void record(uint8_t *out,unsigned offset,const N::ObservationRecord &r){
 const uint64_t values[]={unsigned(r.stage),r.sequence,r.readAttempted,r.readPassed,r.complete,r.elapsedBefore,r.elapsedAfter,
  r.value.get,r.value.put,r.value.qmd,r.value.timeline};
 for(unsigned i=0;i<11;++i)word(out,offset+i,values[i]);
}
// A same-connection CPU snapshot only: no GPU reads, allocation or mutation.
// serial 0 is valid only before any attempted dispatch. Stale serials fail.
inline bool capture(const R::State *s,uint64_t generation,uint64_t serial,uint8_t *out,size_t n){
 if(!generation||!out||n!=Bytes||(s&&!P::separate(s,sizeof(*s),out,n)))return false;
 const N::Result *r=s?&s->core.result():nullptr;
 if(serial!=(r?r->serial:0)||(r&&r->attempted&&s->generation!=generation))return false;
 for(unsigned i=0;i<Bytes;++i)out[i]=0;
 word(out,0,Magic);word(out,1,1);word(out,2,generation);word(out,3,serial);word(out,4,s?unsigned(s->core.phase()):0);
 word(out,5,s?s->core.completed():0);word(out,23,R::ObservationOrder);word(out,26,s?s->backing.completed():0);word(out,27,s&&s->closed);
 if(!r||!r->attempted)return true;
 const bool passed=r->passed&&s->backing.completed()==serial;
 const auto failure=r->failure==N::Failure::None&&r->passed&&!passed?N::Failure::Capture:r->failure;
 const uint64_t values[]={unsigned(failure),r->attempted,passed,r->restored,r->writes,r->notifications,r->polls,r->operations,
  r->started,r->elapsed,r->observations,r->mismatchMask};
 for(unsigned i=0;i<12;++i)word(out,6+i,values[i]);
 const R::Capture *c=s->activeCapture==1?&s->preparation:s->activeCapture==2?&s->before:s->activeCapture==3?&s->staged:s->activeCapture==4?&s->completed:nullptr;
 if(c&&c->serial==serial){word(out,18,s->activeCapture);word(out,19,c->serial);word(out,20,c->passed);word(out,21,c->bytes);}
 word(out,22,r->notifications!=0);word(out,24,r->passed);word(out,25,unsigned(r->failure));word(out,26,s->backing.completed());word(out,27,s->closed);
 record(out,32,r->lastObservation);record(out,48,r->previousObservation);return true;
}
struct Call {
 const uint64_t *scalars=nullptr;size_t scalarCount=0;
 const uint8_t *input=nullptr;size_t inputBytes=0;uint8_t *output=nullptr;size_t outputBytes=0;
};
inline bool dispatch(const R::State *s,uint64_t generation,unsigned selector,const Call &call){
 if(selector!=Selector||call.scalarCount!=1||!call.scalars||call.inputBytes||!call.output||call.outputBytes!=Bytes||
    !P::separate(call.scalars,sizeof(uint64_t),call.output,Bytes)||!P::separate(&call,sizeof(call),call.output,Bytes))return false;
 return capture(s,generation,call.scalars[0],call.output,call.outputBytes);
}
}
