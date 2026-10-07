#pragma once
#include "ApplicationABI.hpp"
#include "../runtime/RuntimeAccess.hpp"
namespace RtxRuntimeABI032 {
using U64=unsigned long long;
namespace A=RtxApplication032;namespace X=RtxRuntimeAccess032;
constexpr U64 InfoMagic=0x5254585254493332ULL,JobMagic=0x52545852544a3332ULL;
constexpr unsigned InfoSelector=68,SubmitSelector=69,JobSelector=70,DataSelector=71,InfoBytes=512,JobBytes=1024;
inline bool selector(unsigned n){return n>=InfoSelector&&n<=DataSelector;}
inline void info(const X::State *s,const ChannelABI::Owner &owner,bool bootstrapCaptured,U64 *out){
 for(unsigned i=0;i<64;++i)out[i]=0;out[0]=InfoMagic;out[1]=1;out[2]=owner.generation;
 out[3]=s?unsigned(s->session.phase()):0;out[4]=s?s->session.completed():0;out[5]=4;out[6]=A::RequestBytes;
 out[7]=s&&s->prepared;out[8]=s&&s->opened;out[9]=bootstrapCaptured;
 out[10]=owner.phase;out[11]=owner.pinned;out[12]=owner.owned;out[13]=owner.command;out[14]=owner.lease;
 out[15]=s&&s->session.phase()==A::Phase::Ready;out[16]=s?s->queue.completed:0;
 out[17]=s&&s->session.phase()==A::Phase::Exhausted;out[18]=0; // Metal unverified.
}
inline void job(const X::State &s,unsigned j,const ApplicationCapture::Result &c,U64 *out){
 for(unsigned i=0;i<128;++i)out[i]=0;out[0]=JobMagic;out[1]=1;out[2]=s.generation;out[3]=j;out[4]=4;
 if(j>=4)return;const auto &slot=s.slots[j];const auto &w=slot.window;const auto &r=slot.stage;const auto &q=s.queue.jobs[j];
 out[5]=unsigned(s.session.phase());out[6]=s.session.completed();out[7]=s.requests[j].count;
 out[8]=w.attempted;out[9]=w.saved;out[10]=w.mutationAttempted;out[11]=w.acquired;out[12]=w.restoreAttempted;out[13]=w.restored;
 out[14]=w.failure;out[15]=w.cleanupFailure;out[16]=w.before;out[17]=w.selected;out[18]=w.after;out[19]=w.generation;out[20]=w.requestId;
 out[21]=w.started;out[22]=w.elapsed;out[23]=w.cleanupStarted;out[24]=w.cleanupElapsed;
 out[25]=slot.stageClaimed;out[26]=slot.stageWrites;out[27]=slot.submitClaimed;out[28]=slot.capturePassed;
 out[29]=r.attempted;out[30]=r.claimed;out[31]=r.cbAttempted;out[32]=r.outputAttempted;out[33]=r.readback;out[34]=r.committed;out[35]=r.passed;
 out[36]=r.failure;out[37]=r.slot;out[38]=r.operations;out[39]=r.reads;out[40]=r.writes;out[41]=r.lastAddress;out[42]=r.started;out[43]=r.elapsed;
 out[44]=q.claimed;out[45]=q.notified;out[46]=q.phase;out[47]=s.queue.completed;
 out[48]=c.attempted;out[49]=c.passed;out[50]=c.failure;out[51]=c.rootBytes;out[52]=c.childBytes;out[53]=c.deviceBytes;out[54]=c.requested;
 out[55]=c.reads;out[56]=c.lastAddress;out[57]=c.lastValue;out[58]=c.elapsed;
 out[59]=slot.submit.expectedCount;out[60]=slot.submit.completedElements;out[61]=slot.submit.completion;out[62]=slot.submit.passed;
}
// Read-only retrieval of service-owned evidence, never arbitrary addresses.
inline bool span(U64 offset,U64 length,unsigned total){return length&&length<=4096&&offset<=total&&length<=total-offset;}
}
