#pragma once
#include "../ProgramSession.hpp"

// One lease for each accepted request. A failed or completed lease cannot be
// reopened with the same result. The enclosing service owns the four results.
namespace RtxProgramWindow033 {
namespace A=RtxProgramSession033;
constexpr uint64_t BudgetNs=UINT64_C(5000000000);
enum Failure:unsigned {None,State,Owner,Clock,Timeout,Read,Changed,Write};
struct Result {
 bool attempted=false,saved=false,mutationAttempted=false,acquired=false;
 bool restoreAttempted=false,restored=false;
 Failure failure=None,cleanupFailure=None;
 uint32_t before=~0U,selected=~0U,after=~0U;
 uint64_t generation=0,requestId=0,started=0,elapsed=0,cleanupStarted=0,cleanupElapsed=0;
};
inline bool bad(uint32_t value){return value==~0U||(value&0xffff0000U)==0xbadf0000U;}
inline bool fail(Result &r,Failure f,bool cleanup=false){auto &target=cleanup?r.cleanupFailure:r.failure;if(target==None)target=f;return false;}
template<class IO>bool tick(IO &io,Result &r,bool cleanup=false){
 if(!io.windowOwned())return fail(r,Owner,cleanup);
 const uint64_t now=io.nowNs(),start=cleanup?r.cleanupStarted:r.started;
 auto &elapsed=cleanup?r.cleanupElapsed:r.elapsed;
 if(now<start||now-start<elapsed)return fail(r,Clock,cleanup);
 elapsed=now-start;return elapsed<BudgetNs||fail(r,Timeout,cleanup);
}
template<class IO>bool acquire(IO &io,A::Session &session,uint64_t caller,uint32_t expected,Result &r){
 if(r.attempted||r.failure!=None)return false;
 if(caller!=session.client()||session.phase()!=A::Phase::Accepted||bad(expected))return fail(r,State);
 r.attempted=true;r.generation=session.generation();r.requestId=session.active().id;
 r.started=io.nowNs();if(!tick(io,r))return false;
 if(!io.readWindow(r.before))return fail(r,Read);
 if(!tick(io,r))return false;
 if(bad(r.before)||r.before!=expected)return fail(r,Changed);r.saved=true;
 // The application state records exposure before the first possible MMIO write.
 if(!session.beginMutation(caller))return fail(r,State);r.mutationAttempted=true;
 if(!io.writeWindow(0))return fail(r,Write);
 if(!tick(io,r))return false;
 if(!io.readWindow(r.selected))return fail(r,Read);
 if(!tick(io,r))return false;
 if(r.selected)return fail(r,Changed);r.acquired=true;return true;
}
template<class IO>bool restore(IO &io,Result &r){
 if(r.restoreAttempted)return false;
 if(!r.mutationAttempted)return true;
 r.restoreAttempted=true;r.cleanupStarted=io.nowNs();
 if(!r.saved||bad(r.before))return fail(r,State,true);
 // Cleanup uses its own bounded clock even if the request exhausted its budget.
 if(!tick(io,r,true))return false;
 if(!io.writeWindow(r.before))return fail(r,Write,true);
 if(!tick(io,r,true))return false;
 if(!io.readWindow(r.after))return fail(r,Read,true);
 if(!tick(io,r,true))return false;
 if(r.after!=r.before)return fail(r,Changed,true);r.restored=true;return true;
}
}
