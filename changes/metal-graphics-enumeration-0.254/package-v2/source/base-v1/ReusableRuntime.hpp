#pragma once
#include "ReusableBacking.hpp"

namespace RtxReusableRuntime035 {
namespace N=RtxReusable035;namespace B=RtxReusableBacking035;namespace P=N::P;
constexpr unsigned RootPhysical=0x01002000,ChildrenPhysical=0x01005000;
constexpr unsigned Pages[9]={0x03400000,0x03402000,0x03403000,0x03409000,0x0340a000,0x0340b000,0x0340c000,0x0340d000,0x0340e000};
struct Storage {
 const uint8_t *root=nullptr,*children=nullptr,*library=nullptr,*code=nullptr;
 uint8_t *captureRoot=nullptr,*captureChildren=nullptr,*captureDevice=nullptr;
 unsigned childBytes=0;
};
struct Capture {bool attempted=false,passed=false;unsigned reads=0,bytes=0,lastAddress=0;uint64_t started=0,elapsed=0,serial=0;};
struct Window {
 bool attempted=false,saved=false,mutationAttempted=false,acquired=false,restoreAttempted=false,restored=false;
 unsigned before=~0U,selected=~0U,after=~0U;uint64_t serial=0,started=0,elapsed=0,cleanupStarted=0,cleanupElapsed=0;
};
// All storage is service-owned and allocated before firmware. The service must
// serialize prepare/open/submit/close and capture reads with the same mutex.
struct State {
 N::State core;B::Ledger backing;Storage storage;uint64_t generation=0,client=0;
 bool preparationAttempted=false,prepared=false,openingAttempted=false,opened=false,closed=false;
 Capture preparation,before,staged,completed;Window window;unsigned activeCapture=0;
 uint8_t hostRing[4096]={},scratch[4096]={};
};
inline bool bound(const State &state,const Storage &s){
 if(s.childBytes<8192||s.childBytes>B::MaxChildren||s.childBytes%4096)return false;
 const struct Span{const void *p;size_t n;} spans[]={{&state,sizeof(state)},{s.root,12288},{s.children,B::MaxChildren},
  {s.library,512},{s.code,4096},{s.captureRoot,12288},{s.captureChildren,B::MaxChildren},{s.captureDevice,B::DeviceBytes}};
 for(unsigned i=0;i<8;++i)for(unsigned j=0;j<i;++j)if(!P::separate(spans[i].p,spans[i].n,spans[j].p,spans[j].n))return false;
 return true;
}
inline B::View view(const State &s){return {s.storage.captureRoot,s.storage.captureChildren,s.storage.captureDevice,s.storage.childBytes};}
inline bool span(unsigned a,unsigned n,unsigned base,unsigned length){return n&&n<=4096&&!(a&3)&&!(n&3)&&a>=base&&a-base<length&&n<=length-(a-base);}
inline bool readable(const State &s,unsigned a,unsigned n){
 if(span(a,n,RootPhysical,12288)||span(a,n,ChildrenPhysical,s.storage.childBytes))return true;
 for(unsigned base:Pages)if(span(a,n,base,4096))return true;return false;
}
inline bool badWindow(unsigned v){return v==~0U||(v&0xffff0000U)==0xbadf0000U;}

// Backend methods perform the actual mapping/register access and independently
// check identity, pinned native ownership and the bootstrap/memory predicates.
template<class Backend>class Runtime {
 Backend &native;State &s;
 bool bootReady(){return !s.closed&&bound(s,s.storage)&&native.bootReady(s.generation,s.client,false);}
 bool live(){return s.opened&&!s.closed&&bound(s,s.storage)&&native.runtimeReady(s.generation,s.client);}
 bool tick(uint64_t start,uint64_t &elapsed,bool boot=false){
  if(!(boot?bootReady():live()))return false;const auto now=native.nowNs();
  if(now<start||now-start<elapsed)return false;elapsed=now-start;return elapsed<N::BudgetNs;
 }
 bool read(unsigned address,uint8_t *out,unsigned n,bool boot=false){
  return (boot?bootReady():live())&&out&&readable(s,address,n)&&native.readMemory(address,out,n);
 }
 bool capture(Capture &r,unsigned kind,bool boot=false){
  s.activeCapture=kind;
  r={};r.attempted=true;r.serial=boot?0:s.core.result().serial;r.started=boot?native.nowNs():s.core.result().started;r.elapsed=boot?0:s.core.result().elapsed;
  for(unsigned part=0;part<3;++part){
   const unsigned bytes=part==0?12288:part==1?s.storage.childBytes:B::DeviceBytes;
   auto *out=part==0?s.storage.captureRoot:part==1?s.storage.captureChildren:s.storage.captureDevice;
   for(unsigned off=0;off<bytes;off+=4096){
    if(!tick(r.started,r.elapsed,boot))return false;
    const unsigned address=part==0?RootPhysical+off:part==1?ChildrenPhysical+off:Pages[off/4096];
    ++r.reads;r.lastAddress=address;if(!read(address,out+off,4096,boot))return false;r.bytes+=4096;
    if(!tick(r.started,r.elapsed,boot))return false;
   }
  }
  r.passed=true;return true;
 }
 void retain(){s.closed=true;s.core.ownershipLost();s.backing.retain();native.retain();}
 struct DispatchIO {
  Runtime &r;
  uint64_t nowNs(){return r.native.nowNs();}
  bool ready(uint64_t gen,uint64_t client){return gen==r.s.generation&&client==r.s.client&&r.live();}
  bool selectWindow(){
   auto &w=r.s.window;w={};w.serial=r.s.core.result().serial;w.attempted=true;w.started=r.s.core.result().started;w.elapsed=r.s.core.result().elapsed;
   if(!r.tick(w.started,w.elapsed)||!r.native.readWindow(w.before)||!r.tick(w.started,w.elapsed))return false;
   if(badWindow(w.before)||w.before!=r.native.originalWindow())return false;w.saved=true;
   w.mutationAttempted=true; // Record before an uncertain MMIO write.
   if(!r.native.writeWindow(0)||!r.tick(w.started,w.elapsed)||!r.native.readWindow(w.selected)||!r.tick(w.started,w.elapsed)||w.selected)return false;
   w.acquired=true;return true;
  }
  bool restoreWindow(){
   auto &w=r.s.window;if(w.restoreAttempted)return false;
   if(!w.mutationAttempted)return true;
   w.restoreAttempted=true;w.cleanupStarted=r.native.nowNs();
   const auto &job=r.s.core.result();
   if(w.cleanupStarted<job.started||w.cleanupStarted-job.started<job.elapsed)return false;
   if(!w.saved||badWindow(w.before)||!r.tick(w.cleanupStarted,w.cleanupElapsed))return false;
   if(!r.native.writeWindow(w.before)||!r.tick(w.cleanupStarted,w.cleanupElapsed)||!r.native.readWindow(w.after)||
      !r.tick(w.cleanupStarted,w.cleanupElapsed)||w.after!=w.before)return false;
   w.restored=true;return true;
  }
  bool observe(N::Observation &o){
   auto *p=r.s.scratch;
   if(!r.read(N::PutPhysical-4,p,8))return false;o.get=P::get32(p);o.put=P::get32(p+4);
   if(!r.read(N::FencePhysical,p,8))return false;o.qmd=P::get64(p);
   if(!r.read(N::FencePhysical+16,p,8))return false;o.timeline=P::get64(p);return true;
  }
  bool verifyBefore(const N::Plan &p,uint64_t previous){return r.capture(r.s.before,2)&&r.s.backing.begin(p,r.s.core.request(),previous,view(r.s));}
  bool verifyStaged(const N::Plan &,uint64_t){return r.capture(r.s.staged,3)&&r.s.backing.staged(view(r.s));}
  bool verifyCompleted(const N::Plan &,const N::Request &){return r.capture(r.s.completed,4)&&r.s.backing.captured(view(r.s));}
  bool write(unsigned a,const uint8_t *p,unsigned n){
   return r.live()&&readable(r.s,a,n)&&r.s.backing.permitWrite(a,p,n)&&r.native.writeMemory(a,p,n);
  }
  bool match(unsigned a,const uint8_t *p,unsigned n){return n<=sizeof(r.s.scratch)&&r.read(a,r.s.scratch,n)&&B::equal(p,r.s.scratch,n);}
  bool notify(){return r.live()&&r.s.backing.permitNotify()&&r.native.notify();}
  void delayUs(unsigned us){r.native.delayUs(us);}
 };
public:
 Runtime(Backend &backend,State &state):native(backend),s(state){}
 bool prepare(uint64_t generation,uint64_t client,const Storage &storage){
  if(s.preparationAttempted||s.closed||!generation||!client||!bound(s,storage))return false;
  s.preparationAttempted=true;s.generation=generation;s.client=client;s.storage=storage;
  // The caller still owns bootstrap window restoration if preparation fails.
  auto fail=[&](){s.closed=true;s.backing.retain();return false;};
  if(!bootReady()||!capture(s.preparation,1,true))return fail();
  B::copy(s.hostRing,s.storage.captureDevice,4096);
  const B::Inputs inputs={storage.root,storage.children,s.hostRing,storage.library,storage.code,storage.childBytes};
  if(!s.backing.seed(generation,inputs,view(s))||!bootReady())return fail();
  s.prepared=true;return true;
 }
 // Called only after the original bootstrap window has actually been restored.
 bool open(){
  if(!s.prepared||s.openingAttempted||s.closed)return false;s.openingAttempted=true;
  unsigned window=~0U;
  if(!native.bootReady(s.generation,s.client,true)||!native.readWindow(window)||badWindow(window)||window!=native.originalWindow()||
     !native.bootReady(s.generation,s.client,true)||!native.beginRuntime(s.generation)||!native.runtimeReady(s.generation,s.client)||
     !s.core.open(s.generation,s.client,true)){retain();return false;}
  s.opened=true;return true;
 }
 N::Failure submit(uint64_t caller,const uint8_t *wire,size_t n){
  if(!s.opened||s.closed)return N::Failure::State;
  if(!P::separate(wire,n,&s,sizeof(s)))return N::Failure::Shape;
  const auto previous=s.core.completed();DispatchIO io{*this};const auto result=s.core.dispatch(io,caller,wire,n,s.storage.library,s.storage.code);
  if(s.core.phase()==N::Phase::Retained){retain();return result;}
  if(s.core.completed()!=previous){
   if(result!=N::Failure::None||!s.core.result().passed||!s.window.restored||!live()||!s.backing.finish(s.core.result())){retain();return N::Failure::Capture;}
  }
  return result;
 }
 bool close(uint64_t caller){if(!s.opened||s.closed||caller!=s.client)return false;s.core.close(caller);retain();return true;}
};
}
