#include "native-source-v1/probe/kernel/gpu241/GraphicsSubmit241.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace fs=std::filesystem;
namespace G=RTXGraphicsSubmit241;namespace R=RTXGraphicsRequest240;namespace S=RTXSpans165;
using Bytes=std::vector<uint8_t>;using Wire=std::array<uint8_t,256>;using Owner=S::Owner<64,16>;
unsigned checks=0,rejections=0;
void checked(bool b,unsigned line){++checks;if(!b)throw std::runtime_error("submit241 line "+std::to_string(line));}
#define check(x) checked(bool(x),__LINE__)
Bytes read(const fs::path&p){std::ifstream f(p,std::ios::binary);check(f);return Bytes(std::istreambuf_iterator<char>(f),{});}
void write(const fs::path&p,const void*v,size_t n){std::ofstream f(p,std::ios::binary);f.write(static_cast<const char*>(v),std::streamsize(n));check(f);}
struct Fake {
 Owner owner{0x241,4096,uint64_t(1)<<48};std::unique_ptr<G::State>state=std::make_unique<G::State>();
 S::Mapping maps[5]{};uint64_t handles[5]{},writtenEpoch[5]{},publishedEpoch[5]{};Bytes buffers[5],device,initial,expected;
 Wire wire{};uint64_t clock=1000;unsigned clockCalls=0,clockAt=0;bool expire=false,selected=false,hold=false;
 std::map<std::string,unsigned>calls;std::string fault;unsigned failAt=1,corrupt=0,dataWrites=0,controlWrites=0,bells=0;
 Fake(const Bytes&boot,const Bytes&image):device(boot),initial(boot),expected(image){
  check(device.size()==G::B::DeviceBytes&&expected.size()==24849);
  // Frozen bootstrap fixture supplies the control image, with explicit idle
  // queue and timelines for this synthetic fresh graphics owner.
  G::P::Q::put32(device.data()+0x888,0);G::P::Q::put32(device.data()+0x88c,0);
  for(unsigned n=0;n<24;++n)device[G::B::Fence+n]=0;initial=device;
  const uint64_t bases[]={0x1020004000,0x2000000000,0x2000003000,0x3000000000,0x4000000000};
  const uint64_t logical[]={4096,4097,24849,4096,8192},offsets[]={0,4,256,16,0},sizes[]={4096,48,24576,16,8192};
  for(unsigned role=0;role<5;++role){maps[role]={role+1,role+1,bases[role],logical[role],(logical[role]+4095)&~uint64_t(4095),3};
   check(owner.admitMapping(maps[role],handles[role]));buffers[role].resize(size_t(logical[role]));
   for(size_t i=0;i<buffers[role].size();++i)buffers[role][i]=uint8_t(i*29+7);
  }
  auto*p=wire.data();G::P::Q::put64(p,R::Magic);G::P::Q::put32(p+8,240);G::P::Q::put32(p+12,256);
  G::P::Q::put64(p+16,0x241);G::P::Q::put64(p+24,1);G::P::Q::put32(p+32,64);G::P::Q::put32(p+36,64);G::P::Q::put32(p+40,384);G::P::Q::put32(p+44,2);
  for(unsigned n=0;n<5;++n){auto*b=p+64+n*32;G::P::Q::put64(b,handles[n]);G::P::Q::put64(b+8,offsets[n]);G::P::Q::put64(b+16,sizes[n]);G::P::Q::put32(b+24,n);}
 }
 bool go(const char*n){return ++calls[n]!=failAt||fault!=n;}
 bool ready(){return go("ready");}
 uint64_t nowNs(){if(++clockCalls==clockAt)return expire?clock+G::BudgetNs:0;return ++clock;}
 void delayUs(unsigned us){clock+=uint64_t(us)*1000;}
 bool mapping(unsigned slot,uint64_t&scope,S::Mapping&out){check(slot<5);scope=0x241;out=maps[slot];return go("mapping");}
 bool stable(unsigned role,uint64_t off,uint64_t n){check(role<5);return go("stable")&&writtenEpoch[role]==publishedEpoch[role]&&off<=buffers[role].size()&&n<=buffers[role].size()-off;}
 bool importBuffer(unsigned role){check(role<5);return go("import")&&writtenEpoch[role]==publishedEpoch[role];}
 bool publishBuffer(unsigned role){check(role<5);publishedEpoch[role]=writtenEpoch[role];return go("publish");}
 bool readBuffer(unsigned role,uint64_t off,void*out,unsigned n){check(role<5&&off<=buffers[role].size()&&n<=buffers[role].size()-off);
  G::copy(out,buffers[role].data()+off,n);return go("readBuffer");}
 bool writeBuffer(unsigned role,uint64_t off,const void*p,unsigned n){check(state->allowsData(role,off,p,n));check(role<5&&off<=buffers[role].size()&&n<=buffers[role].size()-off);
  ++dataWrites;++writtenEpoch[role];G::copy(buffers[role].data()+off,p,n);return go("writeBuffer");}
 bool selectWindow(){selected=true;return go("select");}
 bool restoreWindow(){selected=false;return go("restore");}
 uint8_t*address(unsigned a,unsigned n){for(unsigned i=0;i<9;++i)if(RtxReusableRuntime035::span(a,n,RtxReusableRuntime035::Pages[i],4096))return device.data()+i*4096+a-RtxReusableRuntime035::Pages[i];return nullptr;}
 bool readControl(unsigned a,void*out,unsigned n){check(selected);auto*p=address(a,n);check(p);G::copy(out,p,n);return go("readControl");}
 bool writeControl(unsigned a,const void*p,unsigned n){check(selected&&state->allowsControl(a,p,n));auto*out=address(a,n);check(out);++controlWrites;G::copy(out,p,n);return go("writeControl");}
 bool rootStable(){return go("root");}
 bool baseline(const uint8_t*p){return go("baseline")&&G::equal(p,initial.data(),initial.size());}
 bool notify(){
  check(selected&&state->mayNotify());++bells;const bool ok=go("notify");if(hold)return ok;
  uint64_t info[32]{};state->info(0x241,info);const auto next=uint32_t(info[21]),token=uint32_t(info[19]);
  G::P::Q::put32(device.data()+0x888,next);const auto end=maps[4].gpuVA+4804;
  G::P::Q::put32(device.data()+0x840,uint32_t(end));G::P::Q::put32(device.data()+0x844,uint32_t(end));
  for(auto off:{0x84cu,0x860u})G::P::Q::put32(device.data()+off,(G::P::get32(device.data()+off)&~255u)|uint32_t(end>>32));
  G::P::Q::put32(buffers[3].data()+16,token);buffers[2]=expected; // CPU simulation, never a hardware claim.
  if(corrupt==1)G::P::Q::put32(buffers[3].data()+16,token+1);
  if(corrupt==2)G::P::Q::put32(device.data()+0x888,(next+2)&31);
  if(corrupt==3)G::P::Q::put32(device.data()+0x88c,(next+1)&31);
  if(corrupt==4)device[G::B::Fence]=1;
  if(corrupt==5)device[G::B::Fence+16]=1;
  if(corrupt==6)device[0x84c]^=1;
  if(corrupt==7)device[0x860+1]^=1;
  if(corrupt==8)device[0x858]^=1;
  if(corrupt==9)device[0x100]^=1;
  if(corrupt==10)buffers[2][0]^=1;
  if(corrupt==11)buffers[2][256+256]^=1;
  if(corrupt==12)buffers[2].back()^=1;
  if(corrupt==13)buffers[0][111]^=1;
  if(corrupt==14)buffers[1][12]^=1;
  if(corrupt==15)buffers[3][20]^=1;
  if(corrupt==16)buffers[4].back()^=1;
  if(corrupt==17)maps[4].gpuVA+=4096;
  return ok;
 }
 G::Error submit(unsigned serial=1){G::P::Q::put64(wire.data()+24,serial);return state->submit(*this,owner,0x241,wire.data(),wire.size());}
 void refs(unsigned wanted){for(unsigned n=0;n<5;++n){S::Mapping m{};uint32_t refs=999;check(owner.inspect(handles[n],m,refs)&&refs==wanted);}}
 void dump(const fs::path&out){check(fs::create_directory(out));uint64_t info[32]{};state->info(0x241,info);write(out/"info.bin",info,sizeof(info));
  const char*names[]={"request.bin","commands.bin","program.bin","vertices.bin","before.bin","staged.bin","after.bin","journal.bin","hashes.bin"};
  const size_t sizes[]={256,8192,4096,48,G::B::DeviceBytes,G::B::DeviceBytes,G::B::DeviceBytes,size_t(info[11])*G::RecordBytes,128};
  for(unsigned p=0;p<9;++p){Bytes data(sizes[p]);for(size_t off=0;off<data.size();off+=4096){auto n=data.size()-off;if(n>4096)n=4096;check(state->capture(p,off,data.data()+off,n));}write(out/names[p],data.data(),data.size());}
  write(out/"color.bin",buffers[2].data(),buffers[2].size());
 }
};
int main(int argc,char**argv)try{
 check(argc==4);const fs::path out(argv[3]);check(fs::create_directory(out));const auto boot=read(argv[1]),expected=read(argv[2]);
 auto good=std::make_unique<Fake>(boot,expected);
 for(unsigned serial=1;serial<=40;++serial){check(good->submit(serial)==G::Error::None);check(good->state->completed()==serial&&!good->state->retained()&&!good->selected);
  good->refs(0);check(good->dataWrites==5*serial&&good->controlWrites==2*serial&&good->bells==serial);check(good->buffers[2]==expected);
  check(!good->state->allowsData(0,0,good->buffers[0].data(),4096)&&!good->state->allowsControl(G::N::PutPhysical,good->wire.data(),4));
  if(serial==1||serial==32||serial==33||serial==40)good->dump(out/("frame-"+std::to_string(serial)));
 }
 // Bad requests preserve the last committed evidence and all free references.
 for(unsigned kind=0;kind<6;++kind){auto f=std::make_unique<Fake>(boot,expected);
  if(kind==0)f->wire[0]^=1;if(kind==1)f->wire[16]^=1;if(kind==2)f->wire[64]^=1;if(kind==3)f->wire[255]=1;if(kind==4)G::P::Q::put32(f->wire.data()+40,128);
  check(f->submit(kind==5?2:1)!=G::Error::None&&!f->state->active()&&f->dataWrites==0&&f->controlWrites==0&&f->bells==0);f->refs(0);++rejections;
 }
 const std::pair<const char*,unsigned> faults[]={{"ready",3},{"mapping",1},{"mapping",6},{"mapping",11},{"stable",1},{"stable",6},{"select",1},{"restore",1},{"root",1},{"root",2},{"root",3},{"baseline",1},{"import",1},{"import",4},{"readBuffer",1},{"readBuffer",6},{"readBuffer",20},{"readControl",1},{"readControl",8},{"readControl",18},{"readControl",35},{"writeBuffer",1},{"writeBuffer",2},{"writeBuffer",3},{"writeBuffer",4},{"writeBuffer",5},{"publish",1},{"publish",5},{"writeControl",1},{"writeControl",2},{"notify",1}};
 for(const auto&fault:faults){auto f=std::make_unique<Fake>(boot,expected);f->fault=fault.first;f->failAt=fault.second;
  check(f->submit()!=G::Error::None&&f->state->retained()&&f->state->completed()==0);f->refs(1);const auto writes=f->dataWrites+f->controlWrites,bells=f->bells;
  check(f->submit()==G::Error::State&&f->dataWrites+f->controlWrites==writes&&f->bells==bells);S::Mapping m;check(!f->owner.beginRetire(0x241,f->handles[0],m));++rejections;
 }
 for(unsigned corrupt=1;corrupt<=17;++corrupt){auto f=std::make_unique<Fake>(boot,expected);f->corrupt=corrupt;
  check(f->submit()!=G::Error::None&&f->state->retained());f->refs(1);if(corrupt>=10&&corrupt<=12)check(f->state->error()==G::Error::Guard);++rejections;
 }
 for(unsigned kind=0;kind<3;++kind){auto f=std::make_unique<Fake>(boot,expected);if(kind==2)f->hold=true;else{f->clockAt=8;f->expire=kind==1;}
  check(f->submit()!=G::Error::None&&f->state->retained());f->refs(1);++rejections;
 }
 {auto f=std::make_unique<Fake>(boot,expected);check(f->submit()==G::Error::None);f->device[0x100]^=1;
  check(f->submit(2)!=G::Error::None&&f->state->retained()&&f->bells==1);f->refs(1);++rejections;}
 {auto f=std::make_unique<Fake>(boot,expected);check(f->submit()==G::Error::None);uint64_t before[32]{},after[32]{};f->state->info(0x241,before);
  S::Mapping m;check(f->owner.beginRetire(0x241,f->handles[4],m));check(f->owner.retireVerified(f->handles[4],m.allocation,m.mapping));
  m.gpuVA=uint64_t(1)<<41;check(f->owner.admitMapping(m,f->handles[4]));G::P::Q::put64(f->wire.data()+64+4*32,f->handles[4]);
  check(f->submit(2)==G::Error::Mapping&&!f->state->retained()&&f->state->completed()==1);f->refs(0);f->state->info(0x241,after);
  check(G::equal(before,after,sizeof(before))&&f->bells==1);++rejections;}
 std::cout<<"{\"passed\":true,\"checks\":"<<checks<<",\"rejections\":"<<rejections<<",\"synthetic_frames\":40,\"state_bytes\":"<<sizeof(G::State)
  <<",\"synthetic_only\":true,\"gpu_executed\":false,\"mac_io_adapter_integrated\":false}\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
