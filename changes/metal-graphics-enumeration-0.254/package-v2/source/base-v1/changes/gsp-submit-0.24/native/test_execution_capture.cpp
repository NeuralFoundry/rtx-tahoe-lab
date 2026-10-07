#include "FenceSimulation.hpp"
#include "ExecutionABI.hpp"
struct CaptureIO {
  FenceSim &sim;unsigned fail=0,reads=0,mode=0;unsigned long long time=1000;
  bool ready(){return sim.owned;}
  unsigned long long nowNs(){return time+=1000;}
  bool readMemory(unsigned address,unsigned char *out,unsigned bytes){
    ++reads;CHECK(reads<=3&&bytes==4096&&address==ExecutionCapture::Addresses[reads-1]);
    if(reads==fail)return false;std::memcpy(out,sim.memory.data()+address-H::Ring,bytes);
    if(mode==1&&reads==2)time=H::BudgetNs+100000;if(mode==2&&reads==2)time=0;if(mode==3&&reads==2)sim.owned=false;return true;
  }
};
static void saveWords(const std::string &path,const unsigned long long *words,unsigned count){
  Bytes data(count*8);for(unsigned i=0;i<count;++i)L::write64(data.data()+8*i,words[i]);save(path,data);
}
int main(int argc,char **argv){
  CHECK(argc==4);RuntimeSim runtime(load(argv[1]),load(argv[2]));Bytes requests,records;
  const auto execution=run(runtime,&requests,&records);CHECK(execution.rpc.passed);
  L::Range ranges[6];CHECK(P::mappings(runtime.plan,runtime.golden,ranges));walk(runtime,ranges,6,runtime.contexts.childBytes);
  FenceSim fence;const auto result=fenceRun(fence,runtime.golden,execution,requests,records);CHECK(result.passed);
  Bytes raw(ExecutionCapture::Bytes);ExecutionCapture::Result capture;CaptureIO io{fence};
  CHECK(ExecutionCapture::capture(io,raw.data(),capture)&&capture.passed&&capture.bytes==12288&&capture.reads==3);
  CHECK(R::get32(raw.data()+0x888)==1&&R::get32(raw.data()+0x88c)==1&&R::get32(raw.data()+8192)==SubmitCodec::FenceValue);
  CHECK(std::equal(result.command,result.command+20,raw.begin()+4096));
  const std::string out=argv[3];save(out+"/device-capture.bin",raw);save(out+"/execution-requests.bin",requests);save(out+"/execution-records.bin",records);
  ChannelABI::Owner owner;owner.generation=0x12345678;owner.phase=17;owner.pinned=1;owner.owned=1;owner.command=6;owner.lease=1;owner.queueClaimed=1;
  unsigned long long words[80];ExecutionABI::rm(execution,owner,EC::Steps,words);saveWords(out+"/rm-info.bin",words,80);
  ExecutionABI::plan(execution.context,runtime.golden,owner.generation,words);saveWords(out+"/plan-info.bin",words,64);
  ExecutionABI::fence(result,owner,true,true,3,words);saveWords(out+"/fence-info.bin",words,64);
  ExecutionABI::capture(capture,owner,words);saveWords(out+"/capture-info.bin",words,32);
  for(unsigned i=1;i<=3;++i){FenceSim s;CaptureIO fault{s};fault.fail=i;ExecutionCapture::Result r;CHECK(!ExecutionCapture::capture(fault,raw.data(),r)&&r.failure==3&&r.bytes==(i-1)*4096);}
  for(unsigned mode=1;mode<=3;++mode){FenceSim s;CaptureIO fault{s};fault.mode=mode;ExecutionCapture::Result r;CHECK(!ExecutionCapture::capture(fault,raw.data(),r)&&r.bytes==8192);}
  {FenceSim s;s.owned=false;CaptureIO fault{s};ExecutionCapture::Result r;CHECK(!ExecutionCapture::capture(fault,raw.data(),r)&&r.failure==2&&r.bytes==0);}
  {FenceSim s;CaptureIO fault{s};ExecutionCapture::Result r;CHECK(!ExecutionCapture::capture(fault,nullptr,r));}
  {FenceSim s;CaptureIO fault{s};ExecutionCapture::Result r;CHECK(!ExecutionCapture::capture(fault,reinterpret_cast<unsigned char *>(&r),r));}
  std::cout<<"{\"passed\":true,\"hardware_accessed\":false,\"checks\":"<<checks<<",\"capture_scenarios\":10,\"compute_verified\":false,\"metal_verified\":false}\n";
}
