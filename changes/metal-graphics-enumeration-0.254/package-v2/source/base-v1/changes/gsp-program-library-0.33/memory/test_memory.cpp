#include "ProgramMemorySimulation.hpp"
#include <memory>
static unsigned rejected=0;
int main(int argc,char **argv){
 CHECK(argc==6);const auto gr=load(argv[1]),fifo=load(argv[2]),library=load(argv[3]),code=load(argv[4]);
 CHECK(library.size()==512&&code.size()==4096);RuntimeSim initial(gr,fifo);Bytes requests,records;
 const auto execution=run(initial,&requests,&records);CHECK(execution.rpc.passed);
 L::Range goldenRanges[10],executionRanges[6];CHECK(C::mappingRanges(initial.golden,goldenRanges)&&P::mappings(initial.plan,initial.golden,executionRanges));
 walk(initial,goldenRanges,10,initial.contexts.childBytes);walk(initial,executionRanges,6,initial.contexts.childBytes);
 FenceSim hostIO;const auto host=fenceRun(hostIO,initial.golden,execution,requests,records);CHECK(host.passed);
 auto make=[&](){return std::unique_ptr<ProgramMemorySim>(new ProgramMemorySim(initial,library,code));};
 auto good=make();CHECK(good->run(initial.golden,execution,host));
 const unsigned operations=good->ops,clocks=good->clocks;const std::string out=argv[5];
 save(out+"/root.bin",good->root);save(out+"/children-before.bin",Bytes(initial.vTables.begin(),initial.vTables.begin()+good->storage.liveBytes));
 save(out+"/children-after.bin",Bytes(good->tables.begin(),good->tables.begin()+good->storage.liveBytes));
 save(out+"/image.bin",good->image);save(out+"/backing.bin",good->backing);save(out+"/command.bin",good->command);
 Bytes addresses(18*4);for(unsigned i=0;i<18;++i)R::put32(addresses.data()+i*4,good->writeAddresses[i]);save(out+"/writes.bin",addresses);
 CHECK(std::memcmp(good->image.data(),code.data(),4096)==0);
 for(unsigned i=0;i<32;++i)CHECK(good->command[i]==0);
 // Retrying the same result must preserve the first complete/uncertain record.
 {const auto before=good->result;CHECK(!CM::execute(*good,initial.golden,execution,host,good->storage,good->result));
  CHECK(good->ops==operations&&std::memcmp(&before,&good->result,sizeof(before))==0);++rejected;}
 // A new result does not recreate the private I/O claim.
 {CM::Result fresh;CHECK(!CM::execute(*good,initial.golden,execution,host,good->storage,fresh));CHECK(good->ops==operations);++rejected;}
 for(unsigned kind=0;kind<2;++kind)for(unsigned at=1;at<=operations;++at){auto io=make();
  if(kind==0)io->failAt=at;else io->loseAt=at;
  CHECK(!io->run(initial.golden,execution,host));++rejected;
  const auto before=io->result;const unsigned seen=io->ops;
  CHECK(!CM::execute(*io,initial.golden,execution,host,io->storage,io->result));
  CHECK(io->ops==seen&&std::memcmp(&before,&io->result,sizeof(before))==0);
 }
 for(unsigned kind=0;kind<2;++kind)for(unsigned at=2;at<=clocks;++at){auto io=make();
  if(kind==0)io->clockFault=at;else io->timeoutAt=at;
  CHECK(!io->run(initial.golden,execution,host));++rejected;
 }
 for(unsigned kind=0;kind<10;++kind){auto io=make();
  if(kind==0)io->storage.library=nullptr;
  if(kind==1)io->storage.code=nullptr;
  if(kind==2)R::put32(io->library.data()+16,0);
  if(kind==3)io->code[640]=1;
  if(kind==4)R::put32(io->library.data()+68,128);
  if(kind==5)io->storage.image=io->code.data();
  if(kind==6)io->storage.children=io->expected.data();
  if(kind==7)io->storage.liveBytes=8191;
  if(kind==8)io->storage.command=io->library.data();
  if(kind==9)io->owned=false;
  CHECK(!io->run(initial.golden,execution,host));CHECK(io->writes==0);++rejected;
 }
 {auto io=make();auto wrong=host;wrong.passed=false;CHECK(!io->run(initial.golden,execution,wrong)&&io->writes==0);++rejected;}
 {auto io=make();auto wrong=execution;wrong.candidate^=1;CHECK(!io->run(initial.golden,wrong,host)&&io->writes==0);++rejected;}
 {auto io=make();io->root[0]^=1;CHECK(!io->run(initial.golden,execution,host)&&io->writes==0);++rejected;}
 {auto io=make();io->tables[16]^=1;CHECK(!io->run(initial.golden,execution,host)&&io->writes==0);++rejected;}
 {auto io=make();io->sticky=true;CHECK(!io->run(initial.golden,execution,host));++rejected;}
 {auto io=make();io->unreadableReg=1;CHECK(!io->run(initial.golden,execution,host)&&io->writes==0);++rejected;}
 const auto &m=good->result.memory;
 printf("{\"passed\":true,\"scenarios\":%u,\"checks\":%u,\"rejected\":%u,\"operations\":%u,\"clocks\":%u,\"memory_writes\":%u,\"register_writes\":%u,\"links\":%u,\"backing_bytes\":%u,\"child_bytes\":%u,\"gpu_commands_submitted\":false,\"cpu_simulation_only\":true}\n",
  programMemoryScenarios,checks,rejected,operations,clocks,good->memoryWrites,good->registerWrites,m.linksPublished,m.verifiedBackingBytes,m.childBytes);
 return 0;
}
