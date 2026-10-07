#include "../runtime/RuntimeSimulation.hpp"
#include "../entry/ProgramABI.hpp"
#include "../entry/SealedPrograms.hpp"
namespace ABI=RtxProgramABI033;
static void dump(const std::string &path,const ABI::U64 *words,unsigned n){
 Bytes b(n*8);for(unsigned i=0;i<n;++i)PG::Q::put64(b.data()+i*8,words[i]);save(path,b);
}
int main(int argc,char **argv){
 CHECK(argc==5);const auto gr=load(argv[1]),fifo=load(argv[2]);const std::string component=argv[3],out=argv[4];
 const auto library=load((component+"/fixtures/library.bin").c_str()),code=load((component+"/fixtures/code.bin").c_str());
 CHECK(library==Bytes(RtxProgramSealed033::Library,RtxProgramSealed033::Library+512));
 CHECK(code==Bytes(RtxProgramSealed033::Code,RtxProgramSealed033::Code+4096));
 RuntimeSim initial(gr,fifo);Bytes requests,records;const auto execution=run(initial,&requests,&records);CHECK(execution.rpc.passed);
 L::Range goldenRanges[10],executionRanges[6];CHECK(C::mappingRanges(initial.golden,goldenRanges)&&P::mappings(initial.plan,initial.golden,executionRanges));
 walk(initial,goldenRanges,10,initial.contexts.childBytes);walk(initial,executionRanges,6,initial.contexts.childBytes);
 FenceSim hostIO;const auto host=fenceRun(hostIO,initial.golden,execution,requests,records);CHECK(host.passed);
 auto sim=std::unique_ptr<ProgramRuntimeSim>(new ProgramRuntimeSim(initial,execution,host,hostIO.memory,library,code));
 ChannelABI::Owner owner;owner.generation=0x30603301;owner.phase=18;owner.pinned=owner.owned=owner.lease=true;owner.command=6;
 owner.mapped=owner.ringClaimed=owner.contextsClaimed=owner.windowObserved=1;owner.barBase=0x100000000ULL;owner.physical=owner.barBase+L::OldBase;
 ABI::U64 words[128];std::fill(words,words+128,~0ULL);ABI::info(nullptr,owner,false,words);
 CHECK(!words[3]&&!words[4]&&!words[7]&&!words[8]&&words[6]==2112&&words[20]==512&&words[21]==4096);
 dump(out+"/info-empty.bin",words,64);
 struct BootstrapIO {
  ProgramRuntimeSim &s;
  bool ready(){return s.owned&&s.bootPhase&&!s.window;}
  uint64_t nowNs(){return s.nowNs();}
  bool readMemory(unsigned a,uint8_t *p,unsigned n){std::memcpy(p,s.at(a,n),n);return true;}
 } bootstrapIO{*sim};
 Bytes bootRoot(12288),bootChildren(L::MaxChildBytes),bootDevice(PC::DeviceBytes);PC::Result bootCapture;
 CHECK(PC::capture(bootstrapIO,sim->mem.storage.liveBytes,bootRoot.data(),bootChildren.data(),bootDevice.data(),bootCapture));
 ABI::capture(bootCapture,owner,words);dump(out+"/capture-bootstrap.bin",words,64);
 save(out+"/bootstrap-root.bin",bootRoot);save(out+"/bootstrap-children.bin",Bytes(bootChildren.begin(),bootChildren.begin()+bootCapture.childBytes));save(out+"/bootstrap-device.bin",bootDevice);
 save(out+"/before-root.bin",Bytes(sim->mem.storage.root,sim->mem.storage.root+12288));
 save(out+"/before-children.bin",Bytes(sim->mem.storage.liveChildren,sim->mem.storage.liveChildren+sim->mem.storage.liveBytes));
 CHECK(sim->prepare());CHECK(sim->open());
 ABI::info(&sim->state,owner,true,words);CHECK(words[15]&&words[19]==3&&words[25]&&words[26]&&words[28]==96&&words[29]==24576&&words[33]);
 dump(out+"/info-ready.bin",words,64);
 ABI::memory(sim->mem.result,owner,18,3,words);dump(out+"/memory.bin",words,64);
 unsigned chunks=0,denials=0;
 Bytes plans[4];for(auto &p:plans)p.resize(RI::PlanBytes);
 for(unsigned j=0;j<4;++j){
  const uint8_t *data=nullptr;unsigned total=99;
  CHECK(!ABI::data(sim->state,j,0,plans[j].data(),false,data,total)&&!total);++denials;
  CHECK(!ABI::data(sim->state,j,3,plans[j].data(),false,data,total)&&!total);++denials;
  CHECK(!ABI::data(sim->state,j,4,plans[j].data(),false,data,total)&&!total);++denials;
  const auto wire=load((component+"/image-fixtures/request-"+std::to_string(j)+".bin").c_str());
  sim->counters();CHECK(sim->invoke(11,wire)==RT::Error::Ok);CHECK(sim->arithmetic(j+1));
  CHECK(RI::encodePlan(sim->access.slots[j].stage.plan,plans[j].data(),plans[j].size()));
  const Bytes expected[]={sim->capRoot[j],Bytes(sim->capChildren[j].begin(),sim->capChildren[j].begin()+sim->mem.storage.liveBytes),
   sim->capDevice[j],wire,plans[j],library,code};
  for(unsigned part=0;part<7;++part){
   CHECK(ABI::data(sim->state,j,part,plans[j].data(),true,data,total));CHECK(total==expected[part].size());
   Bytes recovered(total,0xcc);
   for(unsigned at=0;at<total;at+=4096){const unsigned n=std::min(4096u,total-at);CHECK(ABI::span(at,n,total));std::memcpy(recovered.data()+at,data+at,n);++chunks;}
   CHECK(recovered==expected[part]);
   const uint64_t invalid[][2]={{0,0},{0,4097},{total,1},{uint64_t(total)+1,1},{UINT64_MAX,1},{1,UINT64_MAX},{UINT64_MAX,UINT64_MAX}};
   for(const auto &v:invalid){CHECK(!ABI::span(v[0],v[1],total));++denials;}
  }
  CHECK(!ABI::data(sim->state,4,0,plans[j].data(),true,data,total)&&!data&&!total);++denials;
  CHECK(!ABI::data(sim->state,j,7,plans[j].data(),true,data,total)&&!data&&!total);++denials;
  const auto suffix="-"+std::to_string(j)+".bin";
  ABI::info(&sim->state,owner,true,words);CHECK(words[4]==j+1&&words[16]==j+1&&words[17]==(j==3)&&!words[18]);dump(out+"/info"+suffix,words,64);
  ABI::job(sim->state,j,words);CHECK(words[7]==1&&words[13]&&words[28]&&words[36]&&words[41]==3&&words[53]==12288&&words[55]==36864&&words[61]);dump(out+"/job"+suffix,words,128);
  ABI::submit(sim->state,j,owner,true,words);CHECK(words[4]&&words[22]==j+2&&words[23]==j+2&&words[25]==PG::completion(j)&&words[51]==j+1);dump(out+"/submit"+suffix,words,64);
  save(out+"/plan"+suffix,plans[j]);save(out+"/device"+suffix,sim->capDevice[j]);
  save(out+"/request"+suffix,wire);save(out+"/root"+suffix,sim->capRoot[j]);
  save(out+"/children"+suffix,Bytes(sim->capChildren[j].begin(),sim->capChildren[j].begin()+sim->mem.storage.liveBytes));
 }
 const unsigned ops=sim->ops;CHECK(sim->close(11));owner.phase=7;
 ABI::info(&sim->state,owner,true,words);CHECK(words[24]&&!words[15]&&words[3]==unsigned(AS::Phase::Retained));dump(out+"/info-closed.bin",words,64);
 // Retrieval after close is read-only and must preserve existing captures.
 const uint8_t *data=nullptr;unsigned total=0;CHECK(ABI::data(sim->state,0,2,plans[0].data(),true,data,total)&&total==36864);CHECK(sim->ops==ops);
 auto &c=sim->state.captures[0];const unsigned old=c.result.deviceBytes;c.result.deviceBytes=4096;c.result.passed=false;c.result.failure=PC::Read;
 CHECK(ABI::data(sim->state,0,2,plans[0].data(),true,data,total)&&total==4096);CHECK(!ABI::span(4096,1,total));++denials;
 ABI::capture(c.result,owner,words);dump(out+"/capture-partial.bin",words,64);c.result.deviceBytes=old;
 std::fill(words,words+128,~0ULL);ABI::job(sim->state,4,words);for(unsigned i=5;i<128;++i)CHECK(words[i]==0);dump(out+"/job-invalid.bin",words,128);
 CHECK(runtimeScenarios==1&&runtimeGateDenials==16);
 std::printf("{\"passed\":true,\"checks\":%u,\"requests\":4,\"programs\":3,\"chunks\":%u,\"span_rejections\":%u,\"raw_files\":44,\"cpu_simulation_only\":true,\"gpu_commands_submitted\":false}\n",checks,chunks,denials);
}
