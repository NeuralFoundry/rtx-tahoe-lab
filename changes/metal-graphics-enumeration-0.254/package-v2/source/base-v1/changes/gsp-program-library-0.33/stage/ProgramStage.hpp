#pragma once
#include "../ProgramImage.hpp"

// Service-owned request and preallocated plan/scratch; no queue publication.
namespace RtxProgramStage033 {
namespace P=RtxProgram033;namespace R=RtxProgramRequest033;namespace I=RtxProgramImage033;
constexpr uint64_t BudgetNs=UINT64_C(5000000000);
constexpr unsigned MaxOperations=256,Base=P::Q::ProgramPhysical;
enum Failure:unsigned{None,Arguments,Request,Owner,Replay,Clock,Timeout,Read,Changed,Write};
struct Storage {
 const uint8_t *library=nullptr,*code=nullptr;uint8_t *canonical=nullptr,*scratch=nullptr;
 size_t libraryBytes=0,codeBytes=0,canonicalBytes=0,scratchBytes=0;
};
struct Result {
 bool attempted=false,claimed=false,dataAttempted=false,cbAttempted=false,qmdAttempted=false,readback=false,committed=false,passed=false;
 Failure failure=None;unsigned slot=0,operations=0,reads=0,writes=0,verifiedWrites=0,lastAddress=0;
 uint64_t started=0,elapsed=0;uint8_t request[R::WireBytes]={};I::Plan plan;
};
inline bool equal(const uint8_t *a,const uint8_t *b,size_t n){for(size_t i=0;i<n;++i)if(a[i]!=b[i])return false;return true;}
inline bool fail(Result &r,Failure f){if(r.failure==None)r.failure=f;r.passed=false;return false;}
inline bool storage(const Storage &s,const R::Request &request,const Result &r){
 if(s.libraryBytes!=512||s.codeBytes!=4096||s.canonicalBytes!=I::ImageBytes||s.scratchBytes!=4096)return false;
 const struct Span {const void *p;size_t n;} ins[]={{s.library,s.libraryBytes},{s.code,s.codeBytes},{&request,sizeof(request)},{&s,sizeof(s)}},
 outs[]={{s.canonical,s.canonicalBytes},{s.scratch,s.scratchBytes},{&r,sizeof(r)}};
 for(unsigned i=0;i<3;++i){
  for(unsigned j=0;j<4;++j)if(!P::separate(outs[i].p,outs[i].n,ins[j].p,ins[j].n))return false;
  for(unsigned j=0;j<i;++j)if(!P::separate(outs[i].p,outs[i].n,outs[j].p,outs[j].n))return false;
 }
 return P::separate(s.library,s.libraryBytes,s.code,s.codeBytes);
}
template<class IO>bool tick(IO &io,Result &r){
 if(!io.ready())return fail(r,Owner);const auto now=io.nowNs();
 if(now<r.started||now-r.started<r.elapsed)return fail(r,Clock);r.elapsed=now-r.started;
 if(r.elapsed>=BudgetNs||r.operations>=MaxOperations)return fail(r,Timeout);++r.operations;return true;
}
template<class IO>bool match(IO &io,Result &r,unsigned address,const uint8_t *expected,unsigned bytes){
 uint8_t data[256];
 for(unsigned at=0;at<bytes;at+=256){
  const unsigned n=bytes-at<256?bytes-at:256;if(!tick(io,r))return false;
  ++r.reads;r.lastAddress=address+at;if(!io.readMemory(address+at,data,n))return fail(r,Read);
  if(!tick(io,r))return false;
  for(unsigned i=0;i<n;++i)if(data[i]!=expected[at+i]){r.lastAddress=address+at+i;return fail(r,Changed);}
 }
 return true;
}
template<class IO>bool write(IO &io,Result &r,unsigned a,const uint8_t *p,unsigned n,bool &latch){
 if(!tick(io,r))return false;latch=true;++r.writes;r.lastAddress=a;
 if(!io.writeMemory(a,p,n))return fail(r,Write);
 if(!match(io,r,a,p,n))return false;++r.verifiedWrites;return true;
}
template<class IO>bool execute(IO &io,const R::Request &request,const Storage &s,Result &r){
 // Check aliasing before reading latches, including a Result over caller bytes.
 if(!storage(s,request,r))return false;
 if(r.attempted||r.failure!=None)return false;
 P::Library lib;
 if(!P::decode(s.library,s.libraryBytes,s.code,s.codeBytes,lib)||!R::valid(request,lib))return fail(r,Request);
 r.slot=unsigned(request.id-1);
 if(!equal(s.canonical,s.code,4096)||P::get32(s.canonical+I::fenceOffset(r.slot)))return fail(r,Arguments);
 if(!R::encode(request,lib,r.request,sizeof(r.request))||
    !I::plan(s.library,s.libraryBytes,s.code,s.codeBytes,request,s.scratch,s.scratchBytes,r.plan))return fail(r,Request);
 r.attempted=true;r.started=io.nowNs();if(!tick(io,r))return false;
 if(!io.claim(r.slot,request.generation))return fail(r,Replay);r.claimed=true;
 const unsigned d=I::dataOffset(r.slot),c=I::constantOffset(r.slot),q=I::qmdOffset(r.slot),f=I::fenceOffset(r.slot);
 if(!match(io,r,Base,s.code,4096)||!match(io,r,Base+d,s.canonical+d,2048)||
    !match(io,r,Base+c,s.canonical+c,1024)||!match(io,r,Base+q,s.canonical+q,256)||!match(io,r,Base+f,s.canonical+f,4))return false;
 if(!write(io,r,Base+d,r.plan.data,2048,r.dataAttempted)||!write(io,r,Base+c,r.plan.constant,1024,r.cbAttempted)||
    !write(io,r,Base+q,r.plan.qmd,256,r.qmdAttempted))return false;
 // Recheck all this slot's inputs/QMD together and immutable code/fence before
 // updating the CPU expectation. Other slots/PTEs are checked by full capture.
 if(!match(io,r,Base+d,r.plan.data,2048)||!match(io,r,Base+c,r.plan.constant,1024)||!match(io,r,Base+q,r.plan.qmd,256)||
    !match(io,r,Base,s.code,4096)||!match(io,r,Base+f,s.canonical+f,4)||!tick(io,r))return false;
 r.readback=true;if(!I::commit(r.plan,s.canonical,s.canonicalBytes))return fail(r,Arguments);
 r.committed=true;r.passed=true;return true;
}
}
