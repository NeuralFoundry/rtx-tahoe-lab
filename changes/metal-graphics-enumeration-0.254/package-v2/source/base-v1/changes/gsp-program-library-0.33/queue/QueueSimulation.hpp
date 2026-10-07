#include "../memory/ProgramMemorySimulation.hpp"
#include "ProgramQueueGate.hpp"
#include "ProgramCapture.hpp"
#include <memory>
namespace PG=RtxProgram033;namespace RI=RtxProgramImage033;namespace RQ=RtxProgramRequest033;
namespace AX=RtxProgramAccess033;namespace ST=RtxProgramStage033;namespace WW=RtxProgramWindow033;
namespace AS=RtxProgramSession033;namespace PS=ProgramSubmit;namespace GG=ProgramQueueGate;namespace PC=ProgramCapture;
static unsigned queueScenarios=0,queueGateDenials=0;
struct QueueSim {
 ProgramMemorySim mem;AX::State access;GG::State queue;PS::Storage storage;
 const TX::Result &execution;const H::Result &host;PG::Library library;
 Bytes device,work=Bytes(4096),capture=Bytes(RI::ImageBytes),capRoot=Bytes(12288),capChildren=Bytes(L::MaxChildBytes),capDevice=Bytes(PC::DeviceBytes);
 PC::Result capResult;
 unsigned j=0,window=0x80173d90,ops=0,clocks=0,reads=0,writes=0,bells=0,delays=0;
 unsigned failAt=0,loseAt=0,clockFault=0,timeoutAt=0,mode=0;bool owned=true,pending=false,adversarial=false;
 uint64_t time=100;std::vector<unsigned> trace,tokens,writeOps;
 QueueSim(RuntimeSim &initial,const TX::Result &e,const H::Result &h,const Bytes &hostMemory,const Bytes &wire,const Bytes &code)
  :mem(initial,wire,code),execution(e),host(h),device(hostMemory){
  ++queueScenarios;CHECK(mem.run(initial.golden,execution,host));CHECK(PG::decode(wire.data(),wire.size(),code.data(),code.size(),library));
  access.storage={mem.library.data(),mem.code.data(),mem.image.data(),work.data(),512,4096,RI::ImageBytes,4096};
  access.generation=0x30603301;access.client=11;access.prepared=access.opened=true;
  AS::Bootstrap b;b.generation=access.generation;b.client=access.client;b.owner=b.firmware=b.host=b.library=b.storage=b.windowRestored=true;
  CHECK(access.session.open(b));
  storage.memory=&mem.storage;storage.history=access.history;storage.capture=capture.data();storage.captureBytes=RI::ImageBytes;
  storage.scratch=access.proofScratch;storage.scratchBytes=4096;storage.proofPlan=&access.proofPlan;
  for(unsigned n=0;n<4;++n)storage.plans[n]=&access.slots[n].stage.plan;
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
 struct WindowIO {
  QueueSim &s;bool windowOwned(){return s.owned;}uint64_t nowNs(){return s.nowNs();}
  bool readWindow(unsigned &v){v=s.window;return true;}bool writeWindow(unsigned v){s.window=v;return true;}
 };
 struct StageIO {
  QueueSim &s;bool ready(){return s.owned&&!s.window&&AX::accepted(s.access,s.j);}uint64_t nowNs(){return s.nowNs();}
  bool claim(unsigned j,uint64_t g){return j==s.j&&ready()&&AX::claimStage(s.access,j,g);}
  bool readMemory(unsigned a,uint8_t *out,unsigned n){CHECK(ready()&&AX::readable(s.j,a,n));std::memcpy(out,s.at(a,n),n);return true;}
  bool writeMemory(unsigned a,const uint8_t *p,unsigned n){CHECK(ready()&&AX::stageWrite(s.access,s.j,a,p,n));std::memcpy(s.at(a,n),p,n);return true;}
 };
 void prepare(const Bytes &request){
  CHECK(access.session.accept(11,request.data(),request.size(),library)==AS::Error::Ok);j=access.session.completed();
  access.history[j]=access.session.active();WindowIO w{*this};CHECK(WW::acquire(w,access.session,11,0x80173d90,access.slots[j].window));
  StageIO s{*this};CHECK(ST::execute(s,access.session.active(),access.storage,access.slots[j].stage));CHECK(AX::staged(access,j));
  ops=clocks=reads=writes=bells=delays=0;time=100;writeOps.clear();
 }
 bool ready(){return owned&&!window&&AX::staged(access,j)&&GG::ready(queue,j);}
 bool claim(unsigned n){return n==j&&ready()&&GG::claim(queue,j,*storage.plans[j]);}
 bool physicalMode(){return ready()&&queue.jobs[j].claimed;}
 bool operation(){++ops;if(ops==loseAt)owned=false;return ops!=failAt;}
 bool readMemory(unsigned a,uint8_t *out,unsigned n){
  CHECK(queue.jobs[j].claimed);++reads;if(!operation())return false;std::memcpy(out,at(a,n),n);return true;
 }
 bool writeMemory(unsigned a,const uint8_t *p,unsigned n){
  const auto phase=queue.jobs[j].phase;
  if(adversarial){Bytes bad(p,p+n);bad[0]^=1;
   CHECK(!GG::write(queue,j,a+4,p,n));CHECK(!GG::write(queue,j,a,p,n+4));CHECK(!GG::write(queue,j,a,bad.data(),n));
   CHECK(!GG::write(queue,j,CM::Base,p,n));CHECK(!GG::notify(queue,j));CHECK(queue.jobs[j].phase==phase);queueGateDenials+=5;
  }
  CHECK(GG::write(queue,j,a,p,n));CHECK(queue.jobs[j].phase==phase+1);CHECK(!GG::write(queue,j,a,p,n));++queueGateDenials;
  ++writes;trace.insert(trace.end(),{j,a,n});const bool ok=operation();writeOps.push_back(ops);
  std::memcpy(at(a,n),p,n);return ok;
 }
 bool notify(unsigned token){
  CHECK(token==execution.candidate&&token==4);CHECK(GG::notify(queue,j));CHECK(!GG::notify(queue,j));++queueGateDenials;
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
 bool run(){return PS::execute(*this,j,storage,mem.result,execution,host,queue.jobs[j].result);}
 bool arithmetic(unsigned completed){
  for(unsigned k=0;k<completed;++k){const auto &r=access.history[k];
   for(unsigned i=0;i<64;++i){const uint32_t a=PG::get32(r.data+i*4),b=PG::get32(r.data+256+i*4),expected=r.program==0?a+b:r.program==1?a*b:a^b;
    if(PG::get32(mem.backing.data()+RI::dataOffset(k)+512+i*4)!=expected)return false;
   }
  }
  return true;
 }
 struct CaptureIO {
  QueueSim &s;bool ready(){return s.ready();}uint64_t nowNs(){return s.nowNs();}
  bool readMemory(unsigned a,uint8_t *out,unsigned n){if(!ready())return false;std::memcpy(out,s.at(a,n),n);return true;}
 };
 bool fullCapture(){CaptureIO io{*this};return PC::capture(io,mem.storage.liveBytes,capRoot.data(),capChildren.data(),capDevice.data(),capResult)&&
  PC::verifyFull(capResult,capRoot.data(),capChildren.data(),capDevice.data(),storage,host,j+1,j+1);}
 void finish(){
  CHECK(fullCapture());WindowIO w{*this};CHECK(WW::restore(w,access.slots[j].window));CHECK(window==0x80173d90);
  CHECK(GG::finish(queue,j));CHECK(!GG::finish(queue,j));CHECK(access.session.submitted(11));
  const auto &r=queue.jobs[j].result;AS::Completion e;e.generation=access.generation;e.id=j+1;e.get=r.get;e.put=r.put;e.marker=r.completion;
  e.owner=owned;e.capture=capResult.passed;e.windowRestored=access.slots[j].window.restored;CHECK(access.session.finish(11,e));
 }
};
