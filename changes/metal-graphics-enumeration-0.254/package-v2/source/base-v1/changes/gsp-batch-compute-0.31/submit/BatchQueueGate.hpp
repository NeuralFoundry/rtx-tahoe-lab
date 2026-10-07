#pragma once
#include "BatchSubmit.hpp"
// Monotonic four-job publication. A failed/uncertain operation consumes its
// permission; completion never authorizes recycling any of these four slots.
namespace BatchQueueGate {
struct Job {bool claimed=false,notified=false;unsigned phase=0;};
struct State {Job jobs[4];unsigned completed=0;};
inline bool ready(const State &s,unsigned j){return j<4&&s.completed==j;}
inline bool claim(State &s,unsigned j){
 if(!ready(s,j)||s.jobs[j].claimed||s.jobs[j].notified||s.jobs[j].phase)return false;
 s.jobs[j].claimed=true;return true;
}
inline bool write(State &s,unsigned j,unsigned address,const unsigned char *data,unsigned bytes){
 if(!ready(s,j)||!data)return false;auto &job=s.jobs[j];if(!job.claimed||job.notified)return false;
 unsigned char expected[32];
 if(job.phase==0){if(address!=BatchSubmit::commandPhysical(j)||bytes!=32||!BatchSubmit::command(j,expected))return false;}
 else if(job.phase==1){if(address!=BatchSubmit::entryPhysical(j)||bytes!=8||!BatchSubmit::entry(j,expected))return false;}
 else if(job.phase==2){if(address!=HostFence::Put||bytes!=4)return false;GSPComputePrep::put32(expected,j+2);}
 else return false;
 if(!ExecutionMemory::equal(data,expected,bytes))return false;++job.phase;return true;
}
inline bool notify(State &s,unsigned j){
 if(!ready(s,j))return false;auto &job=s.jobs[j];
 if(!job.claimed||job.notified||job.phase!=3)return false;job.notified=true;return true;
}
inline bool finish(State &s,unsigned j,const BatchSubmit::Result &r){
 if(!ready(s,j))return false;const auto &job=s.jobs[j];
 if(!job.claimed||!job.notified||job.phase!=3||r.job!=j||!r.passed||r.failure!=BatchSubmit::None||!r.claimed||
  !r.commandAttempted||!r.entryAttempted||!r.putAttempted||!r.bellAttempted||!r.immutableVerified||!r.guardsVerified||!r.stable||
  r.writes!=3||r.initialGet!=j+1||r.initialPut!=j+1||r.initialCompletion||!BatchSubmit::done(r))return false;
 ++s.completed;return true;
}
}
