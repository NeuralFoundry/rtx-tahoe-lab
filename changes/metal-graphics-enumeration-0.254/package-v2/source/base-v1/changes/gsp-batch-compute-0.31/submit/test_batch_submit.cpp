#include "BatchSimulation.hpp"
int main(int argc,char **argv){
 CHECK(argc==4);const auto gr=load(argv[1]),fifo=load(argv[2]);RuntimeSim runtime(gr,fifo);Bytes requests,records;
 const auto execution=run(runtime,&requests,&records);CHECK(execution.rpc.passed);
 L::Range goldenRanges[10],executionRanges[6];CHECK(C::mappingRanges(runtime.golden,goldenRanges)&&P::mappings(runtime.plan,runtime.golden,executionRanges));
 walk(runtime,goldenRanges,10,runtime.contexts.childBytes);walk(runtime,executionRanges,6,runtime.contexts.childBytes);
 FenceSim hostIO;const auto host=fenceRun(hostIO,runtime.golden,execution,requests,records);CHECK(host.passed);
 ComputeSim memory(runtime);CHECK(memory.run(runtime.golden,execution,host));
 SubmitSim good(memory,hostIO);BS::Result results[4];unsigned successfulOps[4]={};
 for(unsigned job=0;job<4;++job){
  const SubmitSim before=good;results[job]=good.run(job,memory,execution,host);CHECK(results[job].passed);successfulOps[job]=good.ops;
  CHECK(V::validateCapture(good.backing.data(),good.backing.size(),memory.image.data(),memory.image.size(),job+1));
  for(unsigned i=1;i<=good.ops;++i){auto io=before;io.failAt=i;CHECK(!io.run(job,memory,execution,host).passed&&io.ops==i);
   CHECK(!Gate::claim(io.gate,job+1));if(io.gate.jobs[job].claimed)CHECK(!Gate::claim(io.gate,job));}
  for(unsigned i=1;i<=good.ops;++i){auto io=before;io.loseAt=i;CHECK(!io.run(job,memory,execution,host).passed);CHECK(!Gate::claim(io.gate,job+1));}
  for(unsigned mode=1;mode<=28;++mode){auto io=before;io.mode=mode;
   const bool allowed=mode==18||mode==19||mode==20||mode==21||(mode==28&&job==3);
   CHECK(io.run(job,memory,execution,host).passed==allowed);
  }
  for(unsigned fault:{2U,3U,20U,45U,70U}){auto io=before;io.clockFault=fault;CHECK(!io.run(job,memory,execution,host).passed);
   io=before;io.timeoutAt=fault;CHECK(!io.run(job,memory,execution,host).passed);}
  for(unsigned target=0;target<4;++target){auto io=before;io.backing[V::fenceOffset(target)]^=1;
   CHECK(!io.run(job,memory,execution,host).passed&&io.writes==0);}
  {auto io=before;io.physical=false;CHECK(!io.run(job,memory,execution,host).passed&&io.writes==0);}
  // Replay cannot change a completed result or publish another write.
  const auto saved=results[job];const auto writes=good.writesAt.size();
  CHECK(!BS::execute(good,job,memory.storage,memory.result,execution,host,results[job]));
  CHECK(!std::memcmp(&saved,&results[job],sizeof(saved))&&good.writesAt.size()==writes);
  // Every missing prerequisite rejects committing the slot for the next job.
  for(unsigned field=0;field<17;++field){auto state=good.gate;state.completed=job;auto bad=results[job];
   switch(field){case 0:bad.passed=false;break;case 1:bad.failure=BS::Write;break;case 2:bad.claimed=false;break;
    case 3:bad.commandAttempted=false;break;case 4:bad.entryAttempted=false;break;case 5:bad.putAttempted=false;break;
    case 6:bad.bellAttempted=false;break;case 7:bad.immutableVerified=false;break;case 8:bad.guardsVerified=false;break;
    case 9:bad.stable=false;break;case 10:bad.writes=2;break;case 11:bad.initialGet^=1;break;case 12:bad.initialPut^=1;break;
    case 13:bad.initialCompletion=1;break;case 14:bad.completion^=1;break;case 15:bad.completedElements^=1;break;case 16:bad.job^=1;break;}
   CHECK(!Gate::finish(state,job,bad)&&state.completed==job);
  }
  save(std::string(argv[3])+"/simulated-prefix-"+std::to_string(job+1)+".bin",good.backing);
 }
 CHECK(good.gate.completed==4);for(unsigned j=0;j<=4;++j)CHECK(!Gate::claim(good.gate,j));
 CHECK(good.writesAt.size()==12);for(unsigned j=0;j<4;++j){CHECK(good.writesAt[j*3]==BS::commandPhysical(j));CHECK(good.writesAt[j*3+1]==BS::entryPhysical(j));CHECK(good.writesAt[j*3+2]==H::Put);}
 // Invalid publication requests do not consume the legitimate next permission.
 for(unsigned j=0;j<4;++j){Gate::State state;state.completed=j;CHECK(Gate::claim(state,j));unsigned char cmd[32],ent[8];CHECK(BS::command(j,cmd)&&BS::entry(j,ent));
  CHECK(!Gate::notify(state,j));CHECK(!Gate::write(state,j,BS::entryPhysical(j),ent,8));
  for(unsigned i=0;i<32;++i){cmd[i]^=1;CHECK(!Gate::write(state,j,BS::commandPhysical(j),cmd,32)&&state.jobs[j].phase==0);cmd[i]^=1;}
  CHECK(!Gate::write(state,j,BS::commandPhysical((j+1)%4),cmd,32));CHECK(Gate::write(state,j,BS::commandPhysical(j),cmd,32));
  CHECK(!Gate::write(state,j,BS::commandPhysical(j),cmd,32));CHECK(Gate::write(state,j,BS::entryPhysical(j),ent,8));
  R::put32(ent,j+1);CHECK(!Gate::write(state,j,H::Put,ent,4));R::put32(ent,j+2);CHECK(Gate::write(state,j,H::Put,ent,4));
  CHECK(Gate::notify(state,j)&&!Gate::notify(state,j));CHECK(!Gate::write(state,j,H::Put,ent,4));
 }
 Bytes trace(12*4);for(unsigned i=0;i<12;++i)R::put32(trace.data()+i*4,good.writesAt[i]);
 save(std::string(argv[3])+"/write-trace.bin",trace);save(std::string(argv[3])+"/simulated-queue.bin",good.queue);
 save(std::string(argv[3])+"/initial-image.bin",memory.image);
 std::cout<<"{\"passed\":true,\"hardware_accessed\":false,\"compute_verified\":false,\"metal_verified\":false,\"jobs\":4,\"scenarios\":"<<batchScenarios
  <<",\"failed_scenarios\":"<<failedCases<<",\"checks\":"<<checks<<",\"successful_io_operations\":["<<successfulOps[0]<<","<<successfulOps[1]<<","<<successfulOps[2]<<","<<successfulOps[3]<<"],\"memory_writes\":12,\"notifications\":4,\"storage_recycled\":false}\n";
}
