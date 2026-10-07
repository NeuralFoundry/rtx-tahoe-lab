#include "RuntimeSimulation.hpp"
int main(int argc,char **argv){
 CHECK(argc==4);const auto gr=load(argv[1]),fifo=load(argv[2]);RuntimeSim initial(gr,fifo);Bytes requests,records;
 const auto execution=run(initial,&requests,&records);CHECK(execution.rpc.passed);
 L::Range goldenRanges[10],executionRanges[6];CHECK(C::mappingRanges(initial.golden,goldenRanges)&&P::mappings(initial.plan,initial.golden,executionRanges));
 walk(initial,goldenRanges,10,initial.contexts.childBytes);walk(initial,executionRanges,6,initial.contexts.childBytes);
 FenceSim hostIO;const auto host=fenceRun(hostIO,initial.golden,execution,requests,records);CHECK(host.passed);
 ComputeSim memory(initial);CHECK(memory.run(initial.golden,execution,host));
 for(unsigned field=0;field<5;++field){ComputeSim invalid(initial);
  if(field==0)invalid.storage.requests=nullptr;
  if(field==1)invalid.requests[0].requestId=2;
  if(field==2)invalid.requests[3].generation=8;
  if(field==3)invalid.requests[1].count=65;
  if(field==4)invalid.requests[2].a[63]=1;
  CHECK(!invalid.run(initial.golden,execution,host)&&invalid.writes==0);
 }
 auto make=[&](){return std::unique_ptr<RuntimeIO>(new RuntimeIO(memory,hostIO,execution,host));};
 const unsigned counts[]={3,17,47,64};unsigned successfulOps[4]={};auto good=make();
 {auto io=make();io->runtimePhase=false;CHECK(!io->run(0,3)&&io->ops==0&&io->writes==0);}
 save(std::string(argv[3])+"/initial.bin",good->image);
 for(unsigned j=0;j<4;++j){CHECK(good->run(j,counts[j]));successfulOps[j]=good->ops;
  const auto prefix=std::string(argv[3])+"/";
  save(prefix+"expected-prefix-"+std::to_string(j+1)+".bin",good->image);
  save(prefix+"simulated-prefix-"+std::to_string(j+1)+".bin",good->backing);
  const auto &slot=good->state.slots[j];save(prefix+"request-"+std::to_string(j)+".bin",Bytes(slot.stage.request,slot.stage.request+A::RequestBytes));
  save(prefix+"plan-"+std::to_string(j)+".bin",Bytes(slot.stage.plan,slot.stage.plan+I::PlanBytes));
 }
 CHECK(good->state.session.phase()==A::Phase::Exhausted);
 for(unsigned j=0;j<4;++j){auto &slot=good->state.slots[j];const auto prior=slot.window;const auto oldOps=good->ops;
  CHECK(!W::acquire(*good,good->state.session,11,0x42,slot.window));CHECK(!W::restore(*good,slot.window));
  CHECK(std::memcmp(&prior,&slot.window,sizeof(prior))==0&&good->ops==oldOps);
  auto queue=good->state.queue;queue.completed=j;auto wrong=slot.submit;wrong.expectedCount^=1;
  CHECK(!AppGate::finish(queue,j,good->state.requests[j],wrong));
  wrong=slot.submit;wrong.output[0]^=1;CHECK(!AppGate::finish(queue,j,good->state.requests[j],wrong));
  wrong=slot.submit;wrong.initialOutput[63]^=1;CHECK(!AppGate::finish(queue,j,good->state.requests[j],wrong));
 }
 // Every size 0..64 in each slot, across real portable request boundaries.
 for(unsigned count=0;count<=64;++count){auto io=make();for(unsigned j=0;j<4;++j)CHECK(io->run(j,count));}
 // Every I/O boundary of the first full request, including uncertain writes,
 // notification, capture and restoration. Prefix jobs are covered separately.
 for(unsigned mode=0;mode<3;++mode)for(unsigned at=1;at<=successfulOps[0];++at){auto io=make();
  if(mode==0)io->failAt=at;else if(mode==1)io->loseAt=at;else io->corruptAt=at;
  const bool ok=io->run(0,counts[0]);
  if(mode<2){CHECK(!ok);++faults;
   if(mode==0&&io->state.slots[0].window.mutationAttempted&&at<successfulOps[0]-1)CHECK(io->window==0x42);
   const auto ops=io->ops;unsigned char wire[A::RequestBytes]={};CHECK(io->invoke(11,wire,sizeof(wire))==A::Error::State&&io->ops==ops);
  }
 }
 for(unsigned j=0;j<4;++j)for(unsigned mode=1;mode<=8;++mode){auto io=make();for(unsigned p=0;p<j;++p)CHECK(io->run(p,counts[p]));
  io->mode=mode;const bool acceptedFault=io->run(j,counts[j]);if(acceptedFault)std::cerr<<"unexpected mode "<<mode<<" slot "<<j<<"\n";
  CHECK(!acceptedFault);++faults;CHECK(io->window==0x42);
 }
 for(unsigned kind=0;kind<2;++kind)for(unsigned at=1;at<=good->clocks;++at){auto io=make();
  if(kind==0)io->clockFault=at;else io->timeoutAt=at;
  // The first clock establishes a baseline. Cleanup establishes its own clock.
  const bool ok=io->run(0,counts[0]);if(!ok)++faults;
 }
 for(unsigned at:{1U,3U}){auto io=make();io->wrongWindowAt=at;CHECK(!io->run(0,counts[0]));++faults;}
 // Invalid requests never reach even a BAR register read or consume readiness.
 {auto io=make();A::Request r;r.generation=7;r.requestId=1;unsigned char wire[A::RequestBytes];CHECK(A::encode(r,wire,sizeof(wire)));
  CHECK(io->invoke(12,wire,sizeof(wire))==A::Error::Identity&&io->ops==0);
  for(unsigned size=0;size<576;++size)CHECK(io->invoke(11,wire,size)==A::Error::Shape&&io->ops==0);
  CHECK(io->invoke(11,wire,577)==A::Error::Shape&&io->ops==0);
  wire[16]^=1;CHECK(io->invoke(11,wire,sizeof(wire))==A::Error::Identity&&io->ops==0);wire[16]^=1;
  wire[24]=2;CHECK(io->invoke(11,wire,sizeof(wire))==A::Error::Order&&io->ops==0);wire[24]=1;
  wire[40]=1;CHECK(io->invoke(11,wire,sizeof(wire))==A::Error::Shape&&io->ops==0);wire[40]=0;
  CHECK(io->invoke(11,wire,sizeof(wire))==A::Error::Ok);const auto ops=io->ops;
  CHECK(io->invoke(11,wire,sizeof(wire))==A::Error::Order&&io->ops==ops);
  CHECK(!io->state.session.close(12)&&io->state.session.phase()==A::Phase::Ready);
  CHECK(io->state.session.close(11));CHECK(io->invoke(11,wire,sizeof(wire))==A::Error::State&&io->ops==ops);
 }
 // Retained startup result cannot satisfy the old in-bootstrap predicate.
 const auto fixed=initial.fixed;auto restored=fixed;restored.windowRestored=true;
 CHECK(!ExecutionMemory::fixedReady(restored,initial.storage.goldenBytes));
 CHECK(!X::readable(0xffffffffU,4,memory.storage.liveBytes));CHECK(!X::readable(CM::Base,4097,memory.storage.liveBytes));
 CHECK(!X::readable(HostFence::Doorbell,4,memory.storage.liveBytes));
 std::cout<<"{\"passed\":true,\"hardware_accessed\":false,\"metal_verified\":false,\"scenarios\":"<<runtimeScenarios<<",\"checks\":"<<checks
  <<",\"rejected_faults\":"<<faults<<",\"count_cases\":260,\"successful_io_operations\":["<<successfulOps[0]<<","<<successfulOps[1]<<","<<successfulOps[2]<<","<<successfulOps[3]
  <<"],\"application_calls\":4,\"slot_recycling\":false,\"bootstrap_cleanup_preserved\":true}\n";
}
