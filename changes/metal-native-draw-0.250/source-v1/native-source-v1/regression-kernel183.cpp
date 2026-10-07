#include "cpu-root-fixture182.hpp"
#include "probe/kernel/root181/MappedDataWindow181.hpp"
#include "probe/kernel/gpu183/OwnedDispatchABI183.hpp"
#include "probe/kernel/SelectorRouting171.hpp"
#include <map>
static void checked183(bool value,unsigned line){if(!value)std::cerr<<"test line "<<line<<'\n';check(value);}
#define check(value) checked183(bool(value),__LINE__)
namespace G=RTXOwnedDispatch183;namespace GA=RTXOwnedDispatchABI183;namespace RB=RtxReusableBacking035;namespace RN=RtxReusable035;
using Bytes=std::vector<uint8_t>;
struct Input {
 Bytes root,children,device,library,code;
 Input(const std::filesystem::path&s):root(read(s/"fixtures/program-root.bin")),children(read(s/"fixtures/program-children.bin")),device(read(s/"probe/kernel/bootstrap-device.bin")),library(read(s/"probe/kernel/library.bin")),code(read(s/"probe/kernel/code.bin")){
  check(device.size()==RB::DeviceBytes&&library.size()==512&&code.size()==4096);check(RB::image(library.data(),code.data(),device.data()+RB::Image,RB::ImageBytes));children.resize(RB::MaxChildren);
 }
};
struct Fake {
 Input&input;std::unique_ptr<Trial>root;std::unique_ptr<G::State>state=std::make_unique<G::State>();std::unique_ptr<RB::Ledger>legacy=std::make_unique<RB::Ledger>();
 Bytes memory,rootCopy,childCopy,ring;uint64_t clock=1000;bool selected=false,hold=false;
 std::string failure;unsigned failAt=1,reads=0,writes=0,bells=0,imports=0,clockAt=0,clockCalls=0,badCompletion=0;bool expire=false;
 std::map<std::string,unsigned>calls;
 explicit Fake(Input&i):input(i),root(std::make_unique<Trial>(i.root,Bytes(i.children.begin(),i.children.begin()+40960))),memory(i.device),rootCopy(i.root),childCopy(i.children),ring(i.device.begin(),i.device.begin()+4096){
  check(root->commit());RB::Inputs src{i.root.data(),i.children.data(),ring.data(),i.library.data(),i.code.data(),40960};
  check(legacy->seed(0x179,src,{rootCopy.data(),childCopy.data(),memory.data(),40960}));
 }
 bool go(const char*n){const auto count=++calls[n];return failure!=n||count!=failAt;}
 bool ready(){return go("ready")&&root->binding.ready();}
 uint64_t nowNs(){++clockCalls;if(clockAt&&clockCalls==clockAt)return expire?clock+G::BudgetNs:0;return ++clock;}
 void delayUs(unsigned us){clock+=uint64_t(us)*1000;}
 bool mapping(unsigned i,uint64_t&s,RTXSpans165::Mapping&m){return go("mapping")&&root->arena.preparedSpan(i,s,m);}
 bool stable(unsigned i,uint64_t off,uint64_t n){return go("stable")&&RTXDataWindow181::stable(root->arena.life,root->arena.io,i,off,n);}
 bool import(unsigned){++imports;return go("import");}
 bool selectWindow(){selected=true;return go("select");}
 bool restoreWindow(){selected=false;return go("restore");}
 uint8_t*address(unsigned a,unsigned n){for(unsigned i=0;i<9;++i)if(RtxReusableRuntime035::span(a,n,RtxReusableRuntime035::Pages[i],4096))return memory.data()+4096*i+a-RtxReusableRuntime035::Pages[i];return nullptr;}
 bool read(unsigned a,uint8_t*out,unsigned n){check(selected);auto*p=address(a,n);check(p!=nullptr);++reads;const bool ok=go("read");G::copy(out,p,n);return ok;}
 bool write(unsigned a,const uint8_t*p,unsigned n){check(selected&&state->allowedWrite(a,p,n));auto*out=address(a,n);check(out!=nullptr);++writes;const bool ok=go("write");G::copy(out,p,n);return ok;}
 bool rootStable(){return go("root")&&root->arena.exposedStable();}
 bool baseline(const uint8_t*p){return go("baseline")&&legacy->replacementBefore(0,{rootCopy.data(),childCopy.data(),p,40960});}
 bool notify(){
  check(selected&&state->mayNotify());++bells;const bool ok=go("notify");if(hold)return ok;
  uint64_t info[16];state->info(0x179,info);const uint64_t serial=info[5]+1;
  G::Q::put64(memory.data()+RB::Fence,badCompletion==1?serial-1:serial);
  G::Q::put64(memory.data()+RB::Fence+16,badCompletion==2?serial+1:serial);
  const unsigned put=RN::nextIndex(serial);G::Q::put32(memory.data()+0x888,badCompletion==3?(put+3)&31:put);
  if(badCompletion==4)G::Q::put32(memory.data()+0x88c,(put+2)&31);
  G::Q::put32(memory.data()+0x840,0x20001078);G::Q::put32(memory.data()+0x844,0x20001078);
  G::Q::put32(memory.data()+RB::Qmd,uint32_t(serial));
  if(badCompletion==5)memory[RB::Data+1]^=1;
  if(badCompletion==6)root->arena.io.data[1].pages[0]^=4096;
  return ok;
 }
 G::Error call(unsigned sel,const GA::Call&c){return GA::dispatch(*state,root->owner,*this,0x179,sel,c);}
 void program(const Bytes&container){
  check(container.size()==5248);const uint64_t session=0x179;
  check(call(GA::Begin,{&session,1,container.data()+640,512,nullptr,0})==G::Error::None);
  const uint64_t chunk[]={session,0,4096};check(call(GA::Upload,{chunk,3,container.data()+1152,4096,nullptr,0})==G::Error::None);
  check(call(GA::Seal,{&session,1,nullptr,0,nullptr,0})==G::Error::None);
 }
 Bytes request(const Bytes&container,uint64_t serial){
  const auto*p=container.data()+640+64;Bytes wire(G::RequestBytes);G::Q::put64(wire.data(),G::Magic);G::Q::put32(wire.data()+8,183);G::Q::put32(wire.data()+12,G::RequestBytes);
  G::Q::put64(wire.data()+16,0x179);G::Q::put64(wire.data()+24,serial);G::Q::put32(wire.data()+36,3);
  for(unsigned i=0;i<3;++i){G::Q::put64(wire.data()+48+i*8,i==0?2:1);G::Q::put64(wire.data()+72+i*8,G::P::get32(p+16+i*4));}
  const uint64_t offsets[]={4,4092,65532},lengths[]={65532,131076,262148};
  for(unsigned i=0;i<3;++i){auto*b=wire.data()+128+i*32;G::Q::put64(b,(1ULL<<32)|(i+2));G::Q::put64(b+8,offsets[i]);G::Q::put64(b+16,lengths[i]);G::Q::put32(b+24,i);}
  return wire;
 }
 G::Error submit(const Bytes&wire){return call(GA::Submit,{nullptr,0,wire.data(),wire.size(),nullptr,0});}
 void references(unsigned expected){for(unsigned i=1;i<4;++i){RTXSpans165::Mapping m;uint32_t refs=0;check(root->owner.inspect((1ULL<<32)|(i+1),m,refs)&&refs==expected);}}
 void dump(const std::filesystem::path&out){
  check(std::filesystem::create_directory(out));uint64_t info[16];state->info(0x179,info);writeFile(out/"info.bin",info,sizeof(info));
  const char*names[]={"request.bin","qmd.bin","constants.bin","command.bin","ring.bin","before.bin","staged.bin","after.bin","observations.bin","library.bin","code.bin"};
  const size_t sizes[]={384,256,4096,56,8,RB::DeviceBytes,RB::DeviceBytes,RB::DeviceBytes,size_t(info[15])*40,512,4096};
  for(unsigned part=0;part<11;++part){Bytes bytes(sizes[part]);for(size_t off=0;off<bytes.size();off+=4096){const auto n=std::min<size_t>(4096,bytes.size()-off);check(state->captureBytes(part,off,bytes.data()+off,n));}writeFile(out/names[part],bytes.data(),bytes.size());}
 }
 static void writeFile(const std::filesystem::path&p,const void*d,size_t n){::write(p,d,n);}
};
int main(int argc,char**argv){
 if(argc!=3)return 2;const std::filesystem::path source=argv[1],out=argv[2];check(std::filesystem::create_directory(out));Input inputs(source);
 unsigned jobs=0;Bytes first;
 for(const auto&name:{"1x1x1","1x1x64","16x8x1","32x32x1","64x1x1","8x4x4"}){
  auto container=read(source/"programs"/name/"compiled.rtxlib");if(first.empty())first=container;
  auto f=std::make_unique<Fake>(inputs);f->program(container);const unsigned count=std::string(name)=="1x1x1"?40:3;
  for(unsigned serial=1;serial<=count;++serial){auto wire=f->request(container,serial);check(f->submit(wire)==G::Error::None);check(f->state->completed()==serial&&!f->state->retained()&&!f->selected);f->references(0);++jobs;f->dump(out/(std::string(name)+"-"+std::to_string(serial)));}
  // Rejected input must preserve the previous successful evidence snapshot.
  Bytes before(384),after(384);check(f->state->captureBytes(0,0,before.data(),384));auto bad=f->request(container,count+1);bad[16]^=1;
  const auto writes=f->writes;check(f->submit(bad)==G::Error::Scope&&f->writes==writes);check(f->state->captureBytes(0,0,after.data(),384)&&before==after);f->references(0);++rejections;
 }
 for(unsigned i=0;i<7;++i){auto f=std::make_unique<Fake>(inputs);f->program(first);auto wire=f->request(first,1);
  if(i==0)wire[0]^=1;if(i==1)wire[40]=1;if(i==2)G::Q::put64(wire.data()+48,0);if(i==3)G::Q::put64(wire.data()+128+8,65536);if(i==4)G::Q::put64(wire.data()+128,0);if(i==5)wire[128+32+24]=0;if(i==6)wire[128+96]=1;
  check(f->submit(wire)!=G::Error::None&&f->writes==0&&f->bells==0&&!f->state->retained());f->references(0);++rejections;
 }
 const std::pair<const char*,unsigned>faults[]={{"write",1},{"write",2},{"write",3},{"write",4},{"write",5},{"write",6},{"write",7},{"read",1},{"read",4},{"read",17},{"read",29},{"read",44},{"select",1},{"restore",1},{"notify",1},{"root",1},{"root",2},{"root",3},{"stable",1},{"stable",7},{"import",1},{"import",2},{"baseline",1}};
 for(const auto&fault:faults){auto f=std::make_unique<Fake>(inputs);f->program(first);f->failure=fault.first;f->failAt=fault.second;auto wire=f->request(first,1);
  check(f->submit(wire)!=G::Error::None&&f->state->retained()&&f->state->completed()==0);f->references(1);const auto writes=f->writes,bells=f->bells;
  check(f->submit(wire)==G::Error::State&&f->writes==writes&&f->bells==bells);++rejections;
 }
 for(unsigned bad=1;bad<=6;++bad){auto f=std::make_unique<Fake>(inputs);f->program(first);f->badCompletion=bad;check(f->submit(f->request(first,1))!=G::Error::None&&f->state->retained());f->references(1);++rejections;}
 for(unsigned kind=0;kind<3;++kind){auto f=std::make_unique<Fake>(inputs);f->program(first);if(kind==2)f->hold=true;else{f->clockAt=8;f->expire=kind==1;}
  check(f->submit(f->request(first,1))!=G::Error::None&&f->state->retained());f->references(1);++rejections;
 }
 {
  auto f=std::make_unique<Fake>(inputs);uint64_t info[16]{},scope=0x179,badScope=0x180;
  check(f->call(GA::Info,{nullptr,0,nullptr,0,info,128})==G::Error::None&&info[3]==0);
  check(f->call(GA::Info,{nullptr,0,nullptr,0,info,127})==G::Error::Shape);++rejections;
  check(f->call(GA::Begin,{&scope,1,first.data()+640,511,nullptr,0})==G::Error::Shape);++rejections;
  check(f->call(GA::Begin,{&badScope,1,first.data()+640,512,nullptr,0})==G::Error::Scope);++rejections;
  check(f->call(GA::Begin,{&scope,1,first.data()+640,512,nullptr,0})==G::Error::None);
  check(f->call(GA::Begin,{&scope,1,first.data()+640,512,nullptr,0})==G::Error::State);++rejections;
  const uint64_t wrongScope[]={badScope,0,4096},wrongOffset[]={scope,1,4096},empty[]={scope,0,0},partial[]={scope,0,2048};
  check(f->call(GA::Upload,{wrongScope,3,first.data()+1152,4096,nullptr,0})==G::Error::Scope);++rejections;
  check(f->call(GA::Upload,{wrongOffset,3,first.data()+1152,4096,nullptr,0})==G::Error::Shape);++rejections;
  check(f->call(GA::Upload,{empty,3,nullptr,0,nullptr,0})==G::Error::Shape);++rejections;
  check(f->call(GA::Upload,{partial,3,first.data()+1152,2048,nullptr,0})==G::Error::None);
  check(f->call(GA::Seal,{&scope,1,nullptr,0,nullptr,0})==G::Error::Program&&f->state->retained());++rejections;
  check(f->writes==0&&f->bells==0);f->references(0);
 }
 {
  auto f=std::make_unique<Fake>(inputs);auto bad=first;bad[640+8]=3;uint64_t scope=0x179,chunk[]={scope,0,4096};
  check(f->call(GA::Begin,{&scope,1,bad.data()+640,512,nullptr,0})==G::Error::None);
  check(f->call(GA::Upload,{chunk,3,bad.data()+1152,4096,nullptr,0})==G::Error::None);
  check(f->call(GA::Seal,{&scope,1,nullptr,0,nullptr,0})==G::Error::Program&&f->state->retained());++rejections;f->references(0);
 }
 check(RTXSelector171::route(100)==RTXSelector171::Route::Data&&RTXSelector171::route(101)==RTXSelector171::Route::Dispatch&&RTXSelector171::route(106)==RTXSelector171::Route::Dispatch&&RTXSelector171::route(109)==RTXSelector171::Route::Graphics&&RTXSelector171::route(112)==RTXSelector171::Route::Legacy);
 std::ofstream result(out/"result.json");result<<"{\"passed\":true,\"checks\":"<<checks<<",\"rejections\":"<<rejections<<",\"synthetic_jobs\":"<<jobs<<",\"programs\":6,\"synthetic_only\":true,\"actual_native_io\":false,\"gpu_executed\":false}\n";
 check(bool(result));std::cout<<"checks="<<checks<<" rejections="<<rejections<<" synthetic_jobs="<<jobs<<'\n';
}
