#include "HostSimulation.hpp"
#include "ComputeMemory.hpp"
namespace CM=ComputeMemory;
static unsigned computeScenarios=0;
struct ComputeSim {
 Bytes root,tables,backing=Bytes(CM::Bytes,0x55),rootScratch=Bytes(12288),expected=Bytes(L::MaxChildBytes),children=Bytes(L::MaxChildBytes),image=Bytes(CM::Bytes),command=Bytes(32),scratch=Bytes(4096);
 CM::Storage storage;CM::Result result;
 bool owned=true,claimed=false,sticky=false;unsigned ops=0,reads=0,writes=0,memoryWrites=0,registerWrites=0;
 unsigned failAt=0,loseAt=0,corruptRead=0,unreadableReg=0,clocks=0,clockFault=0,timeoutAt=0,pending=0,pdb=0,upper=0,window=0;
 unsigned long long time=100;std::vector<unsigned> writeAddresses;
 ComputeSim(RuntimeSim &runtime):root(runtime.vRoot),tables(runtime.vTables){
  ++computeScenarios;
  storage={runtime.root.data(),runtime.children.data(),runtime.fullImage.data(),runtime.storage.goldenBytes,runtime.contexts.childBytes,
   rootScratch.data(),expected.data(),children.data(),image.data(),command.data(),scratch.data()};
 }
 bool ready(){return owned;}
 unsigned long long nowNs(){++clocks;if(clocks==clockFault)return 0;time+=100;if(clocks==timeoutAt)time+=M::BudgetNs;return time;}
 bool claim(){if(!owned||claimed)return false;claimed=true;return true;}
 bool operation(){++ops;if(ops==loseAt)owned=false;return ops!=failAt;}
 unsigned char *at(unsigned a,unsigned n){
  CHECK(n&&n<=4096&&!(a&3)&&!(n&3));
  if(a>=L::OldBase&&a-L::OldBase+n<=root.size())return root.data()+a-unsigned(L::OldBase);
  if(a>=L::NewBase&&a-L::NewBase+n<=storage.liveBytes)return tables.data()+a-unsigned(L::NewBase);
  if(a>=CM::Base&&a-CM::Base+n<=backing.size())return backing.data()+a-CM::Base;
  CHECK(false);return nullptr;
 }
 bool readMemory(unsigned a,unsigned char *out,unsigned n){
  CHECK(claimed);++reads;if(!operation())return false;std::memcpy(out,at(a,n),n);if(reads==corruptRead)out[0]^=1;return true;
 }
 bool writeMemory(unsigned a,const unsigned char *data,unsigned n){
  CHECK(claimed);++writes;const unsigned phase=memoryWrites++;CHECK(phase<18);
  if(phase<6){CHECK(a==CM::Base+phase*4096&&n==4096);CHECK(std::memcmp(data,image.data()+phase*4096,n)==0);}
  else{
   const unsigned off=CM::PteOffset+(phase-6)/2*8+(phase%2==0?4:0);
   CHECK(a==L::NewBase+off&&n==4);CHECK(std::memcmp(backing.data(),image.data(),CM::Bytes)==0);
   CHECK(std::memcmp(data,children.data()+off,n)==0);
   if(phase%2)CHECK(R::get32(at(a+4,4))==R::get32(children.data()+off+4));
  }
  // Completion words are zero in the image. A CPU store cannot fake shader success.
  CHECK(R::get32(image.data()+16384)==0&&R::get32(image.data()+20480)==0);
  writeAddresses.push_back(a);const bool ok=operation();std::memcpy(at(a,n),data,n);return ok;
 }
 bool readRegister(unsigned a,unsigned &value){
  ++reads;if(!operation())return false;
  if(a==M::Window)value=window;
  else if(a==GMMUInvalidate::PDBRegister)value=pdb;
  else if(a==GMMUInvalidate::UpperRegister)value=upper;
  else {CHECK(a==GMMUInvalidate::CommandRegister);value=pending?0x80000041:0x41;if(pending&&!sticky)--pending;}
  if(reads==unreadableReg)value=0xbadf1100;return true;
 }
 bool writeRegister(unsigned a,unsigned value){
  ++writes;CHECK(memoryWrites==18);const unsigned phase=registerWrites++;CHECK(phase<3);
  const unsigned addresses[]={GMMUInvalidate::PDBRegister,GMMUInvalidate::UpperRegister,GMMUInvalidate::CommandRegister};
  const unsigned values[]={GMMUInvalidate::PDBValue,0,GMMUInvalidate::CommandValue};CHECK(a==addresses[phase]&&value==values[phase]);
  const bool ok=operation();if(phase==0)pdb=value;else if(phase==1)upper=value;else pending=2;return ok;
 }
 bool run(const C::Plan &golden,const TX::Result &execution,const H::Result &host){
  const bool pass=CM::execute(*this,golden,execution,host,storage,result);
  CHECK(pass==result.memory.passed);CHECK(memoryWrites<=18&&registerWrites<=3&&result.memory.linksPublished<=6);
  if(pass)CHECK(CM::ready(result,storage.liveBytes)&&memoryWrites==18&&registerWrites==3);
  return pass;
 }
};
int main(int argc,char **argv){
 CHECK(argc==4);const auto gr=load(argv[1]),fifo=load(argv[2]);RuntimeSim runtime(gr,fifo);Bytes requests,records;
 const auto execution=run(runtime,&requests,&records);CHECK(execution.rpc.passed);
 L::Range goldenRanges[10],executionRanges[6];CHECK(C::mappingRanges(runtime.golden,goldenRanges)&&P::mappings(runtime.plan,runtime.golden,executionRanges));
 walk(runtime,goldenRanges,10,runtime.contexts.childBytes);walk(runtime,executionRanges,6,runtime.contexts.childBytes);
 FenceSim hostIO;const auto host=fenceRun(hostIO,runtime.golden,execution,requests,records);CHECK(host.passed);
 ComputeSim good(runtime);CHECK(good.run(runtime.golden,execution,host));
 CHECK(good.root==runtime.vRoot&&good.backing==good.image);
 for(unsigned i=0;i<good.storage.liveBytes;++i)if(i<CM::PteOffset||i>=CM::PteOffset+CM::PteBytes)CHECK(good.tables[i]==runtime.vTables[i]);
 for(unsigned page=0;page<6;++page)for(unsigned edge:{0U,4095U}){
  bool mapped=false;uint64_t pa=0;CHECK(L::walk(good.root.data(),good.root.size(),good.tables.data(),good.storage.liveBytes,QmdProfile::ProgramVA+page*4096+edge,mapped,pa));
  CHECK(mapped&&pa==CM::Base+page*4096+edge);
 }
 for(unsigned i=1;i<=good.ops;++i){ComputeSim io(runtime);io.failAt=i;CHECK(!io.run(runtime.golden,execution,host));CHECK(io.ops==i);}
 for(unsigned i=1;i<=good.ops;++i){ComputeSim io(runtime);io.loseAt=i;CHECK(!io.run(runtime.golden,execution,host));}
 for(unsigned i:{2U,3U,20U,45U,70U}){ComputeSim io(runtime);io.clockFault=i;CHECK(!io.run(runtime.golden,execution,host));}
 for(unsigned i:{2U,3U,20U,45U,70U}){ComputeSim io(runtime);io.timeoutAt=i;CHECK(!io.run(runtime.golden,execution,host));}
 for(unsigned mode=0;mode<10;++mode){auto bad=host;
  switch(mode){case 0:bad.passed=false;break;case 1:bad.claimed=false;break;case 2:bad.bellAttempted=false;break;case 3:bad.lastFence=0;break;
   case 4:bad.lastGet=0;break;case 5:bad.lastPut=0;break;case 6:bad.token^=1;break;case 7:bad.command[0]^=1;break;case 8:bad.entry[0]^=1;break;case 9:bad.writes=2;break;}
  ComputeSim io(runtime);CHECK(!io.run(runtime.golden,execution,bad)&&!io.claimed&&io.writes==0);
 }
 for(unsigned mode=0;mode<8;++mode){ComputeSim io(runtime);
  switch(mode){case 0:io.owned=false;break;case 1:io.claimed=true;break;case 2:io.window=1;break;case 3:io.root[800]^=1;break;
   case 4:io.tables[800]^=1;break;case 5:io.tables[CM::PteOffset]^=1;break;case 6:R::put32(io.backing.data()+4092,0xbadf1100);break;case 7:io.storage.liveBytes-=4096;break;}
  CHECK(!io.run(runtime.golden,execution,host)&&io.writes==0);
 }
 {ComputeSim io(runtime);io.sticky=true;CHECK(!io.run(runtime.golden,execution,host)&&io.result.memory.failure==M::Failure::Invalidate);}
 {ComputeSim io(runtime);io.storage.image=io.scratch.data();CHECK(!io.run(runtime.golden,execution,host)&&io.writes==0);}
 {ComputeSim io(runtime);io.storage.command=io.image.data();CHECK(!io.run(runtime.golden,execution,host)&&io.writes==0);}
 {ComputeSim io(runtime);io.storage.liveChildren=io.children.data();CHECK(!io.run(runtime.golden,execution,host)&&io.writes==0);}
 {ComputeSim io(runtime);io.storage.rootScratch=reinterpret_cast<unsigned char *>(&io.result);const auto before=io.root;CHECK(!io.run(runtime.golden,execution,host)&&io.root==before&&!io.claimed);}
 CHECK(!good.run(runtime.golden,execution,host)&&good.memoryWrites==18&&good.registerWrites==3);
 save(std::string(argv[3])+"/image.bin",good.image);save(std::string(argv[3])+"/command.bin",good.command);
 save(std::string(argv[3])+"/children.bin",Bytes(good.tables.begin(),good.tables.begin()+good.storage.liveBytes));
 save(std::string(argv[3])+"/root.bin",good.root);
 Bytes trace(18*4);for(unsigned i=0;i<18;++i)R::put32(trace.data()+i*4,good.writeAddresses[i]);save(std::string(argv[3])+"/write-trace.bin",trace);
 std::cout<<"{\"passed\":true,\"hardware_accessed\":false,\"compute_verified\":false,\"metal_verified\":false,\"scenarios\":"<<computeScenarios
  <<",\"checks\":"<<checks<<",\"baseline_io_operations\":"<<good.ops<<",\"baseline_memory_writes\":18,\"baseline_register_writes\":3,\"added_pages\":6}\n";
}
