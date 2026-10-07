#include "CaptureSimulation.hpp"
#include "VectorABI.hpp"
namespace CC=VectorCapture;
static unsigned captureScenarios=0;
struct CaptureSim {
 const SubmitSim &source;unsigned reads=0,clocks=0,failAt=0,loseAt=0,clockFault=0,timeoutAt=0;
 bool owned=true;unsigned long long time=1000;
 explicit CaptureSim(const SubmitSim &s):source(s){++captureScenarios;}
 bool ready(){return owned;}
 unsigned long long nowNs(){++clocks;if(clocks==clockFault)return 0;if(clocks==timeoutAt)time+=CC::BudgetNs;return time+=1000;}
 bool readMemory(unsigned a,unsigned char *out,unsigned n){
  CHECK(n==4096&&!(a&4095));++reads;if(reads==loseAt)owned=false;if(reads==failAt)return false;
  const Bytes *data=nullptr;unsigned base=0;
  if(a>=L::OldBase&&a-L::OldBase+n<=source.root.size()){data=&source.root;base=unsigned(L::OldBase);}
  else if(a>=L::NewBase&&a-L::NewBase+n<=source.tables.size()){data=&source.tables;base=unsigned(L::NewBase);}
  else if(a>=CM::Base&&a-CM::Base+n<=source.backing.size()){data=&source.backing;base=CM::Base;}
  else if(a>=H::Ring&&a-H::Ring+n<=source.queue.size()){data=&source.queue;base=H::Ring;}
  CHECK(data);std::memcpy(out,data->data()+a-base,n);return true;
 }
};
static CC::Result capture(CaptureSim &io,Bytes &root,Bytes &children,Bytes &device,unsigned bytes){
 CC::Result r;const auto ok=CC::capture(io,bytes,root.data(),children.data(),device.data(),r);
 CHECK(ok==r.passed&&r.reads==io.reads&&r.reads<=23);return r;
}
struct RoutingSim {
 unsigned reads=0;bool owned=true;
 bool ready(){return owned;}
 unsigned long long nowNs(){return 1234;}
 bool readMemory(unsigned,unsigned char *,unsigned){++reads;return true;}
};
int main(int argc,char **argv){
 CHECK(argc==4);const auto gr=load(argv[1]),fifo=load(argv[2]);RuntimeSim runtime(gr,fifo);Bytes requests,records;
 const auto execution=run(runtime,&requests,&records);CHECK(execution.rpc.passed);
 L::Range goldenRanges[10],executionRanges[6];CHECK(C::mappingRanges(runtime.golden,goldenRanges)&&P::mappings(runtime.plan,runtime.golden,executionRanges));
 walk(runtime,goldenRanges,10,runtime.contexts.childBytes);walk(runtime,executionRanges,6,runtime.contexts.childBytes);
 FenceSim hostIO;const auto host=fenceRun(hostIO,runtime.golden,execution,requests,records);CHECK(host.passed);
 ComputeSim memory(runtime);CHECK(memory.run(runtime.golden,execution,host));
 SubmitSim deviceIO(memory,hostIO);const auto submit=deviceIO.run(memory,execution,host);CHECK(submit.passed);
 Bytes root(CC::RootBytes),children(CC::MaxChildren),device(CC::DeviceBytes);
 CaptureSim good(deviceIO);const unsigned childBytes=memory.storage.liveBytes;const auto result=capture(good,root,children,device,childBytes);
 CHECK(result.passed&&result.rootBytes==root.size()&&result.childBytes==childBytes&&result.deviceBytes==device.size());
 CHECK(result.reads==12+childBytes/4096&&result.lastAddress==QmdProfile::FencePhysical);
 CHECK(root==deviceIO.root&&std::memcmp(children.data(),deviceIO.tables.data(),childBytes)==0);
 CHECK(std::memcmp(device.data(),deviceIO.queue.data(),4096)==0&&std::memcmp(device.data()+4096,deviceIO.queue.data()+8192,8192)==0);
 CHECK(std::memcmp(device.data()+12288,deviceIO.backing.data(),CM::Bytes)==0);
 for(unsigned n=1;n<=result.reads;++n){
  CaptureSim io(deviceIO);io.failAt=n;const auto r=capture(io,root,children,device,childBytes);
  CHECK(!r.passed&&r.failure==CC::Read&&r.reads==n&&r.rootBytes+r.childBytes+r.deviceBytes==(n-1)*4096);
  CaptureSim lost(deviceIO);lost.loseAt=n;CHECK(capture(lost,root,children,device,childBytes).failure==CC::Owner);
 }
 for(unsigned n=2;n<=result.reads+2;++n){
  CaptureSim io(deviceIO);io.clockFault=n;CHECK(capture(io,root,children,device,childBytes).failure==CC::Clock);
  CaptureSim late(deviceIO);late.timeoutAt=n;CHECK(capture(late,root,children,device,childBytes).failure==CC::Timeout);
 }
 for(unsigned bytes:{0U,4096U,8191U,8193U,CC::MaxChildren+4096,~0U}){
  CaptureSim io(deviceIO);CHECK(capture(io,root,children,device,bytes).failure==CC::Arguments&&io.reads==0);
 }
 {CaptureSim io(deviceIO);io.owned=false;CHECK(capture(io,root,children,device,childBytes).failure==CC::Owner&&!io.reads);}
 for(unsigned i=0;i<3;++i){CaptureSim io(deviceIO);CC::Result r;unsigned char *out[]={root.data(),children.data(),device.data()};out[i]=nullptr;
  CHECK(!CC::capture(io,childBytes,out[0],out[1],out[2],r)&&r.failure==CC::Arguments&&!io.reads);}
 for(unsigned i=0;i<3;++i){CaptureSim io(deviceIO);CC::Result r;unsigned char *out[]={root.data(),children.data(),device.data()};out[i]=reinterpret_cast<unsigned char *>(&r);
  r.reads=123;CHECK(!CC::capture(io,childBytes,out[0],out[1],out[2],r)&&r.reads==123&&!io.reads);}
 {CaptureSim io(deviceIO);CC::Result r;CHECK(!CC::capture(io,childBytes,root.data(),root.data()+4,device.data(),r)&&r.failure==CC::Arguments&&!io.reads);}
 for(unsigned sentinel:{0xffffffffU,0xbadf1100U,0xbad0acffU}){
  for(unsigned part=0;part<2;++part){auto broken=deviceIO;R::put32((part?broken.tables:broken.root).data()+4092,sentinel);CaptureSim io(broken);
   const auto r=capture(io,root,children,device,childBytes);CHECK(r.failure==CC::Unreadable&&r.lastValue==sentinel&&r.lastAddress==(part?L::NewBase:L::OldBase)+4092);}
  auto raw=deviceIO;R::put32(raw.backing.data()+1234,sentinel);CaptureSim io(raw);CHECK(capture(io,root,children,device,childBytes).passed);
 }
 {RoutingSim c,e;CC::Reader<RoutingSim,RoutingSim> io(c,e);CHECK(io.ready()&&io.nowNs()==1234);
  for(unsigned a:CC::Addresses)CHECK(io.readMemory(a,device.data(),4096));CHECK(c.reads==6&&e.reads==3);
  e.owned=false;CHECK(!io.ready());e.owned=true;c.owned=false;CHECK(!io.ready());}
 // CPU fixtures carry explicit simulated names. They never establish hardware access.
 CaptureSim restored(deviceIO);CHECK(capture(restored,root,children,device,childBytes).passed);children.resize(childBytes);
 save(std::string(argv[3])+"/simulated-root.bin",root);save(std::string(argv[3])+"/simulated-children.bin",children);
 save(std::string(argv[3])+"/simulated-device.bin",device);
 save(std::string(argv[3])+"/simulated-before-root.bin",memory.root);
 save(std::string(argv[3])+"/simulated-before-children.bin",Bytes(runtime.vTables.begin(),runtime.vTables.begin()+childBytes));
 ChannelABI::Owner owner;owner.generation=0x100000123ULL;owner.phase=17;owner.pinned=1;owner.owned=1;owner.command=6;owner.lease=1;owner.mapped=1;
 owner.barBase=0x824000000ULL;owner.physical=owner.barBase+0x1002000;owner.ringClaimed=owner.contextsClaimed=owner.windowObserved=owner.queueClaimed=1;
 unsigned long long words[192];static_assert(sizeof(words)==1536,"64-bit ABI");
 VectorABI::memory(memory.result,owner,18,3,words);CHECK(words[0]==VectorABI::MemoryMagic&&words[3]==2&&words[17]==0&&words[18]==24576&&words[60]==18&&words[61]==3);
 save(std::string(argv[3])+"/simulated-memory-info.bin",Bytes(reinterpret_cast<unsigned char *>(words),reinterpret_cast<unsigned char *>(words)+512));
 VectorABI::submit(submit,owner,true,true,3,true,words);CHECK(words[0]==VectorABI::SubmitMagic&&words[26]==61&&words[27]==61&&words[25]==RtxVector030::Completion&&words[63]==1);
 for(unsigned i=0;i<64;++i){CHECK(words[64+i]==submit.initialOutput[i]);CHECK(words[128+i]==submit.output[i]);}
 save(std::string(argv[3])+"/simulated-submit-info.bin",Bytes(reinterpret_cast<unsigned char *>(words),reinterpret_cast<unsigned char *>(words)+1536));
 VectorABI::capture(result,owner,words);CHECK(words[0]==VectorABI::CaptureMagic&&words[8]==device.size()&&words[32]==QmdProfile::FencePhysical);
 for(unsigned i=33;i<64;++i)CHECK(words[i]==0);
 save(std::string(argv[3])+"/simulated-capture-info.bin",Bytes(reinterpret_cast<unsigned char *>(words),reinterpret_cast<unsigned char *>(words)+512));
 std::cout<<"{\"passed\":true,\"hardware_accessed\":false,\"compute_verified\":false,\"metal_verified\":false,\"scenarios\":"<<captureScenarios
  <<",\"checks\":"<<checks<<",\"capture_reads\":"<<result.reads<<",\"root_bytes\":"<<result.rootBytes<<",\"child_bytes\":"<<result.childBytes<<",\"device_bytes\":"<<result.deviceBytes<<"}\n";
}
