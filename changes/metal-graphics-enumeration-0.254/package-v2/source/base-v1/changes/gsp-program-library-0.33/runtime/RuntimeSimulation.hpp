#include "../memory/ProgramMemorySimulation.hpp"
#include "../queue/ProgramQueueGate.hpp"
#include "ProgramRuntime.hpp"
#include <memory>
namespace PG=RtxProgram033;namespace RI=RtxProgramImage033;namespace RQ=RtxProgramRequest033;
namespace AX=RtxProgramAccess033;namespace ST=RtxProgramStage033;namespace WW=RtxProgramWindow033;
namespace AS=RtxProgramSession033;namespace PS=ProgramSubmit;namespace GG=ProgramQueueGate;namespace PC=ProgramCapture;
namespace RT=RtxProgramRuntime033;
static unsigned runtimeScenarios=0,runtimeGateDenials=0;
struct ProgramRuntimeSim {
 ProgramMemorySim mem;RT::State state;AX::State &access=state.access;GG::State &queue=state.queue;PS::Storage &storage=state.submission;
 const TX::Result &execution;const H::Result &host;PG::Library &library=state.library;
 Bytes device,work=Bytes(4096),capture=Bytes(RI::ImageBytes),capRoot[4],capChildren[4],capDevice[4];
 unsigned j=0,window=0,ops=0,clocks=0,reads=0,writes=0,bells=0,delays=0;
 unsigned failAt=0,loseAt=0,clockFault=0,timeoutAt=0,mode=0,corruptAt=0,ownerChecks=0,loseProofAt=0,retentions=0,captureCalls=0,restoreCalls=0;
 bool owned=true,pending=false,adversarial=false,bootPhase=true,runtimePhase=false,originalRestored=false;
 uint64_t time=100;std::vector<unsigned> trace,tokens,writeOps,events,timerStarts;
 ProgramRuntimeSim(RuntimeSim &initial,const TX::Result &e,const H::Result &h,const Bytes &hostMemory,const Bytes &wire,const Bytes &code)
  :mem(initial,wire,code),execution(e),host(h),device(hostMemory){
  ++runtimeScenarios;CHECK(mem.run(initial.golden,execution,host));CHECK(PG::decode(wire.data(),wire.size(),code.data(),code.size(),library));
  access.storage={mem.library.data(),mem.code.data(),mem.image.data(),work.data(),512,4096,RI::ImageBytes,4096};
  storage.memory=&mem.storage;storage.history=access.history;storage.capture=capture.data();storage.captureBytes=RI::ImageBytes;
  storage.scratch=access.proofScratch;storage.scratchBytes=4096;storage.proofPlan=&access.proofPlan;
  for(unsigned n=0;n<4;++n){
   storage.plans[n]=&access.slots[n].stage.plan;
   capRoot[n].resize(12288);capChildren[n].resize(L::MaxChildBytes);capDevice[n].resize(PC::DeviceBytes);
   state.captures[n].root=capRoot[n].data();state.captures[n].children=capChildren[n].data();state.captures[n].device=capDevice[n].data();
  }
  CHECK(RT::bound(state,mem.storage));

 }
 uint64_t nowNs(){++clocks;if(clocks==clockFault)return 0;time+=1000;if(clocks==timeoutAt)time+=PS::BudgetNs;return time;}
 uint8_t *at(unsigned a,unsigned n){
  CHECK(n&&n<=4096&&!(a&3)&&!(n&3));
  if(a>=L::OldBase&&a-L::OldBase+n<=mem.root.size())return mem.root.data()+a-unsigned(L::OldBase);
  if(a>=L::NewBase&&a-L::NewBase+n<=mem.storage.liveBytes)return mem.tables.data()+a-unsigned(L::NewBase);
  if(a>=CM::Base&&a-CM::Base+n<=mem.backing.size())return mem.backing.data()+a-CM::Base;
  if(a>=H::Ring&&a-H::Ring+n<=device.size())return device.data()+a-H::Ring;
  CHECK(false);return nullptr;
 }
 bool operation(){++ops;if(ops==loseAt)owned=false;return ops!=failAt;}
 void event(unsigned a,unsigned n,const uint8_t *p){events.insert(events.end(),{j,a,n,PG::get32(p)});}
 struct PrepareIO {
  ProgramRuntimeSim &s;uint64_t nowNs(){return s.nowNs();}
  bool ready(uint64_t generation){return s.owned&&s.bootPhase&&!s.window&&generation==0x30603301;}
  bool readMemory(unsigned a,uint8_t *out,unsigned n){++s.reads;if(!s.operation())return false;std::memcpy(out,s.at(a,n),n);if(s.ops==s.corruptAt)out[0]^=1;return true;}
 };
 struct OpenIO {
  ProgramRuntimeSim &s;
  bool openProof(uint64_t g){return s.owned&&s.bootPhase&&s.originalRestored&&g==s.access.generation;}
  bool readWindow(unsigned &v){++s.reads;if(!s.operation())return false;v=s.window;return true;}
  bool beginRuntime(uint64_t g){if(!openProof(g))return false;s.bootPhase=false;s.runtimePhase=true;return s.mode!=17;}
  bool runtimeProof(uint64_t g){return s.owned&&s.runtimePhase&&s.window==0x80173d90&&g==s.access.generation&&s.mode!=18;}
  void retain(){s.retain();}
 };
 struct WindowIO {
  ProgramRuntimeSim &s;bool windowOwned(){return s.owned&&s.runtimePhase;}uint64_t nowNs(){return s.nowNs();}
  bool readWindow(unsigned &v){++s.reads;if(!s.operation())return false;v=s.window;return true;}
  bool writeWindow(unsigned v){++s.writes;uint8_t raw[4];PG::Q::put32(raw,v);s.event(ChannelMemory::Window,4,raw);const bool ok=s.operation();s.window=v;return ok;}
 };
 struct StageIO {
  ProgramRuntimeSim &s;bool ready(){return s.owned&&s.runtimePhase&&!s.window&&AX::accepted(s.access,s.j);}uint64_t nowNs(){return s.nowNs();}
  bool claim(unsigned j,uint64_t g){return j==s.j&&ready()&&AX::claimStage(s.access,j,g);}
  bool readMemory(unsigned a,uint8_t *out,unsigned n){
   if(!ready()||!AX::readable(s.j,a,n))return false;++s.reads;if(!s.operation())return false;std::memcpy(out,s.at(a,n),n);if(s.ops==s.corruptAt)out[0]^=1;return true;
  }
  bool writeMemory(unsigned a,const uint8_t *p,unsigned n){
   if(!ready()||!AX::stageWrite(s.access,s.j,a,p,n))return false;++s.writes;s.event(a,n,p);const bool ok=s.operation();std::memcpy(s.at(a,n),p,n);return ok;
  }
 };
 void counters(){ops=clocks=reads=writes=bells=delays=ownerChecks=captureCalls=restoreCalls=0;time=100;writeOps.clear();timerStarts.clear();}
 bool prepare(){PrepareIO io{*this};return RT::prepare(io,state,mem.storage,mem.result,execution,host,0x30603301,11);}
 bool open(){window=0x80173d90;originalRestored=true;OpenIO io{*this};return RT::open(io,state,0x80173d90);}
 bool ready(){return owned&&runtimePhase&&!window&&AX::staged(access,j)&&GG::ready(queue,j);}
 bool claim(unsigned n){return n==j&&ready()&&GG::claim(queue,j,*storage.plans[j]);}
 bool physicalMode(){return ready()&&queue.jobs[j].claimed;}
 bool readMemory(unsigned a,uint8_t *out,unsigned n){
  CHECK(queue.jobs[j].claimed);++reads;if(!operation())return false;std::memcpy(out,at(a,n),n);if(ops==corruptAt)out[0]^=1;return true;
 }
 bool writeMemory(unsigned a,const uint8_t *p,unsigned n){
  const auto phase=queue.jobs[j].phase;
  if(adversarial){Bytes bad(p,p+n);bad[0]^=1;
   CHECK(!GG::write(queue,j,a+4,p,n));CHECK(!GG::write(queue,j,a,p,n+4));CHECK(!GG::write(queue,j,a,bad.data(),n));
   CHECK(!GG::write(queue,j,CM::Base,p,n));CHECK(!GG::notify(queue,j));CHECK(queue.jobs[j].phase==phase);runtimeGateDenials+=5;
  }
  CHECK(GG::write(queue,j,a,p,n));CHECK(queue.jobs[j].phase==phase+1);CHECK(!GG::write(queue,j,a,p,n));++runtimeGateDenials;
  ++writes;event(a,n,p);trace.insert(trace.end(),{j,a,n});const bool ok=operation();writeOps.push_back(ops);
  std::memcpy(at(a,n),p,n);return ok;
 }
 bool notify(unsigned token){
  CHECK(token==execution.candidate&&token==4);CHECK(GG::notify(queue,j));CHECK(!GG::notify(queue,j));++runtimeGateDenials;
  ++bells;tokens.push_back(token);pending=true;const bool ok=operation();writeOps.push_back(ops);return ok;
 }
 void emulate(){
  CHECK(pending);pending=false;const auto &request=access.history[j];
  for(unsigned i=0;i<request.groups*library.programs[request.program].localX;++i){
   const uint32_t a=PG::get32(request.data+i*4),b=PG::get32(request.data+256+i*4);
   const uint32_t result=request.program==0?a+b:request.program==1?a*b:a^b;
   PG::Q::put32(mem.backing.data()+RI::dataOffset(j)+512+i*4,result);
  }
  PG::Q::put32(at(H::Get,4),j+2);PG::Q::put32(mem.backing.data()+RI::fenceOffset(j),PG::completion(j));
  if(mode==1)mem.backing[RI::dataOffset(j)+512]^=1; // Permitted location, wrong arithmetic.
  if(mode==2)mem.backing[RI::dataOffset(j)]^=1;
  if(mode==3)mem.backing.back()^=1;
  if(mode==4)mem.backing[0]^=1;
  if(mode==5)PG::Q::put32(mem.backing.data()+RI::fenceOffset(j),42);
  if(mode==6)PG::Q::put32(at(H::Get,4),99);
  if(mode==7)PG::Q::put32(at(H::Fence,4),0);
  if(mode==8)mem.backing[12288+4*256]^=1;
  if(mode==9)mem.tables[4096+4*8]^=1;
  if(mode==10)mem.root[8]^=1;
  if(mode==11)at(H::Command,4)[0]^=1;
  if(mode==12)mem.backing[RI::qmdOffset(j)]^=1; // Submitted QMD writeback is allowed.
  if(mode==13)mem.backing[RI::qmdOffset(j+1)]^=1;
  if(mode==14)PG::Q::put32(at(H::Put,4),99);
 }
 void delayUs(unsigned us){CHECK(us==100);++delays;time+=100000;if(mode==15){time+=PS::BudgetNs;return;}emulate();}
 bool arithmetic(unsigned completed){
  for(unsigned k=0;k<completed;++k){const auto &r=access.history[k];
   for(unsigned i=0;i<64;++i){const uint32_t a=PG::get32(r.data+i*4),b=PG::get32(r.data+256+i*4),expected=r.program==0?a+b:r.program==1?a*b:a^b;
    if(PG::get32(capDevice[completed-1].data()+12288+RI::dataOffset(k)+512+i*4)!=expected)return false;
   }
  }
  return true;
 }
 struct CaptureIO {
  ProgramRuntimeSim &s;bool ready(){return s.owned&&s.runtimePhase&&!s.window&&AX::accepted(s.access,s.j);}uint64_t nowNs(){return s.nowNs();}
  bool readMemory(unsigned a,uint8_t *out,unsigned n){if(!ready())return false;++s.reads;if(!s.operation())return false;std::memcpy(out,s.at(a,n),n);if(s.ops==s.corruptAt)out[0]^=1;return true;}
 };
 bool collect(unsigned job){
  CHECK(job==j);++captureCalls;timerStarts.push_back(clocks+1);CaptureIO io{*this};auto &c=state.captures[j];
  return PC::capture(io,mem.storage.liveBytes,c.root,c.children,c.device,c.result)&&PC::verifyFull(c.result,c.root,c.children,c.device,storage,host,j+1,j+1);
 }
 bool proof(){++ownerChecks;if(ownerChecks==loseProofAt)owned=false;return owned&&runtimePhase&&window==0x80173d90;}
 void retain(){++retentions;runtimePhase=bootPhase=false;}
 struct DispatchIO {
  ProgramRuntimeSim &s;
  bool acquire(unsigned job,uint64_t caller){s.j=job;s.timerStarts.push_back(s.clocks+1);WindowIO io{s};return WW::acquire(io,s.access.session,caller,0x80173d90,s.access.slots[job].window);}
  bool stage(unsigned job){CHECK(job==s.j);s.timerStarts.push_back(s.clocks+1);StageIO io{s};return ST::execute(io,s.access.session.active(),s.access.storage,s.access.slots[job].stage);}
  bool submit(unsigned job){CHECK(job==s.j);s.timerStarts.push_back(s.clocks+1);return PS::execute(s,job,s.storage,s.mem.result,s.execution,s.host,s.queue.jobs[job].result);}
  bool capture(unsigned job){return s.collect(job);}
  bool restore(unsigned job){++s.restoreCalls;s.timerStarts.push_back(s.clocks+1);WindowIO io{s};return WW::restore(io,s.access.slots[job].window);}
  bool proof(){return s.proof();}void retain(){s.retain();}
 };
 RT::Error invoke(uint64_t caller,const Bytes &wire){DispatchIO io{*this};return RT::dispatch(io,state,caller,wire.data(),wire.size());}
 bool close(uint64_t caller){DispatchIO io{*this};return RT::close(io,state,caller);}
};
