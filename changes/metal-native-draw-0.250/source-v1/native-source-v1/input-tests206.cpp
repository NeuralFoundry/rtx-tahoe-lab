#include "test-fixture205.hpp"
#include "probe/kernel/gpu183/ProgramInput206.hpp"
namespace I=RTXProgramInput206;
constexpr unsigned Out206=2;
struct Descriptor206 {
 Bytes bytes;unsigned direction=Out206,prepares=0,reads=0,completes=0;
 bool prepared=false,failPrepare=false,shortRead=false,failComplete=false,shrink=false;
 explicit Descriptor206(const Bytes&image):bytes(image.begin()+640,image.end()){}
 uint64_t getLength(){return bytes.size()-(shrink&&prepared?1:0);}
 unsigned getDirection(){return direction;}
 int prepare(){check(!prepared);++prepares;if(failPrepare)return -1;prepared=true;return 0;}
 uint64_t readBytes(uint64_t offset,void*out,uint64_t n){check(prepared&&offset==0&&n==4608&&n<=bytes.size());++reads;std::memcpy(out,bytes.data(),n);return n-(shortRead?1:0);}
 int complete(){check(prepared);++completes;prepared=false;return failComplete?-1:0;}
};
struct Args206 {
 unsigned version=2,asyncWakePort=0,asyncReferenceCount=0,scalarInputCount=3,structureInputSize=0,structureOutputSize=64,structureOutputDescriptorSize=0,scalarOutputCount=0;
 const void*asyncReference=nullptr;const uint64_t*scalarInput=nullptr;const void*structureInput=nullptr;
 Descriptor206*structureInputDescriptor=nullptr;void*structureOutput=nullptr;void*structureOutputDescriptor=nullptr;void*structureVariableOutputData=nullptr;
};
static G::Error invoke206(Fake&f,I::Snapshot&snapshot,Args206&a){
 if(!I::envelope(&a,RTXProgram205::Select,2))return G::Error::Shape;
 uint64_t state[8]{};f.state->programInfo205(0x179,state);
 auto e=snapshot.take(a,0x179,I::current(state,a.scalarInput,a.scalarInputCount)&&f.ready(),Out206);
 if(e==I::Error::Shape)return G::Error::Shape;if(e==I::Error::Scope)return G::Error::Scope;if(e==I::Error::State)return G::Error::State;
 if(e!=I::Error::None)return G::Error::Read;
 const GA::Call c{a.scalarInput,a.scalarInputCount,snapshot.data(),4608,a.structureOutput,a.structureOutputSize};
 return f.call(RTXProgram205::Select,c);
}
struct Bridge206 {
 Wire185&base;I::Snapshot snapshot;unsigned calls=0,descriptorCalls=0,prepares=0,reads=0,completes=0;
 explicit Bridge206(Wire185&io):base(io){}
 uint64_t nowNs(){return base.nowNs();}
 bool call(unsigned sel,const uint64_t*sc,unsigned n,const uint8_t*in,size_t bytes,uint8_t*out,size_t cap){
  ++calls;if(sel!=RTXProgram205::Select)return base.call(sel,sc,n,in,bytes,out,cap);
  check(bytes==4608);Bytes container(640+bytes);std::memcpy(container.data()+640,in,bytes);Descriptor206 descriptor(container);uint8_t emptyInband=0;
  Args206 args;args.scalarInput=sc;args.scalarInputCount=n;args.structureInput=&emptyInband;args.structureInputDescriptor=&descriptor;args.structureOutput=out;args.structureOutputSize=unsigned(cap);
  const bool okay=invoke206(base.f,snapshot,args)==G::Error::None;++descriptorCalls;prepares+=descriptor.prepares;reads+=descriptor.reads;completes+=descriptor.completes;check(!descriptor.prepared);return okay;
 }
};
int main(int argc,char**argv){
 check(argc==3);const std::filesystem::path source=argv[1],out=argv[2];check(std::filesystem::create_directory(out));
 Input input(source);auto root=rootCapture(source/"root-captures");auto bindings206=bindings();
 auto first=read(source/"programs/1x1x1/compiled.rtxlib"),second=read(source/"programs/16x8x1/compiled.rtxlib");
 unsigned scenarios=0,jobs=0;
 // Every rejected envelope/extent/scope/state reaches zero descriptor reads.
 for(unsigned bad=0;bad<24;++bad){auto f=std::make_unique<Fake>(input);f->program(first);I::Snapshot snapshot;Descriptor206 descriptor(second);
  uint64_t scalars[]={0x179,0,1};std::array<uint64_t,8>reply;reply.fill(UINT64_MAX);const auto untouched=reply;uint8_t dummy=0;
  Args206 a;a.scalarInput=scalars;a.structureInput=&dummy;a.structureInputDescriptor=&descriptor;a.structureOutput=reply.data();
  if(bad==0)a.version=1;if(bad==1)a.asyncWakePort=1;if(bad==2)a.asyncReference=&dummy;if(bad==3)a.asyncReferenceCount=1;
  if(bad==4)a.structureOutputDescriptor=&dummy;if(bad==5)a.structureOutputDescriptorSize=64;if(bad==6)a.scalarOutputCount=1;if(bad==7)a.structureVariableOutputData=&dummy;
  if(bad==8)a.scalarInputCount=2;if(bad==9)a.scalarInput=nullptr;if(bad==10)a.structureInputSize=4608;if(bad==11)a.structureInputDescriptor=nullptr;
  if(bad==12)a.structureOutputSize=63;if(bad==13)a.structureOutput=nullptr;if(bad==14)scalars[0]++;
  if(bad==15)descriptor.bytes.resize(4607);if(bad==16)descriptor.bytes.resize(4609);if(bad==17)descriptor.direction=1;if(bad==18)descriptor.direction=3;
  if(bad==19)scalars[1]=1;if(bad==20)scalars[2]=0;if(bad==21)scalars[2]=2;
  if(bad==22)a.structureInputSize=1;if(bad==23){a.structureInputSize=1;a.structureInput=nullptr;}
  const auto memory=f->memory;check(invoke206(*f,snapshot,a)!=G::Error::None);++rejections;++scenarios;
  check(!snapshot.data()&&descriptor.prepares==0&&descriptor.reads==0&&descriptor.completes==0&&reply==untouched&&f->memory==memory);
  uint64_t info[8];f->state->programInfo205(0x179,info);check(info[4]==1&&info[5]==0&&info[6]==2);
 }
 for(unsigned fault=0;fault<4;++fault){auto f=std::make_unique<Fake>(input);f->program(first);I::Snapshot snapshot;Descriptor206 descriptor(second);
  descriptor.failPrepare=fault==0;descriptor.shortRead=fault==1;descriptor.failComplete=fault==2;descriptor.shrink=fault==3;
  uint64_t sc[]={0x179,0,1},reply[8]{};Args206 a;a.scalarInput=sc;a.structureInputDescriptor=&descriptor;a.structureOutput=reply;
  check(invoke206(*f,snapshot,a)!=G::Error::None&&!snapshot.data()&&!descriptor.prepared);++rejections;++scenarios;
  check(descriptor.prepares==1&&descriptor.reads==unsigned(fault==1||fault==2)&&descriptor.completes==unsigned(fault!=0));
  uint64_t info[8];f->state->programInfo205(0x179,info);check(info[4]==1&&f->writes==0&&f->bells==0);
 }
 {
  auto f=std::make_unique<Fake>(input);f->program(first);I::Snapshot snapshot;Descriptor206 descriptor(second);uint64_t sc[]={0x179,0,1},reply[8]{};
  Args206 a;a.scalarInput=sc;a.structureInputDescriptor=&descriptor;a.structureOutput=reply;
  check(invoke206(*f,snapshot,a)==G::Error::None&&reply[4]==2);check(descriptor.prepares==descriptor.reads&&descriptor.reads==descriptor.completes&&descriptor.completes==1);
  auto saved=Bytes(snapshot.data(),snapshot.data()+4608);descriptor.bytes[0]^=1;check(Bytes(snapshot.data(),snapshot.data()+4608)==saved);
  sc[2]=2;check(invoke206(*f,snapshot,a)==G::Error::Program&&f->writes==0&&f->bells==0);++rejections;
  // A failed next snapshot must never expose the previous successful input.
  descriptor.failPrepare=true;check(invoke206(*f,snapshot,a)!=G::Error::None&&!snapshot.data());++rejections;++scenarios;
 }
 {
  Descriptor206 d(first);uint64_t sc[]={0x179,0,1},reply[8]{};Args206 a;a.scalarInput=sc;a.structureInputDescriptor=&d;a.structureOutput=reply;
  for(unsigned selector=0;selector<=109;++selector){check(I::envelope(&a,selector,2)==(selector==107));++scenarios;}
  check(!I::envelope<Args206>(nullptr,107,2));
  // Non-descriptor old synchronous methods retain their existing envelope.
  a.structureInputDescriptor=nullptr;a.structureInputSize=512;a.structureInput=d.bytes.data();check(I::envelope(&a,102,2));
 }
 {
  // Drive the real native205 verifier through the descriptor adapter and
  // existing owned dispatch state, including ABI1 compiler output.
  auto t=std::make_unique<Test185>(input,root);Sink185 start;check(t->begin(root,first,start));Bridge206 bridge(t->io);
  unsigned serial=0;
  for(const char*name:{"16x8x1","1x1x64","runtime203","1x1x1","32x32x1","64x1x1","8x4x4"}){
   auto image=read(source/"programs"/name/"compiled.rtxlib");const auto memory=t->f.memory;Sink185 selection;
   check(t->client.select(bridge,selection,image.data()+640,4608)&&memory==t->f.memory);selection.dump(out/("select-"+std::to_string(serial+1)));
   Sink185 job;uint64_t done=0;const bool legacy=G::P::get32(image.data()+8)==1;
   check(t->client.submit(bridge,job,0,bindings206.data(),3,{legacy?1ULL:2ULL,1,1},local(image),done)&&done==++serial);++jobs;t->f.references(0);job.dump(out/("job-"+std::to_string(serial)));
  }
  check(bridge.descriptorCalls==7&&bridge.prepares==7&&bridge.reads==7&&bridge.completes==7&&t->client.revision()==8);++scenarios;
 }
 std::ofstream result(out/"result.json");result<<"{\"passed\":true,\"checks\":"<<checks<<",\"scenarios\":"<<scenarios<<",\"rejections\":"<<rejections<<",\"synthetic_jobs\":"<<jobs<<",\"descriptor_transfers\":7,\"gpu_executed\":false,\"actual_iokit\":false}\n";
 check(bool(result));std::cout<<"checks="<<checks<<" scenarios="<<scenarios<<" rejections="<<rejections<<" synthetic_jobs="<<jobs<<'\n';
}
