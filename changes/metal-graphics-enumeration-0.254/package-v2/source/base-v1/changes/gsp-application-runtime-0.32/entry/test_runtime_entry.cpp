#include "../runtime/RuntimeSimulation.hpp"
#include "RuntimeABI.hpp"
namespace ABI=RtxRuntimeABI032;namespace CA=ApplicationCapture;
static Bytes encoded(const unsigned long long *p,unsigned n){return Bytes(reinterpret_cast<const unsigned char*>(p),reinterpret_cast<const unsigned char*>(p)+n);}
int main(int argc,char **argv){
 CHECK(argc==4);const auto gr=load(argv[1]),fifo=load(argv[2]);RuntimeSim initial(gr,fifo);Bytes requests,records;
 const auto execution=run(initial,&requests,&records);CHECK(execution.rpc.passed);
 L::Range goldenRanges[10],executionRanges[6];CHECK(C::mappingRanges(initial.golden,goldenRanges)&&P::mappings(initial.plan,initial.golden,executionRanges));
 walk(initial,goldenRanges,10,initial.contexts.childBytes);walk(initial,executionRanges,6,initial.contexts.childBytes);
 FenceSim hostIO;const auto host=fenceRun(hostIO,initial.golden,execution,requests,records);CHECK(host.passed);
 ComputeSim memory(initial);CHECK(memory.run(initial.golden,execution,host));
 auto io=std::unique_ptr<RuntimeIO>(new RuntimeIO(memory,hostIO,execution,host));
 ChannelABI::Owner owner;owner.generation=7;owner.phase=18;owner.pinned=1;owner.owned=1;owner.command=6;owner.lease=1;owner.mapped=1;
 owner.barBase=0x824000000ULL;owner.physical=owner.barBase+0x1002000;owner.ringClaimed=owner.contextsClaimed=owner.windowObserved=owner.queueClaimed=1;
 unsigned long long words[ApplicationABI::SubmitWords];const std::string out=argv[3];
 ABI::info(&io->state,owner,true,words);CHECK(words[0]==ABI::InfoMagic&&words[3]==1&&words[4]==0&&words[15]==1);
 save(out+"/runtime-before.bin",encoded(words,512));
 ApplicationABI::memory(memory.result,owner,18,3,words);save(out+"/memory-info.bin",encoded(words,512));
 save(out+"/before-root.bin",initial.vRoot);save(out+"/before-children.bin",Bytes(initial.vTables.begin(),initial.vTables.begin()+memory.storage.liveBytes));
 const unsigned counts[]={3,17,47,64};
 for(unsigned j=0;j<4;++j){
  CHECK(io->run(j,counts[j]));const auto &r=io->state.slots[j];const auto &c=io->captured;
  CHECK(c.passed&&c.reads==12+memory.storage.liveBytes/4096&&c.rootBytes==12288&&c.deviceBytes==36864);
  for(unsigned part=0;part<3;++part){auto data=part==0?io->captureRoot:part==1?io->captureChildren:io->captureDevice;
   const unsigned offsets[]={part==2?0x888U:600U,part==2?4096U:4128U,part==2?24576U+(j+1)*256:4092U};
   for(unsigned offset:offsets){
    if(offset>=data.size()||(part==2&&j==3&&offset==25600))continue;
    data[offset]^=1;
    const bool accepted=CA::verifyFull(c,part==0?data.data():io->captureRoot.data(),part==1?data.data():io->captureChildren.data(),part==2?data.data():io->captureDevice.data(),io->storage,host,j+1);
    CHECK(!accepted);data[offset]^=1;
   }
  }
  ABI::job(io->state,j,c,words);CHECK(words[0]==ABI::JobMagic&&words[13]==1&&words[7]==counts[j]&&words[39]==17&&words[40]==2);
  CHECK(words[25]==1&&words[26]==2&&words[27]==1&&words[28]==1&&words[62]==1);
  save(out+"/job-"+std::to_string(j)+"-info.bin",encoded(words,1024));
  ApplicationABI::submit(r.submit,j,j+1,owner,true,true,3,true,words);CHECK(words[27]==counts[j]&&words[25]==A::completion(j));
  save(out+"/job-"+std::to_string(j)+"-submit.bin",encoded(words,1568));
  save(out+"/job-"+std::to_string(j)+"-request.bin",Bytes(r.stage.request,r.stage.request+A::RequestBytes));
  save(out+"/job-"+std::to_string(j)+"-plan.bin",Bytes(r.stage.plan,r.stage.plan+I::PlanBytes));
  save(out+"/job-"+std::to_string(j)+"-root.bin",io->captureRoot);
  save(out+"/job-"+std::to_string(j)+"-children.bin",Bytes(io->captureChildren.begin(),io->captureChildren.begin()+c.childBytes));
  save(out+"/job-"+std::to_string(j)+"-device.bin",io->captureDevice);
 }
 ABI::info(&io->state,owner,true,words);CHECK(words[3]==5&&words[4]==4&&words[15]==0&&words[17]==1);save(out+"/runtime-after.bin",encoded(words,512));
 {auto failed=std::unique_ptr<RuntimeIO>(new RuntimeIO(memory,hostIO,execution,host));failed->mode=5;
  CHECK(!failed->run(0,3)&&failed->captured.passed&&!failed->state.slots[0].capturePassed&&failed->window==0x42);
 }
 {auto failed=std::unique_ptr<RuntimeIO>(new RuntimeIO(memory,hostIO,execution,host));failed->failAt=762;
  CHECK(!failed->run(0,3)&&failed->captured.passed&&failed->state.slots[0].capturePassed&&failed->state.session.completed()==0);
 }
 for(unsigned total:{0U,576U,1280U,12288U,36864U,45056U})for(unsigned offset:{0U,1U,575U,4096U,total,~0U})for(unsigned n:{0U,1U,4U,4096U,4097U,~0U}){
  CHECK(ABI::span(offset,n,total)==(n&&n<=4096&&uint64_t(offset)+n<=total));
 }
 for(unsigned n=0;n<74;++n)CHECK(ABI::selector(n)==(n>=68&&n<=71));
 ABI::job(io->state,4,io->captured,words);for(unsigned i=5;i<128;++i)CHECK(words[i]==0);
 CHECK(faults==0);
 std::cout<<"{\"passed\":true,\"hardware_accessed\":false,\"metal_verified\":false,\"checks\":"<<checks<<",\"jobs\":4,\"artifacts\":33,\"capture_pages_per_job\":"<<12+memory.storage.liveBytes/4096<<"}\n";
}
