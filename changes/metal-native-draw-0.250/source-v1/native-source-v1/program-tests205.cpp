#include "test-fixture205.hpp"
#include "owner/RTXLibraryCatalog187.hpp"

static std::array<uint64_t,8>programInfo(Fake&f){
 std::array<uint64_t,8>v{};check(f.call(RTXProgram205::Info,{nullptr,0,nullptr,0,v.data(),64})==G::Error::None);return v;
}
static Bytes selectedPayload(Fake&f){Bytes v(4608);check(f.state->captureBytes(9,0,v.data(),512));check(f.state->captureBytes(10,0,v.data()+512,4096));return v;}
static G::Error selectKernel(Fake&f,const Bytes&image,uint64_t done,uint64_t revision,uint64_t scope=0x179){
 const uint64_t sc[]={scope,done,revision};std::array<uint64_t,8>reply{};
 return f.call(RTXProgram205::Select,{sc,3,image.data()+640,4608,reply.data(),64});
}
int main(int argc,char**argv){
 check(argc==3);const std::filesystem::path source=argv[1],out=argv[2];check(std::filesystem::create_directory(out));
 Input input(source);auto root=rootCapture(source/"root-captures");auto b=bindings();
 std::vector<Bytes>images;for(const char*name:{"1x1x1","1x1x64","16x8x1","32x32x1","64x1x1","8x4x4","runtime203"})images.push_back(read(source/"programs"/name/"compiled.rtxlib"));
 const auto&first=images[0];const auto&second=images[1];unsigned jobs=0,selections=0,selectionCalls=0,selectionSaves=0;
 // Decoder agrees with the application's existing ABI1/ABI2 catalog.
 for(const auto&image:images){RTXCatalog187::Catalog c;RTXProgram205::Library d;
  check(RTXCatalog187::decode(image.data(),image.size(),c));check(RTXProgram205::decode(image.data()+640,512,image.data()+1152,4096,d));
  check(c.abi==d.abi&&c.library.count==d.programs.count&&c.library.usedCodeBytes==d.programs.usedCodeBytes);
  check(std::memcmp(&c.library.programs,&d.programs.programs,sizeof(d.programs.programs))==0);
 }
 {
  auto t=std::make_unique<Test185>(input,root);Sink185 begin;check(t->begin(root,first,begin)&&t->client.revision()==1);begin.dump(out/"begin");
  // Switch before the first job, after each verified job, back to previous
  // shaders, across ring wrap, and between the two admitted payload ABIs.
  for(unsigned i=0;i<42;++i){const auto&image=images[(i+1)%images.size()];Sink185 selection;
   const auto memory=t->f.memory;const auto done=t->client.completed();const auto writes=t->f.writes,bells=t->f.bells;
   check(t->client.select(t->io,selection,image.data()+640,4608));++selections;
   check(t->client.ready()&&t->client.revision()==i+2&&t->client.completed()==done);
   check(t->f.memory==memory&&t->f.writes==writes&&t->f.bells==bells);t->f.references(0);
   selectionCalls=t->client.operationCalls();selectionSaves=selection.calls;selection.dump(out/("select-"+std::to_string(i+1)));
   Sink185 job;uint64_t completed=999;const bool legacy=G::P::get32(image.data()+8)==1;
   check(t->client.submit(t->io,job,0,b.data(),b.size(),{legacy?1ULL:2ULL,1,1},local(image),completed));
   ++jobs;check(completed==i+1&&t->client.completed()==i+1&&t->f.state->completed()==i+1);t->f.references(0);
   check(selectedPayload(t->f)==Bytes(image.begin()+640,image.end()));job.dump(out/("job-"+std::to_string(i+1)));
  }
 }
 // Malformed or stale kernel selections preserve the complete previous
 // program, completion evidence, references, and device image.
 for(unsigned bad=0;bad<12;++bad){auto f=std::make_unique<Fake>(input);f->program(first);check(f->submit(f->request(first,1))==G::Error::None);
  const auto before=programInfo(*f);const auto payload=selectedPayload(*f),memory=f->memory;Bytes evidence(384);check(f->state->captureBytes(0,0,evidence.data(),384));
  auto image=second;uint64_t sc[]={0x179,1,1};std::array<uint64_t,8>reply;reply.fill(UINT64_MAX);auto unchanged=reply;
  GA::Call call{sc,3,image.data()+640,4608,reply.data(),64};
  if(bad==0)sc[0]++;if(bad==1)sc[1]=0;if(bad==2)sc[2]=0;if(bad==3)sc[2]=2;
  if(bad==4)image[640]^=1;if(bad==5)image[640+8]=3;if(bad==6)image.back()=1;
  if(bad==7)call.inputBytes=4607;if(bad==8)call.count=2;if(bad==9)call.outputBytes=63;
  if(bad==10)call.input=nullptr;if(bad==11)call.output=nullptr;
  check(f->call(RTXProgram205::Select,call)!=G::Error::None);++rejections;
  check(programInfo(*f)==before&&selectedPayload(*f)==payload&&f->memory==memory&&reply==unchanged);
  Bytes after(384);check(f->state->captureBytes(0,0,after.data(),384)&&evidence==after);f->references(0);
  check(f->submit(f->request(first,2))==G::Error::None);
 }
 {
  auto f=std::make_unique<Fake>(input);check(selectKernel(*f,first,0,0)==G::Error::State);++rejections;
  uint64_t scope=0x179;check(f->call(GA::Begin,{&scope,1,first.data()+640,512,nullptr,0})==G::Error::None);
  check(selectKernel(*f,second,0,0)==G::Error::State);++rejections;
 }
 {
  auto f=std::make_unique<Fake>(input);f->program(first);f->failure="notify";
  check(f->submit(f->request(first,1))!=G::Error::None&&f->state->retained());const auto memory=f->memory;
  check(selectKernel(*f,second,0,1)==G::Error::State&&memory==f->memory);f->references(1);++rejections;
 }
 {
  struct During:Fake{const Bytes&next;G::Error rejected=G::Error::None;During(Input&i,const Bytes&n):Fake(i),next(n){}
   bool notify(){rejected=selectKernel(*this,next,0,1);check(rejected==G::Error::State);return Fake::notify();}
  };
  auto f=std::make_unique<During>(input,second);f->program(first);auto wire=f->request(first,1);
  check(f->state->submit(*f,f->root->owner,wire.data(),wire.size())==G::Error::None&&f->rejected==G::Error::State);++rejections;
 }
 {
  auto f=std::make_unique<Fake>(input);f->program(first);check(selectKernel(*f,second,0,1)==G::Error::None);
  check(selectKernel(*f,first,0,1)==G::Error::State);++rejections;
  check(selectKernel(*f,first,0,2)==G::Error::None&&programInfo(*f)[4]==3);
  // A stale library revision cannot be inferred from the unchanged legacy
  // dispatch-info record. The new native adapter checks the205 record too.
  auto t=std::make_unique<Test185>(input,root);Sink185 begin;check(t->begin(root,first,begin));check(selectKernel(t->f,second,0,1)==G::Error::None);
  Sink185 job;uint64_t done=99;check(!t->client.submit(t->io,job,0,b.data(),b.size(),{2,1,1},local(first),done)&&!done&&t->client.failed()&&t->f.writes==0);++rejections;
 }
 // Missing/incorrect native replies and evidence writes are uncertain even
 // if the kernel committed. The owner never retries the selection.
 for(unsigned cut=1;cut<=selectionCalls;++cut)for(bool post:{false,true}){
  auto t=std::make_unique<Test185>(input,root);Sink185 begin;check(t->begin(root,first,begin));t->io.failAt=t->io.count+cut;t->io.postFailure=post;Sink185 sink;
  check(!t->client.select(t->io,sink,second.data()+640,4608)&&t->client.failed());const auto calls=t->io.count;
  check(!t->client.select(t->io,sink,second.data()+640,4608)&&t->io.count==calls);++rejections;
 }
 for(unsigned cut=1;cut<=selectionCalls;++cut){
  auto t=std::make_unique<Test185>(input,root);Sink185 begin;check(t->begin(root,first,begin));t->io.corruptAt=t->io.count+cut;Sink185 sink;
  check(!t->client.select(t->io,sink,second.data()+640,4608)&&t->client.failed());++rejections;
 }
 for(unsigned cut=1;cut<=selectionSaves;++cut){
  auto t=std::make_unique<Test185>(input,root);Sink185 begin;check(t->begin(root,first,begin));Sink185 sink;sink.failAt=cut;
  check(!t->client.select(t->io,sink,second.data()+640,4608)&&t->client.failed());++rejections;
 }
 {
  auto t=std::make_unique<Test185>(input,root);Sink185 begin;check(t->begin(root,first,begin));auto bad=second;bad.back()=1;Sink185 sink;const auto calls=t->io.count;
  check(!t->client.select(t->io,sink,bad.data()+640,4608)&&t->client.ready()&&t->io.count==calls&&sink.files.empty());++rejections;
  Sink185 select;check(t->client.select(t->io,select,images.back().data()+640,4608));auto threads=local(images.back());
  for(auto groups:{RTXGeometry164::Size{2,1,1},{1,2,1},{1,1,2},{0,1,1},{UINT64_MAX,1,1}}){
   const auto count=t->io.count;Sink185 job;uint64_t done=99;
   check(!t->client.submit(t->io,job,0,b.data(),b.size(),groups,threads,done)&&!done&&t->client.ready()&&t->io.count==count);++rejections;
   auto wire=t->f.request(images.back(),1);for(unsigned i=0;i<3;++i)G::Q::put64(wire.data()+48+i*8,i==0?groups.x:i==1?groups.y:groups.z);
   G::Q::put64(wire.data()+80,1);G::Q::put64(wire.data()+88,1);check(t->f.submit(wire)==G::Error::Program&&t->f.writes==0);++rejections;
  }
 }
 check(RTXSelector171::route(107)==RTXSelector171::Route::Dispatch&&RTXSelector171::route(108)==RTXSelector171::Route::Dispatch&&RTXSelector171::route(109)==RTXSelector171::Route::Legacy);
 std::ofstream report(out/"result.json");report<<"{\"passed\":true,\"checks\":"<<checks<<",\"rejections\":"<<rejections<<",\"synthetic_jobs\":"<<jobs<<",\"program_selections\":"<<selections<<",\"programs\":7,\"selection_calls\":"<<selectionCalls<<",\"selection_saves\":"<<selectionSaves<<",\"gpu_executed\":false}\n";
 check(bool(report));std::cout<<"checks="<<checks<<" rejections="<<rejections<<" synthetic_jobs="<<jobs<<" program_selections="<<selections<<'\n';
}
