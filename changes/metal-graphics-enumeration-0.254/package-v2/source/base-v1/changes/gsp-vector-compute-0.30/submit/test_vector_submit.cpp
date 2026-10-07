#include "SubmissionSimulation.hpp"
#include "VectorSubmit.hpp"
namespace CS=VectorSubmit;
static unsigned submitScenarios=0;
struct SubmitSim {
 Bytes root,tables,backing,queue;
 bool owned=true,claimed=false,notified=false,physical=true;
 unsigned reads=0,writes=0,bells=0,polls=0,ops=0,clocks=0,mode=0,failAt=0,loseAt=0,clockFault=0,timeoutAt=0;
 unsigned long long time=1000;std::vector<unsigned> writesAt;
 SubmitSim(const ComputeSim &memory,const FenceSim &host):root(memory.root),tables(memory.tables),backing(memory.backing),queue(host.memory){++submitScenarios;}
 bool ready(){return owned;}
 unsigned long long nowNs(){++clocks;if(clocks==clockFault)return 0;if(mode!=15)time+=1000;if(clocks==timeoutAt)time+=CS::BudgetNs;return time;}
 void delayUs(unsigned us){CHECK(us==100);if(mode!=15)time+=100000000;}
 bool claim(){if(!owned||claimed)return false;claimed=true;return true;}
 bool physicalMode(){return physical;}
 bool operation(){++ops;if(ops==loseAt)owned=false;return ops!=failAt;}
 unsigned char *at(unsigned a,unsigned n){
  CHECK(n&&n<=256&&!(a&3)&&!(n&3));
  if(a>=L::OldBase&&a-L::OldBase+n<=root.size())return root.data()+a-unsigned(L::OldBase);
  if(a>=L::NewBase&&a-L::NewBase+n<=tables.size())return tables.data()+a-unsigned(L::NewBase);
  if(a>=CM::Base&&a-CM::Base+n<=backing.size())return backing.data()+a-CM::Base;
  if(a>=H::Ring&&a-H::Ring+n<=queue.size())return queue.data()+a-H::Ring;
  CHECK(false);return nullptr;
 }
 void set(unsigned a,unsigned v){R::put32(at(a,4),v);}
 // Emulates external device writes only in CPU tests. Never evidence of GPU execution.
 void deviceProgress(){
  ++polls;
  if(mode!=1&&mode!=15&&polls>=3){set(H::Get,2);for(unsigned i=0;i<61;++i)set(QmdProfile::OutputPhysical+i*4,R::get32(backing.data()+8192+0x200+i*4)+R::get32(backing.data()+8192+0x300+i*4));set(QmdProfile::FencePhysical,RtxVector030::Completion);}
  switch(mode){
   case 2:set(H::Get,3);break;case 3:set(H::Put,1);break;
   case 4:set(QmdProfile::OutputPhysical,0xbadf1000);break;case 5:set(QmdProfile::FencePhysical,0xbadf2000);break;
   case 6:if(polls>=3)backing[16384+17]^=1;break;case 7:if(polls>=3)backing[20480+19]^=1;break;
   case 8:if(polls>=3)backing[32]^=1;break;case 9:if(polls>=3)backing[8192+16]^=1;break;
   case 10:if(polls>=3)backing[12288+300]^=1;break;case 11:if(polls>=3)queue[0x2000]^=1;break;
   case 12:if(polls>=3)queue[8]^=1;break;case 13:if(polls>=4)set(QmdProfile::OutputPhysical,1);break;
   case 16:if(polls>=3)queue[0x2000+48]^=1;break;case 17:set(H::Fence,0);break;
   case 18:if(polls>=3)backing[12288]=1;break;
   case 19:if(polls<3)set(QmdProfile::OutputPhysical,0U);break;
   case 20:if(polls<3)set(QmdProfile::FencePhysical,RtxVector030::Completion);break;
   case 21:if(polls<3)set(H::Get,2);break;
   case 22:if(polls>=3)root[900]^=1;break;case 23:if(polls>=3)tables[600]^=1;break;
   case 24:if(polls>=3)set(QmdProfile::OutputPhysical+60*4,~(R::get32(backing.data()+8192+0x200+60*4)+R::get32(backing.data()+8192+0x300+60*4)));break;
   case 25:if(polls>=3)backing[16384+61*4]^=1;break;
   case 26:if(polls>=3)backing[16384+256]^=1;break;
  }
 }
 bool readMemory(unsigned a,unsigned char *out,unsigned n){
  CHECK(claimed);++reads;if(notified&&a==H::Get)deviceProgress();if(!operation())return false;std::memcpy(out,at(a,n),n);return true;
 }
 bool writeMemory(unsigned a,const unsigned char *data,unsigned n){
  CHECK(claimed&&!notified);++writes;CHECK(writes<=3);
  CHECK((writes==1&&a==CS::Command&&n==32)||(writes==2&&a==CS::Entry&&n==8)||(writes==3&&a==H::Put&&n==4));
  CHECK(a!=QmdProfile::OutputPhysical&&a!=QmdProfile::FencePhysical&&a!=H::Fence);
  writesAt.push_back(a);const bool ok=operation();std::memcpy(at(a,n),data,n);return ok;
 }
 bool notify(unsigned token){CHECK(claimed&&!notified&&writes==3&&token==4);notified=true;++bells;return operation()&&mode!=14;}
 CS::Result run(const ComputeSim &memory,const TX::Result &execution,const H::Result &host){
  CS::Result r;const bool ok=CS::execute(*this,memory.storage,memory.result,execution,host,r);
  CHECK(ok==r.passed&&writes<=3&&bells<=1&&r.operations<=CS::MaxOperations);
  if(ok)CHECK(!r.failure&&r.claimed&&r.putAttempted&&r.bellAttempted&&r.immutableVerified&&r.guardsVerified&&r.stable&&r.get==2&&r.put==2&&
    r.completedElements==61&&r.output[0]==0U&&r.initialOutput[0]==0xffffffffU&&r.completion==RtxVector030::Completion&&r.initialGet==1&&r.initialPut==1&&!r.initialCompletion);
  return r;
 }
};
int main(int argc,char **argv){
 CHECK(argc==4);const auto gr=load(argv[1]),fifo=load(argv[2]);RuntimeSim runtime(gr,fifo);Bytes requests,records;
 const auto execution=run(runtime,&requests,&records);CHECK(execution.rpc.passed);
 L::Range goldenRanges[10],executionRanges[6];CHECK(C::mappingRanges(runtime.golden,goldenRanges)&&P::mappings(runtime.plan,runtime.golden,executionRanges));
 walk(runtime,goldenRanges,10,runtime.contexts.childBytes);walk(runtime,executionRanges,6,runtime.contexts.childBytes);
 FenceSim hostIO;const auto host=fenceRun(hostIO,runtime.golden,execution,requests,records);CHECK(host.passed);
 ComputeSim memory(runtime);CHECK(memory.run(runtime.golden,execution,host));
 SubmitSim good(memory,hostIO);const auto result=good.run(memory,execution,host);CHECK(result.passed);
 CHECK(good.writesAt==std::vector<unsigned>({CS::Command,CS::Entry,H::Put}));
 CHECK(RtxVector030::validateCapture(good.backing.data(),good.backing.size(),memory.image.data(),memory.image.size(),61));
 for(unsigned i=0;i<64;++i){const auto expected=R::get32(memory.image.data()+8192+0x200+i*4)+R::get32(memory.image.data()+8192+0x300+i*4);CHECK(result.initialOutput[i]==~expected);CHECK(result.output[i]==(i<61?expected:~expected));}
 CHECK(std::memcmp(good.queue.data()+0x2000,host.command,20)==0&&std::memcmp(good.queue.data(),host.entry,8)==0);
 for(unsigned i=1;i<=good.ops;++i){SubmitSim io(memory,hostIO);io.failAt=i;CHECK(!io.run(memory,execution,host).passed&&io.ops==i);}
 for(unsigned i=1;i<=good.ops;++i){SubmitSim io(memory,hostIO);io.loseAt=i;CHECK(!io.run(memory,execution,host).passed);}
 for(unsigned mode=1;mode<=26;++mode){SubmitSim io(memory,hostIO);io.mode=mode;const auto r=io.run(memory,execution,host);
  CHECK(r.passed==(mode==18||mode==19||mode==20||mode==21));
  if(mode==15)CHECK(r.failure==CS::Timeout&&r.operations==CS::MaxOperations);
 }
 for(unsigned i:{2U,3U,20U,400U,800U}){SubmitSim io(memory,hostIO);io.clockFault=i;CHECK(io.run(memory,execution,host).failure==CS::Clock);}
 for(unsigned i:{2U,3U,20U,400U,800U}){SubmitSim io(memory,hostIO);io.timeoutAt=i;CHECK(io.run(memory,execution,host).failure==CS::Timeout);}
 for(unsigned mode=0;mode<9;++mode){SubmitSim io(memory,hostIO);
  switch(mode){case 0:io.owned=false;break;case 1:io.claimed=true;break;case 2:io.physical=false;break;case 3:io.set(H::Get,0);break;
   case 4:io.set(H::Put,2);break;case 5:io.set(QmdProfile::OutputPhysical,0U);break;
   case 6:io.set(QmdProfile::FencePhysical,RtxVector030::Completion);break;case 7:io.root[500]^=1;break;case 8:io.tables[500]^=1;break;}
  CHECK(!io.run(memory,execution,host).passed&&io.writes==0);
 }
 for(unsigned offset:{0U,256U,8191U,8192U,12287U,12288U,12543U,12544U,16383U,16384U,20479U,20480U,24575U}){
  SubmitSim io(memory,hostIO);io.backing[offset]^=1;CHECK(!io.run(memory,execution,host).passed&&io.writes==0);
 }
 for(unsigned mode=0;mode<8;++mode){auto bad=memory.result;
  switch(mode){case 0:bad.claimed=false;break;case 1:bad.memory.passed=false;break;case 2:bad.memory.linksPublished=5;break;
   case 3:bad.memory.invalidation.passed=false;break;case 4:bad.memory.verifiedBackingBytes=0;break;case 5:bad.memory.inspectedBytes=0;break;
   case 6:bad.memory.verifiedChildBytes=0;break;case 7:bad.hostVerified=false;break;}
  SubmitSim io(memory,hostIO);CS::Result r;CHECK(!CS::execute(io,memory.storage,bad,execution,host,r)&&!io.claimed&&io.writes==0);
 }
 for(unsigned i=0;i<32;++i){auto s=memory.storage;Bytes command=memory.command;command[i]^=1;s.command=command.data();SubmitSim io(memory,hostIO);CS::Result r;
  CHECK(!CS::execute(io,s,memory.result,execution,host,r)&&!io.claimed&&io.writes==0);
 }
 {SubmitSim io(memory,hostIO);auto s=memory.storage;CS::Result r;s.image=reinterpret_cast<unsigned char *>(&r);CHECK(!CS::execute(io,s,memory.result,execution,host,r)&&!io.claimed);}
 {SubmitSim io(memory,hostIO);auto bad=host;bad.passed=false;CS::Result r;CHECK(!CS::execute(io,memory.storage,memory.result,execution,bad,r)&&!io.claimed);}
 CHECK(!good.run(memory,execution,host).passed&&good.bells==1&&good.writes==3);
 save(std::string(argv[3])+"/command.bin",Bytes(result.command,result.command+32));save(std::string(argv[3])+"/entry.bin",Bytes(result.entry,result.entry+8));
 Bytes trace(12);for(unsigned i=0;i<3;++i)R::put32(trace.data()+i*4,good.writesAt[i]);save(std::string(argv[3])+"/write-trace.bin",trace);
 save(std::string(argv[3])+"/simulated-queue.bin",good.queue);save(std::string(argv[3])+"/simulated-backing.bin",good.backing);
 std::cout<<"{\"passed\":true,\"hardware_accessed\":false,\"compute_verified\":false,\"metal_verified\":false,\"scenarios\":"<<submitScenarios
  <<",\"checks\":"<<checks<<",\"baseline_io_operations\":"<<good.ops<<",\"baseline_reads\":"<<result.reads<<",\"baseline_writes\":"<<result.writes
  <<",\"baseline_polls\":"<<result.polls<<",\"baseline_operations\":"<<result.operations<<"}\n";
}
