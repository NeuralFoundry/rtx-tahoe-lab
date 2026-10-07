#pragma once
#include "RuntimeImage.hpp"

// Bounded per-request input staging, before queue publication. CPU-testable I/O
// contract only. An actual adapter must bind claim/ready to the accepted Session,
// generation, exclusive owner, physical BAR1 window and the unused slot.
namespace RtxRuntimeStage032 {
namespace A=RtxApplication032;namespace I=RtxRuntimeImage032;namespace B=RtxBatch031;namespace Q=QmdProfile;
constexpr unsigned MaxOperations=128;
constexpr uint64_t BudgetNs=UINT64_C(5000000000);
enum Failure:unsigned{None,Arguments,Request,Owner,Replay,Clock,Timeout,Read,Changed,Write};
struct Result {
 bool attempted=false,claimed=false,cbAttempted=false,outputAttempted=false,readback=false,committed=false,passed=false;
 Failure failure=None;unsigned slot=0,operations=0,reads=0,writes=0,lastAddress=0;
 uint64_t started=0,elapsed=0;
 uint8_t request[A::RequestBytes]={},plan[I::PlanBytes]={};
};
inline bool fail(Result &r,Failure reason){if(r.failure==None)r.failure=reason;r.passed=false;return false;}
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
 }return true;
}
template<class IO>bool write(IO &io,Result &r,unsigned address,const uint8_t *data,unsigned bytes,bool &latch){
 if(!tick(io,r))return false;latch=true;++r.writes;r.lastAddress=address;
 if(!io.writeMemory(address,data,bytes))return fail(r,Write);
 return match(io,r,address,data,bytes);
}
template<class IO>bool execute(IO &io,const A::Request &request,uint8_t *canonical,size_t bytes,Result &r){
 // Failed and successful attempts both retain their intent/evidence. A new
 // result object alone must not reopen a slot; the I/O adapter also owns claim.
 if(r.attempted||r.failure!=None)return false;
 if(bytes!=I::ImageBytes||!A::separate(canonical,bytes,&r,sizeof(r))||
    !A::separate(&request,sizeof(request),canonical,bytes)||!A::separate(&request,sizeof(request),&r,sizeof(r)))return false;
 if(!A::valid(request))return fail(r,Request);
 r.slot=unsigned(request.requestId-1);
 if(!A::encode(request,r.request,sizeof(r.request))||!I::slot(request,r.slot,r.plan,1024,r.plan+1024,256))return fail(r,Request);
 r.attempted=true;r.started=io.nowNs();if(!tick(io,r))return false;
 if(!io.claim(r.slot,request.generation))return fail(r,Replay);r.claimed=true;
 // Read original bytes before any writes. Queue publication and all remaining
 // code/PTE/guard checks are responsibilities of the subsequent submit phase.
 if(!match(io,r,I::constantPhysical(r.slot),canonical+B::cbOffset(r.slot),1024)||
    !match(io,r,I::outputPhysical(r.slot),canonical+B::outOffset(r.slot),256)||
    !match(io,r,Q::FencePhysical+r.slot*256,canonical+B::fenceOffset(r.slot),4)||
    !match(io,r,Q::QmdPhysical+r.slot*256,canonical+B::qmdOffset(r.slot),256))return false;
 if(!write(io,r,I::constantPhysical(r.slot),r.plan,1024,r.cbAttempted)||
    !write(io,r,I::outputPhysical(r.slot),r.plan+1024,256,r.outputAttempted))return false;
 // Both writes were individually read back. Recheck them together before
 // committing the CPU expected image, including a final fresh owner check.
 if(!match(io,r,I::constantPhysical(r.slot),r.plan,1024)||!match(io,r,I::outputPhysical(r.slot),r.plan+1024,256)||!tick(io,r))return false;
 r.readback=true;
 for(unsigned i=0;i<1024;++i)canonical[B::cbOffset(r.slot)+i]=r.plan[i];
 for(unsigned i=0;i<256;++i)canonical[B::outOffset(r.slot)+i]=r.plan[1024+i];
 r.committed=true;r.passed=true;return true;
}
}
