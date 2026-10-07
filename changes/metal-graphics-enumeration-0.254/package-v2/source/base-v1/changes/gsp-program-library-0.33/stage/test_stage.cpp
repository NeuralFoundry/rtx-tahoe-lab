#include "ProgramAccess.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
namespace P=RtxProgram033;namespace R=RtxProgramRequest033;namespace I=RtxProgramImage033;
namespace S=RtxProgramStage033;namespace X=RtxProgramAccess033;namespace W=RtxProgramWindow033;namespace A=RtxProgramSession033;
using Bytes=std::vector<uint8_t>;
static unsigned checks=0,scenarios=0,rejected=0,gateRejections=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"scenario%u line%u: %s\n",scenarios,unsigned(__LINE__),#x);std::exit(1);}}while(0)
static Bytes load(const std::string &name){std::ifstream f(name,std::ios::binary);CHECK(bool(f));return Bytes(std::istreambuf_iterator<char>(f),{});}
static void save(const std::string &name,const Bytes &b){std::ofstream f(name,std::ios::binary);CHECK(bool(f));f.write(reinterpret_cast<const char*>(b.data()),b.size());f.close();CHECK(bool(f));}
struct Sim {
 X::State state;P::Library lib;Bytes library,code,canonical=Bytes(I::ImageBytes),actual,scratch=Bytes(4096);
 unsigned j=0,window=0x80173d90,ops=0,reads=0,writes=0,clocks=0,windowOps=0;
 unsigned failAt=0,loseAt=0,corruptAt=0,clockFault=0,timeoutAt=0,tamperClaim=0,lateCorrupt=0,failWindowAt=0;
 bool owned=true,adversarialGate=false;uint64_t time=100;std::vector<unsigned> trace;
 Sim(const Bytes &wire,const Bytes &c):library(wire),code(c){
  ++scenarios;CHECK(P::decode(library.data(),library.size(),code.data(),code.size(),lib));
  CHECK(I::initial(library.data(),library.size(),code.data(),code.size(),canonical.data(),canonical.size()));actual=canonical;
  state.storage={library.data(),code.data(),canonical.data(),scratch.data(),library.size(),code.size(),canonical.size(),scratch.size()};
  state.generation=0x30603301;state.client=11;state.prepared=state.opened=true;
  A::Bootstrap b;b.generation=state.generation;b.client=state.client;b.owner=b.firmware=b.host=b.library=b.storage=b.windowRestored=true;
  CHECK(state.session.open(b));CHECK(X::bindingValid(state));
 }
 uint64_t nowNs(){++clocks;if(clocks==clockFault)return 0;time+=100;if(clocks==timeoutAt)time+=S::BudgetNs;return time;}
 struct WindowIO {
  Sim &s;
  bool windowOwned(){return s.owned;}
  uint64_t nowNs(){return s.nowNs();}
  bool readWindow(unsigned &v){++s.windowOps;if(s.windowOps==s.failWindowAt)return false;v=s.window;return true;}
  bool writeWindow(unsigned v){++s.windowOps;s.window=v;return s.windowOps!=s.failWindowAt;}
 };
 bool prepare(const Bytes &request){
  CHECK(state.session.accept(11,request.data(),request.size(),lib)==A::Error::Ok);
  j=state.session.completed();state.history[j]=state.session.active();WindowIO io{*this};
  const bool ok=W::acquire(io,state.session,11,0x80173d90,state.slots[j].window);
  ops=reads=writes=clocks=0;time=100;return ok;
 }
 bool restore(){WindowIO io{*this};return W::restore(io,state.slots[j].window);}
 bool ready(){return owned&&window==0&&X::accepted(state,j);}
 bool claim(unsigned slot,uint64_t generation){
  CHECK(slot==j);auto &r=state.slots[j].stage;
  if(tamperClaim==1)r.plan.data[0]^=1;
  if(tamperClaim==2)r.plan.constant[0]^=1;
  if(tamperClaim==3)r.plan.qmd[0]^=1;
  if(tamperClaim==4)r.plan.command[0]^=1;
  if(tamperClaim==5)r.plan.entry[0]^=1;
  if(tamperClaim==6)r.plan.launch.programVA^=256;
  if(tamperClaim==7)r.request[0]^=1;
  if(tamperClaim==8)++generation;
  return ready()&&X::claimStage(state,j,generation);
 }
 bool operation(){++ops;if(ops==loseAt)owned=false;return ops!=failAt;}
 bool readMemory(unsigned address,uint8_t *out,unsigned bytes){
  CHECK(state.slots[j].stageClaimed&&X::readable(j,address,bytes));++reads;
  if(!operation())return false;std::memcpy(out,actual.data()+address-S::Base,bytes);
  if(reads==corruptAt)out[0]^=1;return true;
 }
 bool writeMemory(unsigned address,const uint8_t *data,unsigned bytes){
  const unsigned phase=state.slots[j].stageWrites;
  if(adversarialGate){
   Bytes bad(data,data+bytes);bad[0]^=1;
   CHECK(!X::stageWrite(state,j,address+4,data,bytes));
   CHECK(!X::stageWrite(state,j,address,data,bytes-4));
   CHECK(!X::stageWrite(state,j,address,bad.data(),bytes));
   CHECK(!X::stageWrite(state,j,S::Base,data,bytes));
   CHECK(!X::stageWrite(state,j,S::Base+I::fenceOffset(j),data,bytes));
   CHECK(state.slots[j].stageWrites==phase);gateRejections+=5;
  }
  CHECK(X::stageWrite(state,j,address,data,bytes));CHECK(state.slots[j].stageWrites==phase+1);
  CHECK(!X::stageWrite(state,j,address,data,bytes));++gateRejections;
  ++writes;trace.insert(trace.end(),{j,address,bytes});const bool ok=operation();
  std::memcpy(actual.data()+address-S::Base,data,bytes); // Failure may follow an effective write.
  if(lateCorrupt==1)actual[0]^=1;
  if(lateCorrupt==2)actual[I::fenceOffset(j)]^=1;
  return ok;
 }
 bool run(){return S::execute(*this,state.session.active(),state.storage,state.slots[j].stage);}
 void simulateFinish(){
  CHECK(X::staged(state,j));CHECK(restore());CHECK(window==0x80173d90);
  CHECK(state.session.submitted(11));A::Completion e;e.generation=state.generation;e.id=j+1;e.get=e.put=j+2;e.marker=P::completion(j);
  e.owner=e.capture=e.windowRestored=true;CHECK(state.session.finish(11,e));
  // Only synthetic session evidence, no shader execution or queue notification.
  P::Q::put32(actual.data()+I::fenceOffset(j),e.marker);
 }
};
static void replay(Sim &s){
 const auto &r=s.state.slots[s.j].stage;Bytes before(sizeof(r));std::memcpy(before.data(),&r,sizeof(r));const auto ops=s.ops;
 CHECK(!s.run());CHECK(s.ops==ops&&std::memcmp(before.data(),&r,sizeof(r))==0);
 auto fresh=std::unique_ptr<S::Result>(new S::Result);CHECK(!S::execute(s,s.state.session.active(),s.state.storage,*fresh));CHECK(s.ops==ops);
}
int main(int argc,char **argv){
 CHECK(argc==3);const std::string component=argv[1],out=argv[2];
 const auto library=load(component+"/fixtures/library.bin"),code=load(component+"/fixtures/code.bin");
 Bytes requests[4];for(unsigned j=0;j<4;++j)requests[j]=load(component+"/image-fixtures/request-"+std::to_string(j)+".bin");
 auto make=[&](){auto p=std::unique_ptr<Sim>(new Sim(library,code));CHECK(p->prepare(requests[0]));return p;};
 auto good=std::unique_ptr<Sim>(new Sim(library,code));good->adversarialGate=true;save(out+"/initial.bin",good->canonical);
 unsigned operations=0,clocks=0,readCount=0;
 for(unsigned j=0;j<4;++j){
  CHECK(good->prepare(requests[j]));CHECK(good->run());CHECK(X::staged(good->state,j));
  const auto &r=good->state.slots[j].stage;
  if(j==0){operations=good->ops;clocks=good->clocks;readCount=good->reads;}
  CHECK(good->ops==operations&&good->clocks==clocks&&good->reads==73&&good->writes==3&&r.verifiedWrites==3);
  save(out+"/request-"+std::to_string(j)+".bin",Bytes(r.request,r.request+R::WireBytes));
  Bytes plan(I::PlanBytes);CHECK(I::encodePlan(r.plan,plan.data(),plan.size()));save(out+"/plan-"+std::to_string(j)+".bin",plan);
  save(out+"/canonical-"+std::to_string(j)+".bin",good->canonical);save(out+"/staged-"+std::to_string(j)+".bin",good->actual);
  replay(*good);good->simulateFinish();
 }
 CHECK(good->state.session.phase()==A::Phase::Exhausted);
 Bytes trace(good->trace.size()*4);for(size_t i=0;i<good->trace.size();++i)P::Q::put32(trace.data()+i*4,good->trace[i]);save(out+"/writes.bin",trace);
 for(unsigned kind=0;kind<2;++kind)for(unsigned at=1;at<=operations;++at){auto s=make();const Bytes before=s->canonical;
  if(kind==0)s->failAt=at;else s->loseAt=at;
  CHECK(!s->run());CHECK(s->canonical==before);replay(*s);++rejected;
  const bool cleanup=s->restore();CHECK(cleanup==(kind==0));s->state.session.ownershipLost();CHECK(s->state.session.phase()==A::Phase::Retained);
 }
 for(unsigned at=1;at<=readCount;++at){auto s=make();s->corruptAt=at;const Bytes before=s->canonical;
  CHECK(!s->run());CHECK(s->canonical==before);replay(*s);++rejected;
 }
 for(unsigned kind=0;kind<2;++kind)for(unsigned at=2;at<=clocks;++at){auto s=make();const Bytes before=s->canonical;
  if(kind==0)s->clockFault=at;else s->timeoutAt=at;
  CHECK(!s->run());CHECK(s->canonical==before);replay(*s);++rejected;
 }
 for(unsigned kind=1;kind<=8;++kind){auto s=make();s->tamperClaim=kind;CHECK(!s->run());CHECK(!s->writes&&!s->reads);++rejected;}
 for(unsigned kind=1;kind<=2;++kind){auto s=make();s->lateCorrupt=kind;const Bytes before=s->canonical;CHECK(!s->run());CHECK(s->canonical==before);++rejected;}
 for(unsigned kind=0;kind<12;++kind){auto s=make();auto &m=s->state.storage;
  if(kind==0)m.library=nullptr;
  if(kind==1)m.code=nullptr;
  if(kind==2)m.canonical=nullptr;
  if(kind==3)m.scratch=nullptr;
  if(kind==4)m.codeBytes=4095;
  if(kind==5)m.canonical=m.scratch;
  if(kind==6)m.scratch=s->state.proofScratch;
  if(kind==7)m.canonical=reinterpret_cast<uint8_t*>(&s->state.slots[0].stage);
  if(kind==8)s->code[640]=1;
  if(kind==9)s->canonical[0]^=1;
  if(kind==10)s->canonical[I::fenceOffset(0)]=1;
  if(kind==11)s->window=1;
  CHECK(!s->run());CHECK(!s->reads&&!s->writes);++rejected;
 }
 for(unsigned at=1;at<=5;++at){auto s=std::unique_ptr<Sim>(new Sim(library,code));s->failWindowAt=at;
  const bool acquired=s->prepare(requests[0]);CHECK(acquired==(at>3));
  if(acquired)CHECK(s->run());const bool restored=s->restore();CHECK(restored==(at<=3));
  const auto &w=s->state.slots[0].window;
  if(w.mutationAttempted)CHECK(w.restoreAttempted);
  CHECK(!s->reads||acquired);s->state.session.ownershipLost();CHECK(!s->ready());++rejected;
 }
 printf("{\"passed\":true,\"scenarios\":%u,\"checks\":%u,\"rejected\":%u,\"gate_rejections\":%u,\"requests\":4,\"operations_per_request\":%u,\"clocks_per_request\":%u,\"reads_per_request\":%u,\"writes_per_request\":3,\"result_bytes\":%zu,\"state_bytes\":%zu,\"session_completion_simulated\":true,\"cpu_simulation_only\":true,\"gpu_commands_submitted\":false}\n",
  scenarios,checks,rejected,gateRejections,operations,clocks,readCount,sizeof(S::Result),sizeof(X::State));
}
