#include "cpu-dispatch-fixture185.hpp"
#include "cpu-access-fixture182.hpp"
#include "owner/NativeOwnedDispatch205.hpp"
namespace C=RTXNativeDispatch205;namespace R=RTXNativeRoot196;
struct Sink185 {
 std::map<std::string,Bytes>files;unsigned calls=0,failAt=0;
 bool save(const char*name,const uint8_t*p,size_t n){if(++calls==failAt)return false;check(!files.count(name));files[name]=Bytes(p,p+n);return true;}
 void dump(const std::filesystem::path&p){check(std::filesystem::create_directory(p));for(const auto&v:files)write(p/v.first,v.second.data(),v.second.size());}
};
struct Wire185 {
 Fake&f;IO dataIO;D::State data;unsigned count=0,failAt=0,corruptAt=0,part=999,timeoutAt=99999;bool postFailure=false,regress=false,timeout=false;
 explicit Wire185(Fake&x):f(x),dataIO(*x.root){check(data.activate(0x179));}
 uint64_t nowNs(){if(regress)return 0;if(timeout&&count>=timeoutAt)return f.clock+C::BudgetNs;return ++f.clock;}
 bool call(unsigned sel,const uint64_t*sc,unsigned n,const uint8_t*in,size_t bytes,uint8_t*out,size_t cap){
  ++count;bool pass=true;
  if(count==failAt&&!postFailure)return false;
  if(sel>=101){const GA::Call call{sc,n,in,bytes,out,cap};pass=f.call(sel,call)==G::Error::None;}
  else if(sel==97){check(n==0&&!bytes&&cap==64);uint64_t info[8]{};data.info(0x179,info);std::memcpy(out,info,64);}
  else{const ABI::Call call{sc,n,in,bytes,out,cap};pass=ABI::dispatch(data,f.root->owner,dataIO,sel,call)==D::Error::None;}
  if((count==corruptAt||(sel==106&&n==4&&sc[1]==part&&sc[2]==0))&&cap)out[0]^=1;
  if(count==failAt)return false;return pass;
 }
};
static R::Capture rootCapture(const std::filesystem::path&folder){
 R::Capture c;const auto before=read(folder/"owned-root-info-before.bin"),after=read(folder/"owned-root-info-after.bin");
 std::memcpy(c.before.data(),before.data(),512);std::memcpy(c.after.data(),after.data(),512);check(R::decode(c.before.data(),512,0x179,c.info));
 c.handles=read(folder/"owned-root-handles.bin");c.rows=read(folder/"owned-root-rows.bin");c.passed=true;return c;
}
struct Bootstrap185 {Fake&f;
 bool copyPristineDevice(std::array<uint8_t,36864>&out)const{
  if(!f.legacy->replacementBefore(0,{f.rootCopy.data(),f.childCopy.data(),f.memory.data(),40960}))return false;
  std::copy(f.memory.begin(),f.memory.end(),out.begin());return true;
 }
};
struct Test185 {
 Fake f;Wire185 io;RTXNativeData182::Session data;C::Session client;Bootstrap185 bootstrap;
 explicit Test185(Input&input,const R::Capture&root):f(input),io(f),bootstrap{f}{Sink185 sink;check(data.collect(io,sink,root));io.count=0;}
 bool begin(const R::Capture&root,const Bytes&image,Sink185&sink){return client.begin(io,sink,root,data,bootstrap,image.data()+640,4608);}
};
static std::array<C::Binding,3>bindings(){return {{{1,0,4,65532},{2,1,4092,131076},{3,2,65532,262148}}};}
static RTXGeometry164::Size local(const Bytes&image){const auto*p=image.data()+640+64;return {G::P::get32(p+16),G::P::get32(p+20),G::P::get32(p+24)};}
int main(int argc,char**argv){
 check(argc==3);const std::filesystem::path source=argv[1],out=argv[2];check(std::filesystem::create_directory(out));Input input(source);auto root=rootCapture(source/"root-captures");auto b=bindings();Bytes first;unsigned jobs=0;
 for(const auto&name:{"1x1x1","1x1x64","16x8x1","32x32x1","64x1x1","8x4x4"}){
  auto image=read(source/"programs"/name/"compiled.rtxlib");if(first.empty())first=image;auto t=std::make_unique<Test185>(input,root);Sink185 begin;
  check(t->begin(root,image,begin)&&t->client.ready()&&t->client.operationCalls()==12);begin.dump(out/(std::string(name)+"-begin"));
  unsigned count=std::string(name)=="1x1x1"?40:3;
  for(unsigned serial=1;serial<=count;++serial){Sink185 job;uint64_t completed=999;
   check(t->client.submit(t->io,job,0,b.data(),b.size(),{2,1,1},local(image),completed));
   check(completed==serial&&t->client.completed()==serial&&t->f.state->completed()==serial&&t->client.ready());t->f.references(0);++jobs;job.dump(out/(std::string(name)+"-"+std::to_string(serial)));
  }
 }
 // Protocol failures before and after an effective call must never replay it.
 for(unsigned cut=1;cut<=12;++cut)for(bool post:{false,true}){
  auto t=std::make_unique<Test185>(input,root);t->io.failAt=cut;t->io.postFailure=post;Sink185 sink;
  check(!t->begin(root,first,sink)&&t->client.failed());const auto calls=t->io.count;check(!t->begin(root,first,sink)&&t->io.count==calls);++rejections;
 }
 for(unsigned cut:{1U,8U,9U,10U,11U,12U}){
  auto t=std::make_unique<Test185>(input,root);t->io.corruptAt=cut;Sink185 sink;check(!t->begin(root,first,sink)&&t->client.failed());++rejections;
 }
 for(unsigned cut:{1U,2U,3U,4U,5U,6U,9U,12U,20U,29U,36U,39U,40U,41U})for(bool post:{false,true}){
  auto t=std::make_unique<Test185>(input,root);Sink185 start;check(t->begin(root,first,start));t->io.failAt=t->io.count+cut;t->io.postFailure=post;Sink185 job;uint64_t completed=999;
  check(!t->client.submit(t->io,job,0,b.data(),b.size(),{2,1,1},local(first),completed)&&!completed&&t->client.failed());
  const auto calls=t->io.count;check(!t->client.submit(t->io,job,0,b.data(),b.size(),{2,1,1},local(first),completed)&&t->io.count==calls);++rejections;
 }
 for(unsigned part=0;part<11;++part){
  auto t=std::make_unique<Test185>(input,root);Sink185 start;check(t->begin(root,first,start));t->io.part=part;Sink185 job;uint64_t completed=999;
  check(!t->client.submit(t->io,job,0,b.data(),b.size(),{2,1,1},local(first),completed)&&!completed&&t->client.failed());++rejections;
 }
 for(unsigned cut:{1U,2U,3U,4U,10U,15U}){
  auto t=std::make_unique<Test185>(input,root);Sink185 start;check(t->begin(root,first,start));Sink185 job;job.failAt=cut;uint64_t completed=999;
  check(!t->client.submit(t->io,job,0,b.data(),b.size(),{2,1,1},local(first),completed)&&!completed&&t->client.failed());++rejections;
 }
 for(unsigned kind=0;kind<7;++kind){
  auto t=std::make_unique<Test185>(input,root);Sink185 start;check(t->begin(root,first,start));auto bad=b;auto threads=local(first);auto groups=RTXGeometry164::Size{2,1,1};
  if(kind==0)bad[0].slot=4;if(kind==1)bad[0].bytes=UINT64_MAX;if(kind==2)bad[0].offset=3;if(kind==3)bad[1].index=0;if(kind==4)groups.x=0;if(kind==5)threads.z=65;
  Sink185 job;uint64_t completed=999;auto calls=t->io.count;check(!t->client.submit(t->io,job,kind==6?99:0,bad.data(),bad.size(),groups,threads,completed)&&!completed&&t->client.ready()&&t->io.count==calls&&job.files.empty());++rejections;
 }
 for(bool timeout:{false,true}){
  auto t=std::make_unique<Test185>(input,root);Sink185 start;check(t->begin(root,first,start));t->io.regress=!timeout;t->io.timeout=timeout;t->io.timeoutAt=t->io.count+1;Sink185 job;uint64_t completed=999;
  check(!t->client.submit(t->io,job,0,b.data(),b.size(),{2,1,1},local(first),completed)&&t->client.failed()&&!completed);++rejections;
 }
 std::ofstream report(out/"result.json");report<<"{\"passed\":true,\"checks\":"<<checks<<",\"rejections\":"<<rejections<<",\"synthetic_jobs\":"<<jobs<<",\"programs\":6,\"synthetic_only\":true,\"actual_native_io\":false,\"gpu_executed\":false}\n";check(bool(report));
 std::cout<<"checks="<<checks<<" rejections="<<rejections<<" jobs="<<jobs<<'\n';
}
