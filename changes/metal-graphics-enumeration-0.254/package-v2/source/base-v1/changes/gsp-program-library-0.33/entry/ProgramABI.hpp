#pragma once
#include "../runtime/ProgramRuntime.hpp"
#include "../../gsp-channel-0.23/native/ChannelABI.hpp"
namespace RtxProgramABI033 {
using U64=unsigned long long;
namespace R=RtxProgramRuntime033;namespace P=RtxProgram033;namespace I=RtxProgramImage033;
constexpr U64 MemoryMagic=0x525458424d4d3333ULL,SubmitMagic=0x52545842534d3333ULL,CaptureMagic=0x5254584243503333ULL;
constexpr U64 InfoMagic=0x5254585254493333ULL,JobMagic=0x52545852544a3333ULL;
constexpr unsigned InfoSelector=68,SubmitSelector=69,JobSelector=70,DataSelector=71,InfoBytes=512,SubmitBytes=512,JobBytes=1024;
inline bool selector(unsigned n){return n>=InfoSelector&&n<=DataSelector;}
inline bool span(U64 offset,U64 length,unsigned total){return length&&length<=4096&&offset<=total&&length<=total-offset;}
inline void memory(const ProgramMemory::Result &r,const ChannelABI::Owner &o,unsigned writePhase,unsigned registerPhase,U64 *out){
 ChannelABI::memory(r.memory,2,o,out);out[0]=MemoryMagic;
 out[57]=r.hostVerified;out[58]=r.imagesPrepared;out[59]=r.claimed;out[60]=writePhase;out[61]=registerPhase;out[62]=ProgramMemory::Base;out[63]=ProgramMemory::Bytes;
}
inline void submit(const R::State &s,unsigned j,const ChannelABI::Owner &o,bool memoryReady,U64 *out){
 for(unsigned n=0;n<64;++n)out[n]=0;out[0]=SubmitMagic;out[1]=1;out[2]=o.generation;out[3]=j;
 if(j>=P::Slots)return;const auto &q=s.queue.jobs[j];const auto &r=q.result;
 out[4]=r.passed;out[5]=r.failure;out[6]=r.commandAttempted;out[7]=r.entryAttempted;out[8]=r.putAttempted;out[9]=r.bellAttempted;
 out[10]=r.immutableVerified;out[11]=r.guardsVerified;out[12]=r.stable;out[13]=r.operations;out[14]=r.reads;out[15]=r.writes;out[16]=r.polls;out[17]=r.token;
 out[18]=r.lastAddress;out[19]=r.initialGet;out[20]=r.initialPut;out[21]=r.initialCompletion;
 out[22]=r.get;out[23]=r.put;out[24]=r.hostFence;out[25]=r.completion;out[26]=r.attempted;out[27]=r.claimed;out[28]=r.started;out[29]=r.elapsed;
 out[30]=ProgramSubmit::commandPhysical(j);out[31]=ProgramSubmit::entryPhysical(j);out[32]=HostFence::Get;out[33]=HostFence::Put;
 out[34]=ProgramMemory::Base+I::dataOffset(j);out[35]=QmdProfile::FencePhysical+j*256;
 out[36]=P::commandVA(j);out[37]=P::bufferVA(j,0);out[38]=QmdProfile::FenceVA+j*256;out[39]=QmdProfile::ConstantVA+j*1024;out[40]=QmdProfile::QmdVA+j*256;
 out[41]=HostFence::Doorbell;out[42]=ProgramSubmit::BudgetNs;out[43]=ProgramSubmit::MaxOperations;
 out[44]=o.phase;out[45]=o.pinned;out[46]=o.owned;out[47]=o.command;out[48]=q.claimed;out[49]=q.notified;out[50]=q.phase;out[51]=s.queue.completed;out[52]=P::Slots;
 for(unsigned n=0;n<8;++n)out[53+n]=GSPComputePrep::get32(r.command+n*4);out[61]=GMMULeaves::read64(r.entry);out[62]=o.lease;out[63]=memoryReady;
}
inline void capture(const ProgramCapture::Result &r,const ChannelABI::Owner &o,U64 *out){
 for(unsigned n=0;n<64;++n)out[n]=0;out[0]=CaptureMagic;out[1]=1;out[2]=o.generation;
 out[3]=r.attempted;out[4]=r.passed;out[5]=r.failure;out[6]=r.rootBytes;out[7]=r.childBytes;out[8]=r.deviceBytes;out[9]=r.requested;
 out[10]=r.reads;out[11]=r.lastAddress;out[12]=r.lastValue;out[13]=r.elapsed;out[14]=o.phase;out[15]=o.pinned;out[16]=o.owned;out[17]=o.command;
 out[18]=GMMULeaves::OldBase;out[19]=GMMULeaves::NewBase;out[20]=ProgramCapture::RootBytes;out[21]=ProgramCapture::MaxChildren;out[22]=ProgramCapture::DeviceBytes;out[23]=ProgramCapture::BudgetNs;
 for(unsigned n=0;n<9;++n)out[24+n]=ProgramCapture::Addresses[n];
}
inline void info(const R::State *s,const ChannelABI::Owner &o,bool bootstrapCaptured,U64 *out){
 for(unsigned n=0;n<64;++n)out[n]=0;out[0]=InfoMagic;out[1]=1;out[2]=o.generation;
 out[3]=s?unsigned(s->access.session.phase()):0;out[4]=s?s->access.session.completed():0;out[5]=P::Slots;out[6]=RtxProgramRequest033::WireBytes;
 out[7]=s&&s->access.prepared;out[8]=s&&s->access.opened;out[9]=bootstrapCaptured;
 out[10]=o.phase;out[11]=o.pinned;out[12]=o.owned;out[13]=o.command;out[14]=o.lease;
 out[15]=s&&!s->closed&&s->access.session.phase()==RtxProgramSession033::Phase::Ready;out[16]=s?s->queue.completed:0;
 out[17]=s&&s->access.session.phase()==RtxProgramSession033::Phase::Exhausted;out[18]=0; // Metal unverified.
 out[19]=s?s->library.count:0;out[20]=512;out[21]=4096;out[22]=I::ImageBytes;out[23]=I::PlanBytes;out[24]=s&&s->closed;
 if(s){const auto &p=s->preparation;out[25]=p.attempted;out[26]=p.passed;out[27]=unsigned(p.failure);out[28]=p.reads;out[29]=p.bytes;out[30]=p.started;out[31]=p.elapsed;
  const auto &r=s->opening;out[32]=r.attempted;out[33]=r.passed;out[34]=unsigned(r.failure);out[35]=r.window;}
}
inline void job(const R::State &s,unsigned j,U64 *out){
 for(unsigned n=0;n<128;++n)out[n]=0;out[0]=JobMagic;out[1]=1;out[2]=s.access.generation;out[3]=j;out[4]=P::Slots;
 if(j>=P::Slots)return;const auto &a=s.access;const auto &slot=a.slots[j];const auto &w=slot.window;const auto &r=slot.stage;
 const auto &q=s.queue.jobs[j];const auto &c=s.captures[j].result;
 out[5]=unsigned(a.session.phase());out[6]=a.session.completed();out[7]=a.history[j].groups;
 out[8]=w.attempted;out[9]=w.saved;out[10]=w.mutationAttempted;out[11]=w.acquired;out[12]=w.restoreAttempted;out[13]=w.restored;
 out[14]=w.failure;out[15]=w.cleanupFailure;out[16]=w.before;out[17]=w.selected;out[18]=w.after;out[19]=w.generation;out[20]=w.requestId;
 out[21]=w.started;out[22]=w.elapsed;out[23]=w.cleanupStarted;out[24]=w.cleanupElapsed;
 out[25]=slot.stageClaimed;out[26]=slot.stageWrites;out[27]=q.claimed;out[28]=s.captureVerified[j];
 out[29]=r.attempted;out[30]=r.claimed;out[31]=r.dataAttempted;out[32]=r.cbAttempted;out[33]=r.qmdAttempted;out[34]=r.readback;out[35]=r.committed;out[36]=r.passed;
 out[37]=r.failure;out[38]=r.slot;out[39]=r.operations;out[40]=r.reads;out[41]=r.writes;out[42]=r.verifiedWrites;out[43]=r.lastAddress;out[44]=r.started;out[45]=r.elapsed;
 out[46]=q.claimed;out[47]=q.notified;out[48]=q.phase;out[49]=s.queue.completed;
 out[50]=c.attempted;out[51]=c.passed;out[52]=c.failure;out[53]=c.rootBytes;out[54]=c.childBytes;out[55]=c.deviceBytes;out[56]=c.requested;
 out[57]=c.reads;out[58]=c.lastAddress;out[59]=c.lastValue;out[60]=c.elapsed;
 out[61]=q.result.passed;out[62]=q.result.completion;out[63]=a.history[j].program;out[64]=a.history[j].id;out[65]=a.history[j].generation;
}
// Source spans are private preallocated captures. All sizes reflect completed
// reads or staged snapshots. The serializer's own buffers are never exposed.
inline bool data(const R::State &s,unsigned j,unsigned part,const uint8_t *plan,bool planReady,const uint8_t *&source,unsigned &bytes){
 source=nullptr;bytes=0;if(j>=P::Slots||part>=7)return false;
 const auto &c=s.captures[j];const auto &r=s.access.slots[j].stage;
 if(part==0){source=c.root;bytes=c.result.rootBytes;}
 else if(part==1){source=c.children;bytes=c.result.childBytes;}
 else if(part==2){source=c.device;bytes=c.result.deviceBytes;}
 else if(part==3){source=r.request;bytes=r.attempted?RtxProgramRequest033::WireBytes:0;}
 else if(part==4){source=plan;bytes=planReady?I::PlanBytes:0;}
 else if(part==5){source=s.access.storage.library;bytes=512;}
 else {source=s.access.storage.code;bytes=4096;}
 return source&&bytes;
}
}
