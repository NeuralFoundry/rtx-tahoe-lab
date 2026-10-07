#pragma once
#include "ProgramSubmit.hpp"
namespace ProgramQueueGate {
namespace S=ProgramSubmit;namespace P=RtxProgram033;
struct Job {S::Result result;bool claimed=false,notified=false;unsigned phase=0;uint8_t command[32]={},entry[8]={};};
struct State {Job jobs[P::Slots];unsigned completed=0;};
inline bool ready(const State &s,unsigned j){return j<P::Slots&&s.completed==j;}
inline bool claim(State &s,unsigned j,const RtxProgramImage033::Plan &plan){
 if(!ready(s,j))return false;auto &job=s.jobs[j];const auto &r=job.result;
 if(job.claimed||job.notified||job.phase||!r.attempted||r.claimed||r.failure!=S::None||r.job!=j||plan.launch.slot!=j||
    !RtxProgramStage033::equal(r.command,plan.command,32)||!RtxProgramStage033::equal(r.entry,plan.entry,8))return false;
 for(unsigned i=0;i<32;++i)job.command[i]=plan.command[i];for(unsigned i=0;i<8;++i)job.entry[i]=plan.entry[i];
 job.claimed=true;return true;
}
inline bool write(State &s,unsigned j,unsigned address,const uint8_t *data,unsigned bytes){
 if(!ready(s,j)||!data)return false;auto &job=s.jobs[j];const auto &r=job.result;
 if(!job.claimed||job.notified||!r.claimed||r.failure!=S::None||r.passed)return false;
 uint8_t put[4];const uint8_t *expected=nullptr;
 if(job.phase==0){if(address!=S::commandPhysical(j)||bytes!=32||!r.commandAttempted)return false;expected=job.command;}
 else if(job.phase==1){if(address!=S::entryPhysical(j)||bytes!=8||!r.entryAttempted)return false;expected=job.entry;}
 else if(job.phase==2){if(address!=HostFence::Put||bytes!=4||!r.putAttempted)return false;P::Q::put32(put,j+2);expected=put;}
 else return false;
 if(r.writes!=job.phase+1||!RtxProgramStage033::equal(data,expected,bytes))return false;++job.phase;return true;
}
inline bool notify(State &s,unsigned j){
 if(!ready(s,j))return false;auto &job=s.jobs[j];
 if(!job.claimed||job.notified||job.phase!=3||!job.result.bellAttempted||job.result.failure!=S::None)return false;
 job.notified=true;return true;
}
inline bool finish(State &s,unsigned j){
 if(!ready(s,j))return false;const auto &job=s.jobs[j];const auto &r=job.result;
 if(!job.claimed||!job.notified||job.phase!=3||r.job!=j||!r.passed||r.failure!=S::None||!r.attempted||!r.claimed||
    !r.commandAttempted||!r.entryAttempted||!r.putAttempted||!r.bellAttempted||!r.immutableVerified||!r.guardsVerified||!r.stable||
    r.writes!=3||r.initialGet!=j+1||r.initialPut!=j+1||r.initialCompletion||!S::done(r))return false;
 ++s.completed;return true;
}
}
