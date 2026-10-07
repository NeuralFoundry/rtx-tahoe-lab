#include "QueueSimulation.hpp"
static unsigned rejected=0;
static void replay(QueueSim &s){
 const auto &r=s.queue.jobs[s.j].result;Bytes before(sizeof(r));std::memcpy(before.data(),&r,sizeof(r));const auto ops=s.ops;
 CHECK(!s.run());CHECK(s.ops==ops&&std::memcmp(before.data(),&r,sizeof(r))==0);
 PS::Result fresh;CHECK(!PS::execute(s,s.j,s.storage,s.mem.result,s.execution,s.host,fresh));CHECK(s.ops==ops);
}
int main(int argc,char **argv){
 CHECK(argc==5);const auto gr=load(argv[1]),fifo=load(argv[2]);const std::string component=argv[3],out=argv[4];
 const auto library=load((component+"/fixtures/library.bin").c_str()),code=load((component+"/fixtures/code.bin").c_str());
 Bytes wire[4];for(unsigned j=0;j<4;++j)wire[j]=load((component+"/image-fixtures/request-"+std::to_string(j)+".bin").c_str());
 RuntimeSim initial(gr,fifo);Bytes requests,records;const auto execution=run(initial,&requests,&records);CHECK(execution.rpc.passed);
 L::Range goldenRanges[10],executionRanges[6];CHECK(C::mappingRanges(initial.golden,goldenRanges)&&P::mappings(initial.plan,initial.golden,executionRanges));
 walk(initial,goldenRanges,10,initial.contexts.childBytes);walk(initial,executionRanges,6,initial.contexts.childBytes);
 FenceSim hostIO;const auto host=fenceRun(hostIO,initial.golden,execution,requests,records);CHECK(host.passed);
 auto make=[&](){auto s=std::unique_ptr<QueueSim>(new QueueSim(initial,execution,host,hostIO.memory,library,code));s->prepare(wire[0]);return s;};
 auto good=std::unique_ptr<QueueSim>(new QueueSim(initial,execution,host,hostIO.memory,library,code));good->adversarial=true;
 unsigned operations=0,clocks=0;Bytes outcomes(4*12*8);
 for(unsigned j=0;j<4;++j){
  good->prepare(wire[j]);save(out+"/canonical-"+std::to_string(j)+".bin",good->mem.image);
  CHECK(good->run());CHECK(good->arithmetic(j+1));CHECK(good->writes==3&&good->bells==1);
  const auto &r=good->queue.jobs[j].result;CHECK(r.passed&&r.stable&&r.polls==2&&r.immutableVerified&&r.guardsVerified);
  if(j==0){operations=good->ops;clocks=good->clocks;}
  CHECK(good->ops==operations&&good->clocks==clocks);
  const uint64_t values[]={j,r.initialGet,r.initialPut,r.initialCompletion,r.get,r.put,r.hostFence,r.completion,r.writes,r.polls,r.reads,r.operations};
  for(unsigned k=0;k<12;++k)PG::Q::put64(outcomes.data()+j*96+k*8,values[k]);
  replay(*good);good->finish();
  save(out+"/root-"+std::to_string(j)+".bin",good->capRoot);
  save(out+"/children-"+std::to_string(j)+".bin",Bytes(good->capChildren.begin(),good->capChildren.begin()+good->mem.storage.liveBytes));
  save(out+"/device-"+std::to_string(j)+".bin",good->capDevice);good->capResult={};
 }
 CHECK(good->access.session.phase()==AS::Phase::Exhausted&&good->queue.completed==4);
 save(out+"/outcomes.bin",outcomes);Bytes trace(good->trace.size()*4),tokens(good->tokens.size()*4);
 for(size_t i=0;i<good->trace.size();++i)PG::Q::put32(trace.data()+i*4,good->trace[i]);
 for(size_t i=0;i<good->tokens.size();++i)PG::Q::put32(tokens.data()+i*4,good->tokens[i]);
 save(out+"/writes.bin",trace);save(out+"/tokens.bin",tokens);
 for(unsigned kind=0;kind<2;++kind)for(unsigned at=1;at<=operations;++at){auto s=make();const Bytes canonical=s->mem.image;
  if(kind==0)s->failAt=at;else s->loseAt=at;
  CHECK(!s->run());CHECK(s->mem.image==canonical);CHECK(!GG::finish(s->queue,0));replay(*s);++rejected;
 }
 for(unsigned kind=0;kind<2;++kind)for(unsigned at=2;at<=clocks;++at){auto s=make();
  if(kind==0)s->clockFault=at;else s->timeoutAt=at;
  CHECK(!s->run());CHECK(!GG::finish(s->queue,0));replay(*s);++rejected;
 }
 for(unsigned mode=1;mode<=15;++mode){auto s=make();s->mode=mode;const bool pass=s->run();CHECK(pass==(mode==1||mode==12));
  if(pass){CHECK(s->fullCapture());CHECK(s->arithmetic(1)==(mode==12));
   save(out+(mode==1?"/wrong-arithmetic-allowed-location.bin":"/allowed-qmd-writeback.bin"),s->capDevice);
  }else{CHECK(!GG::finish(s->queue,0));++rejected;}
 }
 for(unsigned kind=0;kind<9;++kind){auto s=make();
  if(kind==0)s->storage.capture=s->mem.image.data();
  if(kind==1)s->storage.scratch=s->access.slots[0].stage.plan.data;
  if(kind==2)s->storage.proofPlan=&s->access.slots[0].stage.plan;
  if(kind==3)s->access.slots[0].stage.plan.command[0]^=1;
  if(kind==4)s->access.history[0].program=2;
  if(kind==5)s->mem.result.memory.passed=false;
  if(kind==6)s->mem.image.back()^=1;
  if(kind==7)s->window=1;
  if(kind==8)s->queue.completed=1;
  CHECK(!s->run());CHECK(s->writes==0&&s->bells==0);++rejected;
 }
 printf("{\"passed\":true,\"scenarios\":%u,\"checks\":%u,\"rejected\":%u,\"gate_denials\":%u,\"requests\":4,\"programs\":3,\"io_per_request\":%u,\"clocks_per_request\":%u,\"result_bytes\":%zu,\"queue_bytes\":%zu,\"arithmetic_location_separation_verified\":true,\"cpu_simulation_only\":true,\"gpu_commands_submitted\":false}\n",
  queueScenarios,checks,rejected,queueGateDenials,operations,clocks,sizeof(PS::Result),sizeof(GG::State));
}
