#pragma once
#include "ReusableRuntime.hpp"
namespace RtxReusableABI035 {
namespace R=RtxReusableRuntime035;namespace N=RtxReusable035;namespace B=RtxReusableBacking035;namespace P=N::P;
using U64=unsigned long long;
constexpr unsigned InfoSelector=68,SubmitSelector=69,JobSelector=70,DataSelector=71,InfoBytes=512,JobBytes=1024,PlanBytes=4096;
constexpr U64 InfoMagic=0x5254585254493335ULL,JobMagic=0x52545852544a3335ULL,PlanMagic=0x52545852504c3335ULL;
inline bool selector(unsigned n){return n>=InfoSelector&&n<=DataSelector;}
inline bool span(U64 offset,U64 length,unsigned total){return length&&length<=4096&&offset<=total&&length<=total-offset;}
inline const R::Capture *active(const R::State &s){
 return s.activeCapture==1?&s.preparation:s.activeCapture==2?&s.before:s.activeCapture==3?&s.staged:s.activeCapture==4?&s.completed:nullptr;
}
struct Snapshot {uint8_t *request=nullptr,*plan=nullptr;uint64_t serial=0;};
inline bool snapshot(const R::State &s,Snapshot &out){
 const auto &r=s.core.request();const auto &p=s.core.plan();
 if(!s.core.result().attempted||!r.serial||r.serial!=s.core.result().serial||p.serial!=r.serial||out.serial==r.serial)return false;
 const struct Span{const void *p;size_t n;} spans[]={{&s,sizeof(s)},{&out,sizeof(out)},{out.request,N::WireBytes},{out.plan,PlanBytes},{s.storage.library,512},{s.storage.code,4096},
  {s.storage.captureRoot,12288},{s.storage.captureChildren,B::MaxChildren},{s.storage.captureDevice,B::DeviceBytes}};
 for(unsigned i=0;i<9;++i)for(unsigned j=0;j<i;++j)if(!P::separate(spans[i].p,spans[i].n,spans[j].p,spans[j].n))return false;
 P::Library lib;if(!P::decode(s.storage.library,512,s.storage.code,4096,lib)||!N::encode(r,lib,out.request,N::WireBytes))return false;
 auto *b=out.plan;for(unsigned i=0;i<PlanBytes;++i)b[i]=0;
 P::Q::put64(b,PlanMagic);P::Q::put32(b+8,1);P::Q::put32(b+12,PlanBytes);P::Q::put64(b+16,r.generation);P::Q::put64(b+24,r.serial);
 const unsigned fields[]={r.program,r.groups,p.entry,p.put,p.launch.codeOffset,p.launch.codeBytes,p.launch.invocations,p.launch.parameters};
 for(unsigned i=0;i<8;++i)P::Q::put32(b+32+i*4,fields[i]);
 B::copy(b+64,p.data,2048);B::copy(b+2112,p.constant,1024);B::copy(b+3136,p.qmd,256);B::copy(b+3392,p.command,56);B::copy(b+3448,p.ringEntry,8);
 out.serial=r.serial;return true;
}
template<class Owner>inline void info(const R::State *s,const Owner &o,bool bootstrapCaptured,U64 *out){
 for(unsigned i=0;i<64;++i)out[i]=0;out[0]=InfoMagic;out[1]=1;out[2]=o.generation;
 out[3]=s?unsigned(s->core.phase()):0;out[4]=s?s->core.completed():0;out[5]=N::RingEntries;out[6]=N::WireBytes;
 out[7]=s&&s->prepared;out[8]=s&&s->opened;out[9]=bootstrapCaptured;out[10]=o.phase;out[11]=o.pinned;out[12]=o.owned;out[13]=o.command;out[14]=o.lease;
 out[15]=s&&!s->closed&&s->core.phase()==N::Phase::Ready;out[16]=s?s->backing.completed():0;out[17]=s&&s->core.phase()==N::Phase::Exhausted;
 out[18]=0; // Metal remains unverified.
 out[19]=s&&s->prepared?P::get32(s->storage.library+16):0;out[20]=512;out[21]=4096;out[22]=B::ImageBytes;out[23]=PlanBytes;out[24]=s&&s->closed;
 if(s){out[25]=s->preparationAttempted;out[26]=s->preparation.passed;out[27]=s->preparation.reads;out[28]=s->preparation.bytes;
  out[29]=s->preparation.started;out[30]=s->preparation.elapsed;out[31]=s->openingAttempted;out[32]=s->activeCapture;out[33]=s->core.result().serial;
  out[34]=unsigned(s->backing.phase());out[35]=s->storage.childBytes;}
}
inline bool job(const R::State &s,uint64_t serial,U64 *out){
 if(!serial||serial!=s.core.result().serial)return false;
 for(unsigned i=0;i<128;++i)out[i]=0;out[0]=JobMagic;out[1]=1;out[2]=s.generation;out[3]=serial;out[4]=unsigned(s.core.phase());out[5]=s.core.completed();out[6]=s.backing.completed();out[7]=s.closed;
 const auto &r=s.core.result();out[8]=r.attempted;out[9]=r.passed&&s.backing.completed()==serial;
 out[10]=unsigned(r.failure==N::Failure::None&&r.passed&&!out[9]?N::Failure::Capture:r.failure);
 out[11]=r.writes;out[12]=r.notifications;out[13]=r.operations;out[14]=r.polls;out[15]=r.elapsed;out[16]=r.started;out[17]=r.passed;
 const auto &w=s.window;out[20]=w.serial==serial;
 if(w.serial==serial){out[21]=w.attempted;out[22]=w.saved;out[23]=w.mutationAttempted;out[24]=w.acquired;out[25]=w.restoreAttempted;out[26]=w.restored;
  out[27]=w.before;out[28]=w.selected;out[29]=w.after;out[30]=w.started;out[31]=w.elapsed;out[32]=w.cleanupStarted;out[33]=w.cleanupElapsed;}
 const auto *c=active(s);
 if(c&&c->serial==serial){out[40]=s.activeCapture;out[41]=c->serial;out[42]=c->attempted;out[43]=c->passed;out[44]=c->reads;out[45]=c->bytes;out[46]=c->lastAddress;out[47]=c->started;out[48]=c->elapsed;}
 out[50]=s.before.serial==serial&&s.before.passed;out[51]=s.staged.serial==serial&&s.staged.passed;out[52]=s.completed.serial==serial&&s.completed.passed;
 out[60]=s.core.request().program;out[61]=s.core.request().groups;out[62]=s.core.plan().entry;out[63]=s.core.plan().put;return true;
}
inline bool data(const R::State &s,uint64_t serial,unsigned part,const Snapshot &snapshot,const uint8_t *&source,unsigned &bytes){
 source=nullptr;bytes=0;if(part>6)return false;
 if(part>=5){if(serial)return false;source=part==5?s.storage.library:s.storage.code;bytes=part==5?512:4096;return source!=nullptr;}
 if(!serial||serial!=s.core.result().serial)return false;
 if(part>=3){if(snapshot.serial!=serial)return false;source=part==3?snapshot.request:snapshot.plan;bytes=part==3?N::WireBytes:PlanBytes;return source!=nullptr;}
 const auto *c=active(s);if(!c||!c->attempted||c->serial!=serial)return false;
 const unsigned root=c->bytes<12288?c->bytes:12288,remaining=c->bytes-root,children=remaining<s.storage.childBytes?remaining:s.storage.childBytes;
 if(part==0){source=s.storage.captureRoot;bytes=root;}
 else if(part==1){source=s.storage.captureChildren;bytes=children;}
 else {source=s.storage.captureDevice;bytes=remaining-children;}
 return source&&bytes;
}
}
