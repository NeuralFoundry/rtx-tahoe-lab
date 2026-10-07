#include "RuntimeSimulation.hpp"
#include "HostFence.hpp"
#include "reference/dev_vm_tu102.h"
#include <iostream>
namespace H=HostFence;
static unsigned fenceScenarios=0;
struct FenceSim {
  Bytes memory=Bytes(0x4000);bool owned=true,claimed=false,notified=false,mappings=true;
  unsigned reads=0,writes=0,bells=0,polls=0,failRead=0,failWrite=0,loseAt=0,corruptAt=0,mode=0;
  unsigned long long time=100000;std::vector<unsigned> writesAt;
  FenceSim(){++fenceScenarios;}
  bool ready(){return owned;}
  unsigned long long nowNs(){if(mode!=9&&mode!=14)time+=1000;return time;}
  void delayUs(unsigned us){CHECK(us==100);if(mode!=9&&mode!=14)time+=100000000;}
  bool verifyMappings(){return mappings;}
  bool claim(){if(claimed||!owned)return false;claimed=true;return true;}
  void set(unsigned address,unsigned value){R::put32(memory.data()+address-H::Ring,value);}
  bool readMemory(unsigned address,unsigned char *out,unsigned bytes){
    CHECK(claimed&&address>=H::Ring&&address+bytes<=H::Ring+memory.size()&&bytes<=64&&bytes%4==0);++reads;
    if(notified&&address==H::Get){
      ++polls;
      if(mode!=1&&mode!=14&&polls>=3){set(H::Get,1);set(H::Fence,SubmitCodec::FenceValue);}
      if(mode==2)set(H::Get,2);if(mode==3)set(H::Put,0);if(mode==4)set(H::Fence,0xbadf0000);
      if(mode==5&&polls>=3)set(H::Fence+12,1);
      if(mode==6&&polls>=4)set(H::Fence,0);
      if(mode==11&&polls<3)set(H::Get,1);
      if(mode==12&&polls<3)set(H::Fence,SubmitCodec::FenceValue);
    }
    if(reads==failRead)return false;
    std::memcpy(out,memory.data()+address-H::Ring,bytes);if(reads==corruptAt)out[0]^=1;
    if(reads==loseAt)owned=false;if(mode==7&&reads==2)time=H::BudgetNs;if(mode==8&&reads==2)time=0;
    return true;
  }
  bool writeMemory(unsigned address,const unsigned char *data,unsigned bytes){
    CHECK(claimed&&!notified);++writes;CHECK(writes<=3);
    CHECK((writes==1&&address==H::Command&&bytes==20)||(writes==2&&address==H::Ring&&bytes==8)||(writes==3&&address==H::Put&&bytes==4));
    CHECK(address!=H::Fence);writesAt.push_back(address);std::memcpy(memory.data()+address-H::Ring,data,bytes);
    if(mode==10&&writes==2)set(H::Fence,SubmitCodec::FenceValue);
    return writes!=failWrite;
  }
  bool notify(unsigned token){CHECK(claimed&&!notified&&writes==3&&R::get32(memory.data()+H::Put-H::Ring)==1&&token==4);notified=true;++bells;return mode!=13;}
};
static H::Result fenceRun(FenceSim &io,const C::Plan &golden,const TX::Result &execution,const Bytes &requests,const Bytes &records){
  Bytes scratch(4096);H::Result r;const bool ok=H::execute(io,golden,execution,requests.data(),records.data(),scratch.data(),r);
  CHECK(ok==r.passed&&io.bells<=1&&io.writes<=3&&r.operations<=H::MaxOperations+1);
  if(ok)CHECK(!r.failure&&r.claimed&&r.putAttempted&&r.bellAttempted&&r.initialGet==0&&r.initialPut==0&&r.initialFence==0&&r.lastGet==1&&r.lastPut==1&&r.lastFence==SubmitCodec::FenceValue);
  return r;
}
int main(int argc,char **argv){
  CHECK(argc==4);const auto gr=load(argv[1]),fifo=load(argv[2]);RuntimeSim runtime(gr,fifo);Bytes requests,records;
  const auto execution=run(runtime,&requests,&records);CHECK(execution.rpc.passed);
  L::Range oldRanges[10],newRanges[6];CHECK(C::mappingRanges(runtime.golden,oldRanges)&&P::mappings(runtime.plan,runtime.golden,newRanges));
  walk(runtime,oldRanges,10,runtime.contexts.childBytes);walk(runtime,newRanges,6,runtime.contexts.childBytes);
  static_assert(H::Doorbell==0xb80000+NV_VIRTUAL_FUNCTION_DOORBELL&&H::Doorbell==0xbb0090,"NVIDIA doorbell offset");
  FenceSim baseline;const auto result=fenceRun(baseline,runtime.golden,execution,requests,records);CHECK(result.passed);
  CHECK(baseline.writesAt==std::vector<unsigned>({H::Command,H::Ring,H::Put}));
  CHECK(Bytes(result.command,result.command+20)==Bytes({4,0,4,32,16,0,0,0,0,32,0,32,1,36,96,48,2,0,0,1}));
  save(std::string(argv[3])+"/command.bin",Bytes(result.command,result.command+20));save(std::string(argv[3])+"/entry.bin",Bytes(result.entry,result.entry+8));
  for(unsigned i=1;i<=baseline.reads;++i){FenceSim io;io.failRead=i;CHECK(!fenceRun(io,runtime.golden,execution,requests,records).passed);}
  for(unsigned i=1;i<=baseline.reads;++i){FenceSim io;io.loseAt=i;CHECK(!fenceRun(io,runtime.golden,execution,requests,records).passed);}
  for(unsigned i=1;i<=3;++i){FenceSim io;io.failWrite=i;const auto r=fenceRun(io,runtime.golden,execution,requests,records);CHECK(!r.passed&&io.writes==i&&io.bells==0);}
  for(unsigned mode:{1U,2U,3U,4U,5U,6U,7U,8U,10U,13U,14U}){FenceSim io;io.mode=mode;CHECK(!fenceRun(io,runtime.golden,execution,requests,records).passed);}
  {FenceSim io;io.mode=9;io.failRead=0;const auto r=fenceRun(io,runtime.golden,execution,requests,records);CHECK(r.passed);}
  for(unsigned mode:{11U,12U}){FenceSim io;io.mode=mode;CHECK(fenceRun(io,runtime.golden,execution,requests,records).passed);}
  for(unsigned address:{H::Get,H::Put,H::Fence,H::Fence+4092,H::Ring+252,H::Command+4092})for(unsigned value:{1U,0xffffffffU,SubmitCodec::FenceValue}){
    FenceSim io;io.set(address,value);CHECK(!fenceRun(io,runtime.golden,execution,requests,records).passed&&io.writes==0&&io.bells==0);
  }
  for(unsigned i:{1U,3U,4U,20U,133U,134U,135U}){FenceSim io;io.corruptAt=i;CHECK(!fenceRun(io,runtime.golden,execution,requests,records).passed);}
  {FenceSim io;io.owned=false;CHECK(!fenceRun(io,runtime.golden,execution,requests,records).passed&&io.writes==0);}
  {FenceSim io;io.mappings=false;CHECK(!fenceRun(io,runtime.golden,execution,requests,records).passed&&!io.claimed);}
  {FenceSim io;io.claimed=true;CHECK(!fenceRun(io,runtime.golden,execution,requests,records).passed&&io.writes==0);}
  for(unsigned mode=0;mode<12;++mode){auto bad=execution;
    switch(mode){case 0:bad.rpc.passed=false;break;case 1:bad.rpc.failure=R::Owner;break;case 2:bad.rpc.txReader=EC::FinalProducer-1;break;
      case 3:bad.channelId=5;break;case 4:bad.rawToken=5;break;case 5:bad.candidate=5;break;case 6:bad.runlist.id=1;break;
      case 7:bad.context.buffers[0].physical+=4096;break;case 8:bad.rpc.records[4].slot=50;break;case 9:bad.rpc.records[4].step=9;break;
      case 10:bad.fixedPrepared=false;break;case 11:bad.rpc.bytes=~0U;break;}
    FenceSim io;CHECK(!fenceRun(io,runtime.golden,bad,requests,records).passed&&io.writes==0&&!io.claimed);
  }
  for(unsigned step=0;step<EC::Steps;++step){Bytes bad=records;bad[step*4096+80]^=1;FenceSim io;CHECK(!fenceRun(io,runtime.golden,execution,requests,bad).passed&&!io.claimed);}
  {FenceSim io;Bytes scratch(4096);auto copy=execution;H::Result resultAlias;CHECK(!H::execute(io,runtime.golden,copy,requests.data(),records.data(),reinterpret_cast<unsigned char *>(&resultAlias),resultAlias));}
  CHECK(!fenceRun(baseline,runtime.golden,execution,requests,records).passed&&baseline.bells==1&&baseline.writes==3);
  std::cout<<"{\"passed\":true,\"hardware_accessed\":false,\"compute_verified\":false,\"metal_verified\":false,\"scenarios\":"<<fenceScenarios<<",\"checks\":"<<checks
    <<",\"baseline_reads\":"<<result.reads<<",\"baseline_writes\":"<<result.writes<<",\"baseline_polls\":"<<result.polls<<",\"baseline_operations\":"<<result.operations<<"}\n";
}
