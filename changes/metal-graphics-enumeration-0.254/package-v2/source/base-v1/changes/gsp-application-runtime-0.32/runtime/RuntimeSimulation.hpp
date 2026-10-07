#pragma once
#include "../submit/ApplicationMemorySimulation.hpp"
#include "RuntimeDispatch.hpp"
#include "../entry/ApplicationCapture.hpp"
#include <memory>
namespace A=RtxApplication032;namespace I=RtxRuntimeImage032;namespace W=RtxRuntimeWindow032;
namespace X=RtxRuntimeAccess032;namespace S=RtxRuntimeStage032;namespace BS=ApplicationSubmit;namespace AppGate=ApplicationQueueGate;
static unsigned runtimeScenarios=0,faults=0;
struct RuntimeIO {
 X::State state;Bytes root,tables,image,backing,queue,capture=Bytes(CM::Bytes);
 Bytes captureRoot=Bytes(12288),captureChildren=Bytes(GMMULeaves::MaxChildBytes),captureDevice=Bytes(ApplicationCapture::DeviceBytes);
 ApplicationCapture::Result captured;
 CM::Storage storage;CM::Result memory;const TX::Result &execution;const H::Result &host;
 unsigned job=0,window=0x42,ops=0,clocks=0,ownerChecks=0,reads=0,writes=0,bells=0,polls=0;
 unsigned failAt=0,loseAt=0,clockFault=0,timeoutAt=0,wrongWindowAt=0,corruptAt=0;
 unsigned mode=0;bool owned=true,runtimePhase=true;uint64_t time=1000;std::vector<unsigned> writesAt;
 RuntimeIO(const ComputeSim &m,const FenceSim &f,const TX::Result &e,const H::Result &h)
  :root(m.root),tables(m.tables),image(m.image),backing(m.backing),queue(f.memory),storage(m.storage),memory(m.result),execution(e),host(h){
  ++runtimeScenarios;state.generation=7;state.client=11;state.prepared=state.opened=true;
  for(unsigned j=0;j<4;++j){state.requests[j].generation=7;state.requests[j].requestId=j+1;}
  // Keep expected root/table buffers separate from simulated device storage.
  storage.image=image.data();storage.requests=state.requests;
  A::BootstrapEvidence proof;proof.generation=7;proof.client=11;
  proof.ownerHeld=proof.firmwareReady=proof.hostQueueVerified=proof.storageReady=proof.windowRestored=true;
  CHECK(state.session.open(proof));
 }
 bool windowOwned(){++ownerChecks;return owned&&runtimePhase;}
 unsigned long long nowNs(){++clocks;if(clocks==clockFault)return 0;time+=1000;if(clocks==timeoutAt)time+=W::BudgetNs;return time;}
 bool operation(){++ops;if(ops==loseAt)owned=false;return ops!=failAt;}
 bool readWindow(unsigned &v){++reads;if(!operation())return false;v=window;if(ops==wrongWindowAt)v^=1;return true;}
 bool writeWindow(unsigned v){++writes;CHECK(v==0||v==0x42);writesAt.push_back(ChannelMemory::Window);const bool ok=operation();window=v;return ok;}
 bool ready(){return owned&&runtimePhase&&window==0&&X::accepted(state,job);}
 unsigned char *at(unsigned a,unsigned n){
  CHECK(X::readable(a,n,storage.liveBytes));
  if(a>=L::OldBase&&a-L::OldBase+n<=root.size())return root.data()+a-unsigned(L::OldBase);
  if(a>=L::NewBase&&a-L::NewBase+n<=tables.size())return tables.data()+a-unsigned(L::NewBase);
  if(a>=CM::Base&&a-CM::Base+n<=backing.size())return backing.data()+a-CM::Base;
  if(a>=H::Ring&&a-H::Ring+n<=queue.size())return queue.data()+a-H::Ring;
  CHECK(false);return nullptr;
 }
 void set(unsigned a,unsigned value){R::put32(at(a,4),value);}
 // Explicit CPU model of GPU writes. No host method submits these values.
 void device(){
  ++polls;const auto &request=state.requests[job];
  if(mode!=1&&polls>=3){
   set(H::Get,job+2);
   for(unsigned i=0;i<request.count;++i)set(I::outputPhysical(job)+i*4,request.a[i]+request.b[i]);
   set(QmdProfile::FencePhysical+job*256,A::completion(job));
  }
  if(mode==2)set(QmdProfile::FencePhysical+job*256,RtxBatch031::completion(job));
  if(mode==3&&polls>=3)backing[RtxBatch031::outOffset(job)+256]^=1;
  if(mode==4&&polls>=3)backing[32]^=1;
  if(mode==5&&polls>=3)tables[600]^=1;
  if(mode==6&&polls>=3)set(H::Put,job+1);
  if(mode==7&&polls>=3)set(H::Fence,0);
  if(mode==8&&polls>=3)set(I::outputPhysical(job),0xdead1234);
 }
 bool readMemory(unsigned a,unsigned char *out,unsigned n){
  if(!ready()||!state.slots[job].stageClaimed||!X::readable(a,n,storage.liveBytes))return false;
  ++reads;if(state.queue.jobs[job].notified&&a==H::Get)device();
  if(!operation())return false;std::memcpy(out,at(a,n),n);if(ops==corruptAt)out[0]^=1;return true;
 }
 bool writeMemory(unsigned a,const unsigned char *p,unsigned n){
  if(!ready())return false;
  const bool allowed=state.slots[job].submitClaimed?AppGate::write(state.queue,job,a,p,n):X::stageWrite(state,job,a,p,n);
  if(!allowed)return false;++writes;writesAt.push_back(a);const bool ok=operation();std::memcpy(at(a,n),p,n);return ok;
 }
 bool claim(unsigned j,uint64_t gen){return j==job&&ready()&&X::claimStage(state,j,gen);}
 bool claim(unsigned j){return j==job&&ready()&&X::claimSubmit(state,j);}
 bool physicalMode(){return ready()&&state.slots[job].submitClaimed;}
 void delayUs(unsigned us){CHECK(us==100);time+=100000000;}
 bool notify(unsigned token){
  if(!ready()||token!=execution.candidate||!AppGate::notify(state.queue,job))return false;++bells;return operation();
 }
 void counters(){ops=clocks=ownerChecks=reads=writes=bells=polls=0;writesAt.clear();}
 bool acquire(unsigned j,uint64_t caller){job=j;return W::acquire(*this,state.session,caller,0x42,state.slots[j].window);}
 bool stage(unsigned j){CHECK(job==j);return S::execute(*this,state.session.active(),image.data(),image.size(),state.slots[j].stage);}
 bool submit(unsigned j){return BS::execute(*this,j,storage,memory,execution,host,state.slots[j].submit);}
 bool collect(unsigned j){
  if(!ApplicationCapture::capture(*this,storage.liveBytes,captureRoot.data(),captureChildren.data(),captureDevice.data(),captured))return false;
  std::memcpy(capture.data(),captureDevice.data()+12288,CM::Bytes);
  return ready()&&ApplicationCapture::verifyFull(captured,captureRoot.data(),captureChildren.data(),captureDevice.data(),storage,host,j+1)&&ready();
 }
 struct DispatchIO {
  RuntimeIO &r;
  bool acquire(unsigned j,uint64_t caller){return r.acquire(j,caller);}
  bool stage(unsigned j){return r.stage(j);}
  bool submit(unsigned j){return r.submit(j);}
  bool capture(unsigned j){return r.collect(j);}
  bool restore(unsigned j){return W::restore(r,r.state.slots[j].window);}
  bool proof(){return r.owned&&r.runtimePhase;}
  void retain(){}
 };
 A::Error invoke(uint64_t caller,const unsigned char *wire,size_t bytes){
  DispatchIO io{*this};return RtxRuntimeDispatch032::execute(io,state,caller,wire,bytes);
 }
 bool run(unsigned j,unsigned count){
  A::Request request;request.generation=7;request.requestId=j+1;request.count=count;
  for(unsigned i=0;i<count;++i){request.a[i]=(0xfffffff0U+(i*0x1234567U))^j;request.b[i]=i*0x1020304U+j+16;}
  unsigned char wire[A::RequestBytes];CHECK(A::encode(request,wire,sizeof(wire)));counters();
  const bool ok=invoke(11,wire,sizeof(wire))==A::Error::Ok;
  if(ok){CHECK(window==0x42&&writes==7&&bells==1&&state.session.completed()==j+1&&state.queue.completed==j+1);
   CHECK(state.slots[j].window.restored&&state.slots[j].stage.reads==17&&state.slots[j].submit.expectedCount==count);
   CHECK(I::capture(backing.data(),backing.size(),image.data(),image.size(),j+1));
  }else CHECK(state.session.phase()==A::Phase::Retained);
  return ok;
 }
};
