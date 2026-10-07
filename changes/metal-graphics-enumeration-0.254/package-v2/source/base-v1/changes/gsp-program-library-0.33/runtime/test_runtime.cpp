#include "RuntimeSimulation.hpp"
static unsigned rejected=0,prepareRejected=0,argumentRejected=0;
static void retired(ProgramRuntimeSim &s,const Bytes &request){
 CHECK(s.state.closed&&!s.runtimePhase&&s.retentions==1&&s.access.session.phase()==AS::Phase::Retained);
 CHECK(s.restoreCalls==1);const unsigned ops=s.ops;
 CHECK(s.invoke(11,request)==RT::Error::State);CHECK(s.ops==ops);CHECK(!s.close(11));CHECK(s.retentions==1);
}
int main(int argc,char **argv){
 CHECK(argc==5);const auto gr=load(argv[1]),fifo=load(argv[2]);const std::string component=argv[3],out=argv[4];
 const auto library=load((component+"/fixtures/library.bin").c_str()),code=load((component+"/fixtures/code.bin").c_str());
 Bytes wire[4];for(unsigned j=0;j<4;++j)wire[j]=load((component+"/image-fixtures/request-"+std::to_string(j)+".bin").c_str());
 RuntimeSim initial(gr,fifo);Bytes requests,records;const auto execution=run(initial,&requests,&records);CHECK(execution.rpc.passed);
 L::Range goldenRanges[10],executionRanges[6];CHECK(C::mappingRanges(initial.golden,goldenRanges)&&P::mappings(initial.plan,initial.golden,executionRanges));
 walk(initial,goldenRanges,10,initial.contexts.childBytes);walk(initial,executionRanges,6,initial.contexts.childBytes);
 FenceSim hostIO;const auto host=fenceRun(hostIO,initial.golden,execution,requests,records);CHECK(host.passed);
 auto empty=[&](){return std::unique_ptr<ProgramRuntimeSim>(new ProgramRuntimeSim(initial,execution,host,hostIO.memory,library,code));};
 auto make=[&](){auto s=empty();CHECK(s->prepare());CHECK(s->open());s->counters();return s;};
 auto good=empty();CHECK(good->prepare());const unsigned prepOps=good->ops,prepClocks=good->clocks;
 CHECK(prepOps==96&&good->state.preparation.reads==96&&good->state.preparation.bytes==24576);
 save(out+"/bootstrap.bin",good->capture);CHECK(!good->prepare());CHECK(!good->close(12));CHECK(good->open());CHECK(!good->open());
 unsigned operations=0,clocks=0;std::vector<unsigned> starts;Bytes outcomes(4*16*8);
 for(unsigned j=0;j<4;++j){
  good->counters();CHECK(good->invoke(11,wire[j])==RT::Error::Ok);CHECK(good->arithmetic(j+1));
  CHECK(good->ops==827&&good->writes==8&&good->bells==1&&good->captureCalls==1&&good->restoreCalls==1&&good->ownerChecks==3);
  CHECK(good->window==0x80173d90&&good->access.session.completed()==j+1&&good->queue.completed==j+1&&!good->state.closed);
  if(j==0){operations=good->ops;clocks=good->clocks;starts=good->timerStarts;}
  CHECK(good->ops==operations&&good->clocks==clocks&&good->timerStarts==starts);
  const auto &w=good->access.slots[j].window;const auto &s=good->access.slots[j].stage;const auto &q=good->queue.jobs[j].result;const auto &c=good->state.captures[j];
  const uint64_t values[]={j,good->access.session.completed(),good->queue.completed,good->state.preparation.passed,good->state.opening.passed,
   w.before,w.after,s.writes,q.writes,q.polls,c.result.reads,good->state.captureVerified[j],q.completion,good->ownerChecks,good->ops,good->clocks};
  for(unsigned k=0;k<16;++k)PG::Q::put64(outcomes.data()+j*128+k*8,values[k]);
  save(out+"/canonical-"+std::to_string(j)+".bin",good->mem.image);
  save(out+"/request-"+std::to_string(j)+".bin",Bytes(s.request,s.request+RQ::WireBytes));
  save(out+"/root-"+std::to_string(j)+".bin",good->capRoot[j]);
  save(out+"/children-"+std::to_string(j)+".bin",Bytes(good->capChildren[j].begin(),good->capChildren[j].begin()+good->mem.storage.liveBytes));
  save(out+"/device-"+std::to_string(j)+".bin",good->capDevice[j]);
 }
 CHECK(good->access.session.phase()==AS::Phase::Exhausted);const unsigned lastOps=good->ops;
 CHECK(!good->close(12));CHECK(good->close(11));CHECK(good->ops==lastOps&&good->retentions==1);CHECK(!good->close(11));
 CHECK(good->invoke(11,wire[0])==RT::Error::State&&good->ops==lastOps);
 save(out+"/outcomes.bin",outcomes);Bytes events(good->events.size()*4),tokens(good->tokens.size()*4);
 for(size_t i=0;i<good->events.size();++i)PG::Q::put32(events.data()+i*4,good->events[i]);
 for(size_t i=0;i<good->tokens.size();++i)PG::Q::put32(tokens.data()+i*4,good->tokens[i]);
 save(out+"/events.bin",events);save(out+"/tokens.bin",tokens);
 for(unsigned kind=0;kind<2;++kind)for(unsigned at=1;at<=operations;++at){auto s=make();
  if(kind==0)s->failAt=at;else s->loseAt=at;
  CHECK(s->invoke(11,wire[0])==RT::Error::Evidence);retired(*s,wire[0]);++rejected;
 }
 // Each component has its own bounded clock origin; changing an origin alone
 // does not establish a regression or expiry within that component.
 for(unsigned kind=0;kind<2;++kind)for(unsigned at=2;at<=clocks;++at){
  if(std::find(starts.begin(),starts.end(),at)!=starts.end())continue;
  auto s=make();if(kind==0)s->clockFault=at;else s->timeoutAt=at;
  CHECK(s->invoke(11,wire[0])==RT::Error::Evidence);retired(*s,wire[0]);++rejected;
 }
 for(unsigned at=1;at<=3;++at){auto s=make();s->loseProofAt=at;CHECK(s->invoke(11,wire[0])==RT::Error::Evidence);retired(*s,wire[0]);++rejected;}
 for(unsigned mode=1;mode<=15;++mode){auto s=make();s->mode=mode;const auto result=s->invoke(11,wire[0]);
  CHECK((result==RT::Error::Ok)==(mode==1||mode==12));
  if(result==RT::Error::Ok){CHECK(s->arithmetic(1)==(mode==12));
   if(mode==1)save(out+"/wrong-arithmetic-allowed-location.bin",s->capDevice[0]);
  }else{retired(*s,wire[0]);++rejected;}
 }
 {auto s=make();const Bytes before=s->mem.image;s->failAt=34;
  CHECK(s->invoke(11,wire[0])==RT::Error::Evidence);retired(*s,wire[0]);CHECK(s->mem.image==before);
  CHECK(s->state.captures[0].result.passed&&!s->state.captureVerified[0]&&s->window==0x80173d90);
  save(out+"/failed-stage-device.bin",s->capDevice[0]);++rejected;
 }
 for(unsigned kind=0;kind<3;++kind)for(unsigned at=1;at<=prepOps;++at){auto s=empty();
  if(kind==0)s->failAt=at;else if(kind==1)s->loseAt=at;else s->corruptAt=at;
  CHECK(!s->prepare());CHECK(!s->state.access.prepared&&!s->state.access.opened);const auto ops=s->ops;
  CHECK(!s->prepare());CHECK(s->ops==ops);CHECK(s->invoke(11,wire[0])==RT::Error::State);++prepareRejected;
 }
 for(unsigned kind=0;kind<2;++kind)for(unsigned at=2;at<=prepClocks;++at){auto s=empty();
  if(kind==0)s->clockFault=at;else s->timeoutAt=at;
  CHECK(!s->prepare());CHECK(!s->state.access.prepared);++prepareRejected;
 }
 for(unsigned kind=0;kind<9;++kind){auto s=empty();
  if(kind==0)s->state.captures[0].device=s->capture.data();
  if(kind==1)s->state.captures[1].root=s->state.captures[0].root;
  if(kind==2)s->state.captures[2].children=s->mem.children.data();
  if(kind==3)s->state.submission.capture=s->mem.image.data();
  if(kind==4)s->state.access.storage.scratch=s->state.access.proofScratch;
  if(kind==5)s->state.captures[3].root=nullptr;
  if(kind==6)s->state.submission.plans[0]=s->state.submission.plans[1];
  if(kind==7)s->mem.result.memory.passed=false;
  if(kind==8)s->mem.image[0]^=1;
  CHECK(!s->prepare());CHECK(s->ops==0&&!s->writes&&!s->bells);++prepareRejected;
 }
 for(unsigned kind=0;kind<6;++kind){auto s=empty();CHECK(s->prepare());s->counters();
  s->window=0x80173d90;s->originalRestored=true;ProgramRuntimeSim::OpenIO io{*s};
  if(kind==0)s->originalRestored=false;
  if(kind==1)s->window=1;
  if(kind==2)s->failAt=1;
  if(kind==3)s->mode=17;
  if(kind==4)s->mode=18;
  if(kind==5)s->owned=false;
  CHECK(!RT::open(io,s->state,0x80173d90));CHECK(s->state.closed&&s->retentions==1&&!s->access.opened);
  const auto ops=s->ops;CHECK(!RT::open(io,s->state,0x80173d90));CHECK(s->ops==ops);++prepareRejected;
 }
 for(unsigned kind=0;kind<5;++kind){auto s=make();Bytes bad=wire[0];uint64_t caller=11;RT::Error expected=RT::Error::Shape;
  if(kind==0){caller=12;expected=RT::Error::Identity;}
  if(kind==1)bad.resize(RQ::WireBytes-1);
  if(kind==2){PG::Q::put64(bad.data()+24,2);expected=RT::Error::Order;}
  if(kind==3){PG::Q::put64(bad.data()+16,123);expected=RT::Error::Identity;}
  if(kind==4)bad[40]=1;
  CHECK(s->invoke(caller,bad)==expected&&s->ops==0&&!s->retentions&&!s->state.closed);++argumentRejected;
 }
 {auto s=make();CHECK(s->close(11));CHECK(s->retentions==1&&!s->ops);CHECK(s->invoke(11,wire[0])==RT::Error::State);++argumentRejected;}
 printf("{\"passed\":true,\"scenarios\":%u,\"checks\":%u,\"dispatch_rejections\":%u,\"prepare_open_rejections\":%u,\"argument_rejections\":%u,\"requests\":4,\"programs\":3,\"io_per_request\":%u,\"clocks_per_request\":%u,\"timer_origins_per_request\":%zu,\"prepare_reads\":%u,\"prepare_clocks\":%u,\"state_bytes\":%zu,\"full_capture_slots\":4,\"close_retains\":true,\"arithmetic_location_separation_verified\":true,\"cpu_simulation_only\":true,\"gpu_commands_submitted\":false}\n",
  runtimeScenarios,checks,rejected,prepareRejected,argumentRejected,operations,clocks,starts.size(),prepOps,prepClocks,sizeof(RT::State));
}
