#include "ResidentABI.hpp"
#include "RuntimeTestFixture.hpp"
namespace D=RtxResident058;namespace A=RtxResidentABI058;namespace P=RtxProgram033;
struct ResidentBackend:Backend {
 D::State resident;bool corruptAfter=false;
 explicit ResidentBackend(Fixtures &f):Backend(f){CHECK(resident.seed(0x30603501,17,f.library.data(),f.code.data()));}
 D::Scope scope()const{return {0x30603501,17,state.core.completed(),!state.closed&&runtime&&state.core.phase()==N::Phase::Ready};}
 bool start(){auto m=storage();m.library=resident.library();m.code=resident.code();R::Runtime<ResidentBackend> engine(*this,state);if(!engine.prepare(0x30603501,17,m))return false;bootstrapRestore();return engine.open();}
 bool writeResidentMemory(unsigned a,const uint8_t *p,unsigned n){
  const auto &r=resident.result();CHECK(owned&&runtime&&window==0&&state.core.phase()==N::Phase::Ready&&state.backing.phase()==B::Phase::Ready);
  CHECK(resident.phase()==D::Phase::Applying&&r.saved&&r.windowAttempted&&r.exposed&&!r.restoreAttempted&&r.writes==1);
  CHECK(a==N::P::Q::ProgramPhysical&&n==4096&&p==resident.candidateCode());++writes;const bool ok=op();std::memcpy(address(a,n),p,n);if(corruptAfter)device[B::Fence+8]^=1;return ok;
 }
 N::Failure job(uint64_t serial,uint64_t epoch){
  if(!resident.accepts(scope(),epoch))return N::Failure::State;
  N::P::Library lib;CHECK(N::P::decode(resident.library(),512,resident.code(),4096,lib));auto r=request(serial);r.program=0;
  std::array<uint8_t,N::WireBytes> wire{};CHECK(N::encode(r,lib,wire.data(),wire.size()));R::Runtime<ResidentBackend> engine(*this,state);return engine.submit(17,wire.data(),wire.size());
 }
 bool replace(){return D::replace(*this,state,resident,scope());}
};
static std::array<uint8_t,D::HeaderBytes> header(const D::Scope &scope,uint64_t epoch,const std::vector<uint8_t> &payload){
 std::array<uint8_t,D::HeaderBytes> h{};P::Q::put64(h.data(),D::Magic);P::Q::put32(h.data()+8,1);P::Q::put32(h.data()+12,D::HeaderBytes);
 P::Q::put64(h.data()+16,scope.generation);P::Q::put64(h.data()+24,epoch);P::Q::put64(h.data()+32,scope.completed);P::Q::put32(h.data()+40,D::PayloadBytes);P::Q::put32(h.data()+44,0x86);
 GSPDigest::SHA256 hash;hash.update(payload.data(),unsigned(payload.size()));hash.finish(h.data()+48);return h;
}
static void stage(ResidentBackend &io,const std::vector<uint8_t> &payload){
 const auto s=io.scope();auto h=header(s,io.resident.epoch(),payload);const auto ops=io.ops;
 CHECK(io.resident.begin(s,h.data(),h.size())==D::Error::None);
 for(unsigned off=0;off<D::PayloadBytes;off+=1024){const unsigned n=D::PayloadBytes-off<1024?D::PayloadBytes-off:1024;CHECK(io.resident.append(s,off,payload.data()+off,n)==D::Error::None);}
 CHECK(io.resident.seal(s)==D::Error::None&&io.ops==ops&&io.resident.phase()==D::Phase::Sealed);
}
int main(int argc,char **argv){
 CHECK(argc==4);Fixtures f(argv[1]);const std::string output=argv[2];auto container=load(argv[3]);CHECK(container.size()==5248);
 std::vector<uint8_t> changed(container.begin()+640,container.end()),initial=f.library;initial.insert(initial.end(),f.code.begin(),f.code.end());CHECK(initial!=changed);
 auto io=std::make_unique<ResidentBackend>(f);CHECK(io->start());unsigned ops=0,clocks=0;
 for(unsigned i=1;i<=65;++i){
  const auto &payload=i%2?changed:initial;const auto oldEpoch=io->resident.epoch();const auto before=io->device;
  stage(*io,payload);CHECK(io->device==before&&io->resident.epoch()==oldEpoch);io->counters();CHECK(io->replace());
  CHECK(io->writes==1&&io->windowWrites==2&&io->notifications==0&&io->reads==44&&io->window==io->originalWindow());
  CHECK(io->resident.epoch()==oldEpoch+1&&io->resident.phase()==D::Phase::Idle&&io->resident.result().passed&&io->state.core.completed()==i-1&&io->state.backing.completed()==i-1);
  CHECK(std::memcmp(io->resident.library(),payload.data(),4608)==0&&std::memcmp(io->device.data()+B::Image,payload.data()+512,4096)==0);
  for(unsigned j=0;j<B::DeviceBytes;++j)CHECK(io->device[j]==(j>=B::Image&&j<B::Image+4096?payload[512+j-B::Image]:before[j]));
  if(i==1){ops=io->ops;clocks=io->clocks;}
  const auto count=io->ops;CHECK(io->job(i,oldEpoch)==N::Failure::State&&io->ops==count);CHECK(io->job(i,oldEpoch+1)==N::Failure::None);
  CHECK(io->state.core.completed()==i&&io->state.backing.completed()==i&&io->resident.epoch()==oldEpoch+1);
  const auto &plan=io->state.core.plan();CHECK(plan.launch.codeBytes==P::get32(payload.data()+72)&&plan.launch.registers==P::get32(payload.data()+76));
  // Existing QMD and method stream explicitly invalidate instruction caches.
  CHECK((plan.qmd[190/8]&(1u<<(190%8)))!=0&&P::get32(plan.command+8)==0x200125a6&&P::get32(plan.command+12)==0x1011);
  if(i==1||i==2||i==65){const auto p=output+"/epoch-"+std::to_string(oldEpoch+1);save(p+"-device.bin",io->device.data(),io->device.size());save(p+"-library.bin",io->resident.library(),512);save(p+"-code.bin",io->resident.code(),4096);save(p+"-qmd.bin",plan.qmd,256);}
 }
 ++scenarios;
 for(unsigned mode=0;mode<2;++mode)for(unsigned fault=1;fault<=ops;++fault){
  io=std::make_unique<ResidentBackend>(f);CHECK(io->start());stage(*io,changed);io->counters();if(mode)io->loseOp=fault;else io->failOp=fault;
  CHECK(!io->replace());CHECK(io->state.closed&&io->resident.phase()==D::Phase::Retained&&io->resident.epoch()==1&&io->retains==1);
  CHECK(std::memcmp(io->resident.library(),initial.data(),4608)==0);CHECK(io->resident.ownsSnapshot(io->scope(),1)&&!io->resident.accepts(io->scope(),1));const auto count=io->ops;CHECK(!io->replace()&&io->ops==count);CHECK(io->writes<=1&&io->notifications==0&&io->windowWrites<=2);++scenarios;
 }
 for(unsigned mode=0;mode<2;++mode)for(unsigned fault=2;fault<=clocks;++fault){
  io=std::make_unique<ResidentBackend>(f);CHECK(io->start());stage(*io,changed);io->counters();io->failClock=fault;io->expire=bool(mode);
  const bool replaced=io->replace();if(replaced||!io->state.closed||io->resident.epoch()!=1)std::fprintf(stderr,"clock mode=%u fault=%u total=%u success=%u phase=%u\n",mode,fault,clocks,unsigned(replaced),unsigned(io->resident.phase()));
  CHECK(!replaced&&io->state.closed&&io->resident.phase()==D::Phase::Retained&&io->resident.epoch()==1);++scenarios;
 }
 for(unsigned offset:{0u,255u,0x840u,0x888u,4096u,B::Image,B::Data,B::Qmd,B::Fence,B::Fence+16,36863u}){
  io=std::make_unique<ResidentBackend>(f);CHECK(io->start());stage(*io,changed);io->device[offset]^=1;io->counters();CHECK(!io->replace()&&io->state.closed&&io->writes==0);++scenarios;
 }
 io=std::make_unique<ResidentBackend>(f);CHECK(io->start());stage(*io,changed);io->corruptAfter=true;CHECK(!io->replace()&&io->state.closed&&io->resident.epoch()==1);++scenarios;
 io=std::make_unique<ResidentBackend>(f);CHECK(io->start());
 auto s=io->scope();auto h=header(s,1,changed);auto bad=h;bad[0]^=1;const auto calls=io->ops;
 CHECK(io->resident.begin(s,bad.data(),bad.size())==D::Error::Shape);bad=h;bad[24]^=1;CHECK(io->resident.begin(s,bad.data(),bad.size())==D::Error::Stale);
 auto foreign=s;foreign.client=18;CHECK(io->resident.begin(foreign,h.data(),h.size())==D::Error::Identity);CHECK(io->ops==calls);
 stage(*io,changed);CHECK(io->job(1,1)==N::Failure::None);CHECK(!io->replace()&&!io->state.closed&&io->resident.epoch()==1);stage(*io,changed);CHECK(io->replace()&&io->resident.epoch()==2);++scenarios;
 io=std::make_unique<ResidentBackend>(f);CHECK(io->start());s=io->scope();h=header(s,1,changed);h[48]^=1;CHECK(io->resident.begin(s,h.data(),h.size())==D::Error::None);
 CHECK(io->resident.append(s,1,changed.data(),1024)==D::Error::Order);
 for(unsigned off=0;off<4608;off+=1024)CHECK(io->resident.append(s,off,changed.data()+off,4608-off<1024?4608-off:1024)==D::Error::None);
 CHECK(io->resident.seal(s)==D::Error::Digest);CHECK(!io->replace()&&!io->state.closed);stage(*io,changed);CHECK(io->replace());++scenarios;
 std::array<uint8_t,256> info{};unsigned applyCalls=0;auto apply=[&](){++applyCalls;return false;};
 CHECK(A::dispatch(io->resident,io->scope(),A::Info,{nullptr,0,nullptr,0,info.data(),info.size()},apply)==D::Error::None&&P::get64(info.data()+24)==2);
 const uint64_t stale=1;CHECK(A::dispatch(io->resident,io->scope(),A::Apply,{&stale,1,nullptr,0,nullptr,0},apply)==D::Error::Stale&&applyCalls==0);++scenarios;
 std::printf("{\"passed\":true,\"checks\":%u,\"scenarios\":%u,\"replacements\":65,\"subsequent_jobs\":65,\"fault_positions\":%u,\"clock_positions\":%u,\"resident_state_bytes\":%zu,\"cpu_fixture_only\":true,\"shader_arithmetic_verified\":false,\"gpu_commands_submitted\":false}\n",checks,scenarios,ops,clocks,sizeof(D::State));
}
