#include "test-fixture207.hpp"
#include "owner/OwnedBroker207.hpp"
#include "owner/OwnedBroker188.hpp"
#include "owner/OwnedCommand207.hpp"
#include "owner/ProgramAdmission207.hpp"
#include <stdexcept>
namespace O=RTXOwnedBroker207;
struct Backend207 {
 std::unique_ptr<Live187>live;Bytes initial,current;std::vector<Bytes>approved;std::filesystem::path evidence;
 unsigned claims=0,admissions=0,executions=0,retires=0,selections=0,mode=0;
 Backend207(Input&i,const R::Capture&r,const std::vector<Bytes>&images):live(std::make_unique<Live187>(i,r,images[0])),initial(images[0]),current(images[0]),approved(images){}
 bool claim(std::array<uint8_t,RTXCatalog187::Bytes>&out,uint64_t&generation,uint64_t&completed){++claims;std::copy(initial.begin(),initial.end(),out.begin());generation=0x179;completed=0;return true;}
 bool admit(const uint8_t*p,size_t n){++admissions;for(const auto&v:approved)if(n==4608&&!std::memcmp(v.data()+640,p,n))return true;return false;}
 void retire(){++retires;live->retire();}
 bool execute(const Bytes&request,const uint8_t*payload,size_t n,Bytes&out,uint64_t&completed){
  ++executions;if(mode==1)return false;Tree187 tree;RTXProgram205::Library library;
  check(n==4608&&RTXProgram205::decode(payload,512,payload+512,4096,library));
  if(std::memcmp(current.data()+640,payload,n)){
   check(tree.part("select-program207",[&](auto&sink){return live->gpu.select(live->io,sink,payload,n);}));
   std::memcpy(current.data()+640,payload,n);++selections;
  }
  B187::Plan plan;check(B187::decode(request.data(),request.size(),library.programs,0x179,live->gpu.completed()+1,plan));live->io.plan=&plan;
  bool passed=RTXOwnedCommand207::execute(live->io,tree,live->data,live->gpu,library,request.data(),request.size(),out,completed);check(passed);expected(plan,request,out);live->io.plan=nullptr;
  if(!evidence.empty())tree.dump(evidence/("job-"+std::to_string(completed)));
  if(mode==2)return false;if(mode==3)++completed;if(mode==4)throw std::runtime_error("Lost after modeled completion");if(mode==5)out[0]^=1;return true;
 }
};
struct Connection207 {O::Peer peer;O::Client client;explicit Connection207(const Bytes&image):client(image.data(),image.size(),0x179){}};
static void connect(Connection207&c,O::Core&core,Backend207&backend){O::Frame f;check(c.client.hello(f));auto reply=core.receive(c.peer,f.bytes.data(),f.size(),true,backend);Bytes out{9};uint64_t completed=99;check(c.client.accept(reply.bytes.data(),reply.size(),0,807,out,completed)&&out==Bytes{9}&&!completed);}
static B187::Plan planFor(const Bytes&image,uint64_t serial,bool alias=false){
 RTXCatalog187::Catalog c;check(RTXCatalog187::decode(image.data(),image.size(),c));
 B187::Input bindings[3]={{17,4097,4,0},{19,8193,4092,1},{alias?17ULL:23ULL,alias?4097ULL:16385ULL,alias?4ULL:12284ULL,2}};
 const auto&p=c.library.programs[0];B187::Plan plan;check(B187::plan(p,0,bindings,3,{1,1,1},{p.localX,p.localY,p.localZ},0x179,serial,plan));return plan;
}
static O::Frame frameFor(Connection207&c,const Bytes&image){auto plan=planFor(image,c.client.completed()+1);auto wire=request(plan);O::Frame frame;check(c.client.execute(wire.data(),wire.size(),image.data()+640,4608,frame));return frame;}
struct AdmissionModel {unsigned calls=0,mode=0;Bytes payload;
 static uint32_t callback(void*ctx,const void*p,size_t n){auto&s=*static_cast<AdmissionModel*>(ctx);++s.calls;if(s.mode==2)throw std::runtime_error("denied");return s.mode||n!=s.payload.size()||std::memcmp(p,s.payload.data(),n)?1:0;}
};
int main(int argc,char**argv){
 check(argc==3);const std::filesystem::path source=argv[1],out=argv[2];check(std::filesystem::create_directory(out));Input input(source);auto root=rootCapture(source/"root-captures");
 std::vector<Bytes>images;for(const char*name:{"1x1x1","1x1x64","16x8x1","32x32x1","64x1x1","8x4x4","runtime203"})images.push_back(read(source/"programs"/name/"compiled.rtxlib"));
 unsigned jobs=0;
 {
  Backend207 backend(input,root,images);backend.evidence=out/"captures";check(std::filesystem::create_directory(backend.evidence));O::Core core;Connection207 a(images[0]),b(images[0]),later(images[0]);connect(a,core,backend);connect(b,core,backend);
  auto transcripts=out/"wire";check(std::filesystem::create_directory(transcripts));
  for(unsigned i=0;i<35;++i){
   if(i==2){connect(later,core,backend);check(later.client.nativeSerial()==2);}
   auto&c=i>=2&&i%3==2?later:(i%2?b:a);auto image=images[(i/2+1)%images.size()];auto plan=planFor(image,c.client.completed()+1,i%2);auto wire=request(plan);O::Frame frame;
   check(c.client.execute(wire.data(),wire.size(),image.data()+640,4608,frame));const auto digest=O::programHash(image.data()+640,4608);
   // Caller mutations after submission cannot change pending payload or data.
   image.back()^=1;wire.back()^=1;auto reply=core.receive(c.peer,frame.bytes.data(),frame.size(),true,backend);Bytes result{9};uint64_t done=0;
   check(c.client.accept(reply.bytes.data(),reply.size(),0,807,result,done)&&done==plan.serial&&c.client.nativeSerial()==i+1);wire.back()^=1;expected(plan,wire,result);O::Identity identity;check(O::read(reply.bytes.data(),reply.size(),identity)&&identity.program==digest);
   auto dir=transcripts/std::to_string(i+1);check(std::filesystem::create_directory(dir));write(dir/"request.bin",frame.bytes.data(),frame.size());write(dir/"reply.bin",reply.bytes.data(),reply.size());write(dir/"result.bin",result.data(),result.size());++jobs;
  }
  check(core.completed()==35&&backend.claims==1&&backend.admissions==35&&backend.executions==35&&backend.selections==18&&backend.live->gpu.revision()==19);
  O::Frame closing;check(a.client.close(closing));auto reply=core.receive(a.peer,closing.bytes.data(),closing.size(),true,backend);Bytes result{9};uint64_t done=99;check(a.client.accept(reply.bytes.data(),reply.size(),0,807,result,done)&&result==Bytes{9}&&!done);core.stop(backend);check(backend.retires==1);
 }
 // An otherwise valid payload not present in the root registry is denied
 // before selection, any I/O, or native serial consumption; another peer lives.
 {
  Backend207 backend(input,root,images);backend.approved.resize(1);O::Core core;Connection207 c(images[0]),other(images[0]);connect(c,core,backend);connect(other,core,backend);auto frame=frameFor(c,images.back());const auto count=backend.live->io.count;
  auto reply=core.receive(c.peer,frame.bytes.data(),frame.size(),true,backend);O::Identity h;check(O::read(reply.bytes.data(),reply.size(),h)&&h.status==O::Status::Denied&&core.ready()&&!core.completed()&&!backend.executions&&backend.live->io.count==count);++rejections;
  auto good=frameFor(other,images[0]);reply=core.receive(other.peer,good.bytes.data(),good.size(),true,backend);check(O::read(reply.bytes.data(),reply.size(),h)&&h.status==O::Status::OK&&core.completed()==1);++jobs;
 }
 // Header/hash/payload/batch corruption cannot invoke a native operation.
 for(unsigned offset:{0U,8U,12U,16U,20U,24U,32U,40U,48U,56U,64U,95U,96U,104U,4704U,4712U,4720U,4728U,4736U,4740U,4744U,4748U,4808U}){
  Backend207 backend(input,root,images);O::Core core;Connection207 c(images[0]);connect(c,core,backend);auto frame=frameFor(c,images[1]);frame.bytes[offset]^=0x80;const auto count=backend.live->io.count;auto reply=core.receive(c.peer,frame.bytes.data(),frame.size(),true,backend);O::Identity h;
  check(O::read(reply.bytes.data(),reply.size(),h)&&h.status!=O::Status::OK&&!backend.executions&&backend.live->io.count==count&&core.ready());++rejections;
 }
 // ABI1's <=64 1D policy holds at client, core, and command layer BEFORE data.
 {
  Backend207 backend(input,root,images);O::Core core;Connection207 c(images[0]);connect(c,core,backend);const auto&image=images.back();auto plan=planFor(image,1);plan.groups.x=2;auto wire=request(plan);O::Frame frame;check(!c.client.execute(wire.data(),wire.size(),image.data()+640,4608,frame));++rejections;
  Bytes body(image.begin()+640,image.end());body.insert(body.end(),wire.begin(),wire.end());O::Identity id{O::Op::Execute,O::Status::OK,false,c.peer.session,2,0x179,0};id.program=O::programHash(body.data(),4608);frame=O::write(id,body.data(),body.size());auto reply=core.receive(c.peer,frame.bytes.data(),frame.size(),true,backend);O::Identity h;check(O::read(reply.bytes.data(),reply.size(),h)&&h.status==O::Status::BadRequest&&!backend.admissions&&!backend.executions);++rejections;
  RTXProgram205::Library lib;check(RTXProgram205::decode(image.data()+640,512,image.data()+1152,4096,lib));Tree187 tree;Bytes result{9};uint64_t done=99;const auto count=backend.live->io.count;
  check(!RTXOwnedCommand207::execute(backend.live->io,tree,backend.live->data,backend.live->gpu,lib,wire.data(),wire.size(),result,done)&&!done&&result==Bytes{9}&&tree.top.files.empty()&&tree.children.empty()&&backend.live->io.count==count);++rejections;
 }
 // Once an effective request has an uncertain outcome the whole owner fails;
 // retries or a second peer cannot repeat that operation.
 for(unsigned mode=1;mode<=5;++mode){Backend207 backend(input,root,images);O::Core core;Connection207 c(images[0]),other(images[0]);connect(c,core,backend);connect(other,core,backend);backend.mode=mode;auto frame=frameFor(c,images[1]);auto reply=core.receive(c.peer,frame.bytes.data(),frame.size(),true,backend);Bytes result{9};uint64_t done=99;
  check(!c.client.accept(reply.bytes.data(),reply.size(),0,807,result,done)&&result==Bytes{9}&&!done&&core.failed()&&backend.retires==1&&backend.executions==1);const auto count=backend.live->io.count;core.receive(c.peer,frame.bytes.data(),frame.size(),true,backend);frame=frameFor(other,images[0]);core.receive(other.peer,frame.bytes.data(),frame.size(),true,backend);check(backend.live->io.count==count&&backend.executions==1&&backend.retires==1);++rejections;}
 // A well-shaped successful reply for another program cannot be accepted.
 {Backend207 backend(input,root,images);O::Core core;Connection207 c(images[0]);connect(c,core,backend);auto frame=frameFor(c,images[1]);auto reply=core.receive(c.peer,frame.bytes.data(),frame.size(),true,backend);reply.bytes[64]^=1;Bytes result{9};uint64_t done=99;check(!c.client.accept(reply.bytes.data(),reply.size(),0,807,result,done)&&result==Bytes{9}&&!done);++rejections;}
 // Old ABI188 wire is not misinterpreted as the new payload-bearing form.
 {Backend207 backend(input,root,images);O::Core core;O::Peer peer;RTXOwnedBroker188::Identity identity;identity.sequence=1;identity.generation=0x179;auto old=RTXOwnedBroker188::write(identity);auto reply=core.receive(peer,old.bytes.data(),old.size(),true,backend);check(!backend.claims&&!backend.executions&&peer.closed);++rejections;}
 AdmissionModel model{0,0,Bytes(images[0].begin()+640,images[0].end())};RTXProgramAdmission207::Callbacks callbacks{207,32,&model,AdmissionModel::callback,0};
 for(unsigned variant=0;variant<7;++variant){RTXProgramAdmission207::Gate gate;auto bad=callbacks;int64_t pid=807;uint32_t uid=0;switch(variant){case 0:bad.abi=206;break;case 1:bad.bytes=24;break;case 2:bad.context=nullptr;break;case 3:bad.function=nullptr;break;case 4:bad.reserved=1;break;case 5:pid=0;break;case 6:uid=501;break;}check(!gate.bind(bad,pid,uid));++rejections;}
 {RTXProgramAdmission207::Gate gate;check(!gate.admit(model.payload.data(),4608,807,0)&&!model.calls);check(gate.bind(callbacks,807,0)&&!gate.bind(callbacks,807,0));check(!gate.admit(model.payload.data(),4608,808,0)&&!gate.admit(model.payload.data(),4608,807,501)&&!gate.admit(model.payload.data(),4607,807,0)&&!model.calls);check(gate.admit(model.payload.data(),4608,807,0)&&model.calls==1);model.mode=1;check(!gate.admit(model.payload.data(),4608,807,0));model.mode=2;check(!gate.admit(model.payload.data(),4608,807,0));rejections+=7;}
 std::ofstream report(out/"result.json");report<<"{\"passed\":true,\"checks\":"<<checks<<",\"rejections\":"<<rejections<<",\"recorded_jobs\":35,\"successful_jobs\":"<<jobs<<",\"programs\":7,\"recorded_selections\":18,\"native_revision\":19,\"gpu_executed\":false}\n";check(bool(report));std::cout<<"PASS checks="<<checks<<" rejections="<<rejections<<" jobs="<<jobs<<'\n';
}
