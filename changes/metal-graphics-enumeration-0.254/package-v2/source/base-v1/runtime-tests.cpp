#include "ReusableRuntime.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
namespace R=RtxReusableRuntime035;namespace N=RtxReusable035;namespace B=RtxReusableBacking035;
static unsigned checks=0,scenarios=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static std::vector<uint8_t> load(const std::string &s){std::ifstream f(s,std::ios::binary);CHECK(bool(f));return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),{});}
static void save(const std::string &s,const void *p,size_t n){std::ofstream f(s,std::ios::binary);CHECK(bool(f));f.write(static_cast<const char*>(p),std::streamsize(n));CHECK(bool(f));}
struct Fixtures {
 std::vector<uint8_t> root,children,device,library,code;N::P::Library lib;
 explicit Fixtures(const std::string &p):root(load(p+"/bootstrap-root.bin")),children(load(p+"/bootstrap-children.bin")),device(load(p+"/bootstrap-device.bin")),library(load(p+"/library.bin")),code(load(p+"/code.bin")){
  CHECK(root.size()==12288&&children.size()==40960&&device.size()==36864&&library.size()==512&&code.size()==4096);
  children.resize(B::MaxChildren);CHECK(N::P::decode(library.data(),512,code.data(),4096,lib));CHECK(B::image(library.data(),code.data(),device.data()+12288,24576));
 }
};
static N::Request request(uint64_t serial){
 N::Request r;r.generation=0x30603501;r.serial=serial;r.program=unsigned((serial-1)%3);r.groups=1;uint32_t seed=uint32_t(serial)^0x35bacc;
 for(unsigned i=0;i<128;++i){seed=seed*1664525+1013904223;N::P::Q::put32(r.data+i*4,seed);}
 for(unsigned i=0;i<64;++i)N::P::Q::put32(r.data+512+i*4,0xcafe0000+i);return r;
}
struct Backend {
 Fixtures &f;R::State state;std::vector<uint8_t> root,children,device,captureRoot,captureChildren,captureDevice;
 uint64_t time=1000000;unsigned ops=0,clocks=0,failOp=0,loseOp=0,failClock=0,reads=0,writes=0,notifications=0,windowWrites=0,retains=0;
 bool owned=true,runtime=false,restored=false,expire=false,wrongOutput=false;unsigned window=0;
 explicit Backend(Fixtures &x):f(x),root(x.root),children(x.children),device(x.device),captureRoot(12288),captureChildren(B::MaxChildren),captureDevice(B::DeviceBytes){}
 R::Storage storage(){return {f.root.data(),f.children.data(),f.library.data(),f.code.data(),captureRoot.data(),captureChildren.data(),captureDevice.data(),40960};}
 bool op(){++ops;if(ops==loseOp)owned=false;return ops!=failOp&&owned;}
 uint64_t nowNs(){++clocks;if(clocks==failClock)return expire?time+N::BudgetNs:0;return ++time;}
 bool bootReady(uint64_t gen,uint64_t client,bool restore){return op()&&gen==0x30603501&&client==17&&!runtime&&restored==restore;}
 bool runtimeReady(uint64_t gen,uint64_t client){return op()&&gen==0x30603501&&client==17&&runtime&&restored;}
 unsigned originalWindow()const{return 0x12345678;}
 bool beginRuntime(uint64_t gen){if(!op()||gen!=0x30603501||!restored||runtime)return false;runtime=true;return true;}
 bool readWindow(unsigned &v){CHECK(owned);if(!op())return false;v=window;return true;}
 bool writeWindow(unsigned v){CHECK(owned&&runtime&&(v==0||v==originalWindow()));++windowWrites;const bool ok=op();window=v;return ok;}
 uint8_t *address(unsigned a,unsigned n){
  if(R::span(a,n,R::RootPhysical,12288))return root.data()+a-R::RootPhysical;
  if(R::span(a,n,R::ChildrenPhysical,40960))return children.data()+a-R::ChildrenPhysical;
  for(unsigned i=0;i<9;++i)if(R::span(a,n,R::Pages[i],4096))return device.data()+i*4096+a-R::Pages[i];
  return nullptr;
 }
 bool readMemory(unsigned a,uint8_t *out,unsigned n){CHECK(owned&&window==0);++reads;auto *p=address(a,n);CHECK(p);const bool ok=op();std::memcpy(out,p,n);return ok;}
 bool writeMemory(unsigned a,const uint8_t *data,unsigned n){
  CHECK(owned&&runtime&&window==0&&state.window.acquired&&!state.window.restoreAttempted&&state.core.phase()==N::Phase::Exposed);
  CHECK(state.backing.phase()==B::Phase::Claimed&&state.backing.writes()>=1&&state.backing.writes()<=7);
  ++writes;auto *p=address(a,n);CHECK(p);const bool ok=op();std::memcpy(p,data,n);return ok;
 }
 bool notify(){
  CHECK(owned&&runtime&&window==0&&state.backing.phase()==B::Phase::Published&&state.backing.writes()==7);++notifications;const bool ok=op();
  const auto &r=state.core.request();const auto &p=state.core.plan();
  for(unsigned i=0;i<64;++i){const auto a=N::P::get32(device.data()+B::Data+i*4),b=N::P::get32(device.data()+B::Data+256+i*4);
   const auto v=r.program==0?a+b:r.program==1?a*b:a^b;N::P::Q::put32(device.data()+B::Data+512+i*4,v);}
  if(wrongOutput)device[B::Data+512]^=1;
  N::P::Q::put64(device.data()+B::Fence,r.serial);N::P::Q::put64(device.data()+B::Fence+16,r.serial);
  N::P::Q::put32(device.data()+0x888,p.put);N::P::Q::put32(device.data()+0x840,0x20001078);N::P::Q::put32(device.data()+0x844,0x20001078);
  N::P::Q::put32(device.data()+B::Qmd,uint32_t(r.serial));return ok;
 }
 void delayUs(unsigned us){time+=uint64_t(us)*1000;}
 void retain(){++retains;owned=false;}
 void bootstrapRestore(){CHECK(owned&&!runtime);restored=true;window=originalWindow();}
 bool start(){R::Runtime<Backend> r(*this,state);auto m=storage();if(!r.prepare(0x30603501,17,m))return false;bootstrapRestore();return r.open();}
 N::Failure run(uint64_t serial){R::Runtime<Backend> runtimeIO(*this,state);auto r=request(serial);std::array<uint8_t,N::WireBytes> wire{};CHECK(N::encode(r,f.lib,wire.data(),wire.size()));return runtimeIO.submit(17,wire.data(),wire.size());}
 void counters(){ops=clocks=reads=writes=notifications=windowWrites=0;}
};
int main(int argc,char **argv){
 CHECK(argc==3);Fixtures f(argv[1]);const std::string out=argv[2];auto io=std::make_unique<Backend>(f);
 CHECK(io->start());const unsigned startupOps=io->ops;save(out+"/initial-device.bin",io->captureDevice.data(),36864);
 unsigned ops=0,clocks=0,readCalls=0;
 for(uint64_t serial=1;serial<=256;++serial){
  io->counters();CHECK(io->run(serial)==N::Failure::None);CHECK(io->state.core.completed()==serial&&io->state.backing.completed()==serial&&!io->state.closed);
  CHECK(io->writes==7&&io->notifications==1&&io->windowWrites==2&&io->window==io->originalWindow());
  for(const auto *capture:{&io->state.before,&io->state.staged,&io->state.completed})CHECK(capture->passed&&capture->reads==22&&capture->bytes==90112);
  if(serial==1){ops=io->ops;clocks=io->clocks;readCalls=io->reads;}
  CHECK(io->ops==ops&&io->clocks==clocks&&io->reads==readCalls);
  auto r=request(serial);for(unsigned i=0;i<64;++i){const auto a=N::P::get32(r.data+i*4),b=N::P::get32(r.data+256+i*4),want=r.program==0?a+b:r.program==1?a*b:a^b;
   CHECK(N::P::get32(io->captureDevice.data()+B::Data+512+i*4)==want);}
  if(serial==1||serial==4||serial==31||serial==32||serial==33||serial==64||serial==65||serial==256){
   std::array<uint8_t,N::WireBytes> wire{};CHECK(N::encode(r,f.lib,wire.data(),wire.size()));const auto prefix=out+"/job-"+std::to_string(serial);
   save(prefix+"-request.bin",wire.data(),wire.size());save(prefix+"-device.bin",io->captureDevice.data(),36864);
   save(prefix+"-root.bin",io->captureRoot.data(),12288);save(prefix+"-children.bin",io->captureChildren.data(),40960);
  }
 }
 ++scenarios;
 // Malformed/foreign requests do not erase the last capture or touch hardware.
 {R::Runtime<Backend> runtime(*io,io->state);auto r=request(257);std::array<uint8_t,N::WireBytes> wire{};CHECK(N::encode(r,f.lib,wire.data(),wire.size()));const auto calls=io->ops;
  CHECK(runtime.submit(18,wire.data(),wire.size())==N::Failure::Identity);CHECK(runtime.submit(17,wire.data(),wire.size()-1)==N::Failure::Shape);
  CHECK(runtime.submit(17,io->state.hostRing,N::WireBytes)==N::Failure::Shape);wire[24]^=1;CHECK(runtime.submit(17,wire.data(),wire.size())==N::Failure::Order);
  CHECK(io->ops==calls&&!io->state.closed&&io->state.core.completed()==256);CHECK(!runtime.close(18));CHECK(runtime.close(17)&&io->retains==1);CHECK(!runtime.close(17));++scenarios;
 }
 for(unsigned warm:{0u,31u,32u}){
  for(unsigned mode=0;mode<2;++mode)for(unsigned fault=1;fault<=ops;++fault){
   io=std::make_unique<Backend>(f);CHECK(io->start());for(unsigned j=1;j<=warm;++j)CHECK(io->run(j)==N::Failure::None);io->counters();
   if(mode)io->loseOp=fault;else io->failOp=fault;
   CHECK(io->run(warm+1)!=N::Failure::None);CHECK(io->state.closed&&io->state.backing.phase()==B::Phase::Retained&&io->retains==1);
   CHECK(io->state.backing.completed()==warm&&io->state.core.phase()==N::Phase::Retained);const unsigned count=io->ops;
   CHECK(io->run(warm+1)==N::Failure::State&&io->ops==count);CHECK(io->writes<=7&&io->notifications<=1&&io->windowWrites<=2);++scenarios;
  }
  for(unsigned mode=0;mode<2;++mode)for(unsigned fault=2;fault<=clocks;++fault){
   io=std::make_unique<Backend>(f);CHECK(io->start());for(unsigned j=1;j<=warm;++j)CHECK(io->run(j)==N::Failure::None);io->counters();io->failClock=fault;io->expire=bool(mode);
   const auto status=io->run(warm+1);
   if(status==N::Failure::None||!io->state.closed||io->state.backing.completed()!=warm)std::fprintf(stderr,"clock warm=%u mode=%u fault=%u status=%u closed=%u completed=%llu\n",warm,mode,fault,unsigned(status),unsigned(io->state.closed),static_cast<unsigned long long>(io->state.backing.completed()));
   CHECK(status!=N::Failure::None&&io->state.closed&&io->state.backing.completed()==warm);++scenarios;
  }
 }
 // Bootstrap failures leave window cleanup with the startup caller. Open
 // failures happen after that restoration and retain the native owner.
 for(unsigned fault=1;fault<=startupOps;++fault){io=std::make_unique<Backend>(f);io->failOp=fault;CHECK(!io->start());CHECK(io->state.closed&&io->writes==0&&io->notifications==0);++scenarios;}
 for(unsigned offset:{0u,8u,255u,0x840u,0x848u,0x888u,4096u,4160u,8192u,B::Image,B::Data+512,B::Qmd,B::Fence+8,B::Fence+16,36863u}){
  io=std::make_unique<Backend>(f);CHECK(io->start());CHECK(io->run(1)==N::Failure::None);io->counters();io->device[offset]^=1;
  CHECK(io->run(2)!=N::Failure::None&&io->state.closed&&io->writes==0&&io->notifications==0);++scenarios;
 }
 io=std::make_unique<Backend>(f);CHECK(io->start());io->wrongOutput=true;CHECK(io->run(1)==N::Failure::None);
 save(out+"/wrong-arithmetic-allowed-location.bin",io->captureDevice.data(),36864);++scenarios;
 io=std::make_unique<Backend>(f);{R::Runtime<Backend> runtime(*io,io->state);auto m=io->storage();m.captureRoot=const_cast<uint8_t*>(m.root);
  CHECK(!runtime.prepare(0x30603501,17,m)&&io->ops==0);CHECK(!runtime.open());++scenarios;}
 std::printf("{\"passed\":true,\"checks\":%u,\"scenarios\":%u,\"jobs\":256,\"ring_wraps\":8,\"fault_start_serials\":[1,32,33],\"io_fault_positions\":%u,\"clock_positions\":%u,\"startup_fault_positions\":%u,\"reads_per_job\":%u,\"state_bytes\":%zu,\"cpu_simulated\":true,\"gpu_commands_submitted\":false,\"metal_verified\":false}\n",checks,scenarios,ops,clocks,startupOps,readCalls,sizeof(R::State));
}
