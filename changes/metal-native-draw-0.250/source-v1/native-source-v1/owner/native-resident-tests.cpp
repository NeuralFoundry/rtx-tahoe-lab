#include "../probe/kernel/SelectorRouting171.hpp"
#include "NativeCommandSession.hpp"
#include "ResidentABI.hpp"
#include "ReusableABI.hpp"
#include "CompletionObservationABI.hpp"
#include "RuntimeTestFixture.hpp"
#include <map>
namespace E=RTXNativeEvidence036;namespace S=RTXNativeSession036;
namespace D=RtxResident058;namespace A=RtxResidentABI058;namespace U=RtxLibraryUpload036;namespace P=E::P;
using Data=std::vector<uint8_t>;
struct Owner {uint64_t generation=0x30603501;unsigned phase=18,pinned=1,owned=1,command=6,lease=1;};
struct Device:Backend {
 D::State resident;U::State upload;
 Data bootRoot,bootChildren,bootDevice,executionChildren;
 std::array<uint8_t,2112> wire{};std::array<uint8_t,4096> plan{};RtxReusableABI035::Snapshot snapshot;
 explicit Device(Fixtures &f):Backend(f){
  CHECK(resident.seed(0x30603501,17,f.library.data(),f.code.data()));
  std::array<uint8_t,4608> payload{};std::memcpy(payload.data(),f.library.data(),512);std::memcpy(payload.data()+512,f.code.data(),4096);
  std::array<uint8_t,128> header{};auto *b=header.data();P::Q::put64(b,U::HeaderMagic);P::Q::put32(b+8,1);P::Q::put32(b+12,128);P::Q::put64(b+16,0x30603501);
  P::Q::put32(b+24,4608);P::Q::put32(b+28,1024);P::Q::put32(b+32,5);P::Q::put32(b+36,0x86);GSPDigest::SHA256 hash;hash.update(payload.data(),4608);hash.finish(b+40);
  U::Scope scope{0x30603501,17,true};CHECK(upload.begin(scope,b,128)==U::Error::None);
  for(unsigned off=0;off<4608;off+=1024)CHECK(upload.append(scope,off,payload.data()+off,4608-off<1024?4608-off:1024)==U::Error::None);
  CHECK(upload.seal(scope)==U::Error::None&&upload.consume(scope)==U::Error::None);
  snapshot.request=wire.data();snapshot.plan=plan.data();
 }
 D::Scope scope()const{return {0x30603501,17,state.core.completed(),!state.closed&&runtime&&state.core.phase()==N::Phase::Ready};}
 bool start(){auto m=storage();m.library=resident.library();m.code=resident.code();R::Runtime<Device> r(*this,state);
  if(!r.prepare(0x30603501,17,m))return false;bootstrapRestore();if(!r.open())return false;
  bootRoot=captureRoot;bootChildren=captureChildren;bootDevice=captureDevice;executionChildren=bootChildren;
  std::fill(executionChildren.begin()+4128,executionChildren.begin()+4176,uint8_t(0));return true;
 }
 bool writeResidentMemory(unsigned addressValue,const uint8_t *data,unsigned n){
  CHECK(owned&&runtime&&window==0&&state.core.phase()==N::Phase::Ready&&state.backing.phase()==B::Phase::Ready);
  CHECK(resident.phase()==D::Phase::Applying&&resident.result().exposed&&resident.result().writes==1);
  CHECK(addressValue==0x03409000&&data==resident.candidateCode()&&n==4096);++writes;const bool ok=op();std::memcpy(address(addressValue,n),data,n);return ok;
 }
 bool replace(){return D::replace(*this,state,resident,scope());}
};
struct IO {
 Device &dev;unsigned calls=0,submits=0,applies=0,failCall=0,lateCall=0,corruptCall=0;size_t corruptOffset=0;uint64_t clock=100;
 explicit IO(Device &d):dev(d){}uint64_t nowNs(){return ++clock;}
 bool call(unsigned selector,const uint64_t *scalars,unsigned count,const uint8_t *input,size_t n,uint8_t *output,size_t capacity){
  const auto route=RTXSelector171::route(selector);
#if defined(RTX_TEST_LEGACY_COLLISION_166)
  if(selector>=78&&selector<=84)return false; // Regression control: old host route intercepted these.
#endif
  if(route==RTXSelector171::Route::Host||route==RTXSelector171::Route::Root)return false;
  ++calls;bool ok=false;const uint8_t *data=nullptr;unsigned size=0;uint64_t offset=0;
  CHECK(bool(scalars)==bool(count)&&bool(input)==bool(n)&&bool(output)==bool(capacity));
  if(selector==68){CHECK(count==0&&n==0&&capacity==512);RtxReusableABI035::U64 words[64];RtxReusableABI035::info(&dev.state,Owner{},true,words);std::memcpy(output,words,512);ok=true;}
  else if(selector==72){CHECK(count==0&&n==0&&capacity==256);ok=dev.upload.info(output,capacity);}
  else if(selector==70){CHECK(count==1&&n==0&&capacity==1024);RtxReusableABI035::U64 words[128];ok=RtxReusableABI035::job(dev.state,scalars[0],words);if(ok)std::memcpy(output,words,1024);}
  else if(selector==77){ok=RtxCompletionObservation037::dispatch(&dev.state,0x30603501,selector,{scalars,count,input,n,output,capacity});}
  else if(selector==71){CHECK(count==4&&n==0&&scalars[1]<=6&&capacity==scalars[3]);ok=RtxReusableABI035::data(dev.state,scalars[0],unsigned(scalars[1]),dev.snapshot,data,size);offset=scalars[2];}
  else if(selector==83){
   CHECK(count==4&&n==0&&scalars[1]<=6&&capacity==scalars[3]);ok=dev.resident.ownsSnapshot(dev.scope(),scalars[0]);const auto part=scalars[1];offset=scalars[2];
   if(part<4){data=part==0?dev.resident.library():part==1?dev.resident.code():part==2?dev.resident.candidateLibrary():dev.resident.candidateCode();size=part%2?4096:512;}
   else{ok=ok&&dev.state.activeCapture==5;data=part==4?dev.captureRoot.data():part==5?dev.captureChildren.data():dev.captureDevice.data();size=part==4?12288:part==5?40960:36864;}
  }else if(selector>=78&&selector<=82){if(selector==81)++applies;ok=A::dispatch(dev.resident,dev.scope(),selector,{scalars,count,input,n,output,capacity},[&](){return dev.replace();})==D::Error::None;}
  else if(selector==84){
   CHECK(count==1&&n==2112&&capacity==0);++submits;ok=dev.resident.accepts(dev.scope(),scalars[0]);
   if(ok){R::Runtime<Device> r(dev,dev.state);ok=r.submit(17,input,n)==N::Failure::None;RtxReusableABI035::snapshot(dev.state,dev.snapshot);}
  }else if(selector==67||selector==51){
   CHECK(count==3&&n==0&&capacity==scalars[2]);CHECK(scalars[0]<=(selector==67?2u:1u));offset=scalars[1];const auto part=scalars[0];
   if(selector==67){data=part==0?dev.bootRoot.data():part==1?dev.bootChildren.data():dev.bootDevice.data();size=part==0?12288:part==1?40960:36864;}
   else{data=part==0?dev.bootRoot.data():dev.executionChildren.data();size=part==0?12288:40960;}ok=true;
  }else if(selector==54){CHECK(count==2&&n==0&&capacity==scalars[1]);offset=scalars[0];data=dev.bootDevice.data();size=12288;ok=true;}
  else CHECK(false);
  if(data){ok=ok&&RtxReusableABI035::span(offset,capacity,size);if(ok)std::memcpy(output,data+offset,capacity);}
  if(calls==corruptCall&&ok){CHECK(corruptOffset<capacity);output[corruptOffset]^=1;}
  if(calls==lateCall)clock+=S::BudgetNs;
  return calls!=failCall&&ok;
 }
};
struct Sink {unsigned calls=0,failCall=0;std::map<std::string,Data> files;
 bool save(const char *name,const uint8_t *data,size_t n){++calls;if(calls==failCall)return false;CHECK(files.emplace(name,Data(data,data+n)).second);return true;}
};
struct Harness {
 Device device;IO io;S::Session session;
 explicit Harness(Fixtures &f):device(f),io(device){CHECK(device.start());CHECK(session.configure(0x30603501,{device.resident.library(),4608}));Sink sink;CHECK(session.arm(io,sink));io.calls=0;}
};
int main(int argc,char **argv){
 CHECK(argc==4);Fixtures f(argv[1]);auto container=load(argv[3]);CHECK(container.size()==5248);Data changed(container.begin()+640,container.end()),initial=f.library;initial.insert(initial.end(),f.code.begin(),f.code.end());
 const std::string output=argv[2];auto h=std::make_unique<Harness>(f);unsigned replacementCalls=0,replacementSaves=0,jobCalls=0,jobSaves=0;
 for(unsigned serial=1;serial<=65;++serial){
  const auto &payload=serial%2?changed:initial;Sink replacement;h->io.calls=0;const auto before=h->device.device;const auto epoch=h->session.epoch();
  CHECK(h->session.replace(h->io,replacement,{payload.data(),payload.size()}));replacementCalls=h->io.calls;replacementSaves=replacement.calls;
  CHECK(h->session.epoch()==epoch+1&&h->session.completed()==serial-1&&h->session.ready()&&h->device.resident.epoch()==epoch+1);
  CHECK(std::memcmp(h->device.resident.library(),payload.data(),4608)==0);for(unsigned i=0;i<36864;++i)CHECK(h->device.device[i]==(i>=12288&&i<16384?payload[512+i-12288]:before[i]));
  const auto &hdr=replacement.files.at("replacement-header.bin");auto info=replacement.files.at("resident-after.bin");
  CHECK(RTXResidentEvidence058::replaced({info.data(),256},{hdr.data(),128},40960));
  if(serial==1){
   for(unsigned i=0;i<256;++i){if(i>=144&&i<160)continue;info[i]^=1;CHECK(!RTXResidentEvidence058::replaced({info.data(),256},{hdr.data(),128},40960));info[i]^=1;}
  }
  auto r=request(serial);r.program=0;P::Library lib;CHECK(P::decode(payload.data(),512,payload.data()+512,4096,lib));std::array<uint8_t,2112> wire{};CHECK(N::encode(r,lib,wire.data(),wire.size()));
  const auto count=h->device.ops;CHECK(!h->io.call(84,&epoch,1,wire.data(),2112,nullptr,0)&&h->device.ops==count);
  Sink job;std::array<uint8_t,2048> result{};uint64_t completed=0;h->io.calls=0;
  CHECK(h->session.execute(h->io,job,{wire.data(),2112},{payload.data(),4608},result,completed));jobCalls=h->io.calls;jobSaves=job.calls;
  CHECK(completed==serial&&h->session.completed()==serial&&h->session.epoch()==serial+1&&h->session.ready());
  CHECK(std::memcmp(result.data(),h->device.device.data()+16384,2048)==0);
  if(serial==1||serial==2||serial==65){
   for(auto *set:{&replacement.files,&job.files})for(auto &item:*set)save(output+"/"+(set==&replacement.files?"replace-":"job-")+std::to_string(serial)+"-"+item.first,item.second.data(),item.second.size());
  }
 }
 unsigned faults=0,saves=0,corruptions=0;
 for(unsigned mode=0;mode<2;++mode)for(unsigned fault=1;fault<=replacementCalls;++fault){
  h=std::make_unique<Harness>(f);Sink sink;if(mode)h->io.lateCall=fault;else h->io.failCall=fault;
  CHECK(!h->session.replace(h->io,sink,{changed.data(),4608})&&!h->session.ready()&&h->session.epoch()==1&&h->session.completed()==0&&h->io.applies<=1);
  const auto calls=h->io.calls;CHECK(!h->session.replace(h->io,sink,{changed.data(),4608})&&h->io.calls==calls);++faults;
 }
 for(unsigned fault=1;fault<=replacementSaves;++fault){
  h=std::make_unique<Harness>(f);Sink sink;sink.failCall=fault;CHECK(!h->session.replace(h->io,sink,{changed.data(),4608})&&!h->session.ready()&&h->session.epoch()==1);++saves;
 }
 for(unsigned offset:{0u,24u,40u,104u,112u,128u,136u,160u,192u,224u,255u}){
  h=std::make_unique<Harness>(f);Sink sink;h->io.corruptCall=14;h->io.corruptOffset=offset; // first post-apply info
  CHECK(!h->session.replace(h->io,sink,{changed.data(),4608})&&!h->session.ready());++corruptions;
 }
 for(unsigned call:{17u,21u,33u,38u}){
  h=std::make_unique<Harness>(f);Sink sink;h->io.corruptCall=call;h->io.corruptOffset=8;
  CHECK(!h->session.replace(h->io,sink,{changed.data(),4608})&&!h->session.ready());++corruptions;
 }
 std::array<uint8_t,2112> first{};P::Library firstLib;CHECK(P::decode(changed.data(),512,changed.data()+512,4096,firstLib));auto firstRequest=request(1);firstRequest.program=0;CHECK(N::encode(firstRequest,firstLib,first.data(),first.size()));
 unsigned jobFailures=0;
 for(unsigned mode=0;mode<3;++mode)for(unsigned fault=1;fault<=(mode==2?jobSaves:jobCalls);++fault){
  h=std::make_unique<Harness>(f);Sink replacement,sink;CHECK(h->session.replace(h->io,replacement,{changed.data(),4608}));h->io.calls=0;
  if(mode==0)h->io.failCall=fault;else if(mode==1)h->io.lateCall=fault;else sink.failCall=fault;
  std::array<uint8_t,2048> result;result.fill(0xa5);uint64_t completion=99;
  CHECK(!h->session.execute(h->io,sink,{first.data(),2112},{changed.data(),4608},result,completion)&&completion==0&&!h->session.ready()&&h->session.epoch()==2&&h->session.completed()==0);
  for(auto b:result)CHECK(b==0xa5);const auto calls=h->io.calls;
  CHECK(!h->session.execute(h->io,sink,{first.data(),2112},{changed.data(),4608},result,completion)&&h->io.calls==calls);++jobFailures;
 }
 h=std::make_unique<Harness>(f);Sink replacementA,replacementB;
 CHECK(h->session.replace(h->io,replacementA,{changed.data(),4608})&&h->session.replace(h->io,replacementB,{initial.data(),4608})&&h->session.epoch()==3&&h->session.completed()==0);
 h=std::make_unique<Harness>(f);Sink sink;Data bad=changed;bad[0]^=1;CHECK(!h->session.replace(h->io,sink,{bad.data(),4608})&&h->io.calls==0&&h->io.applies==0);
 scenarios=faults+saves+corruptions+jobFailures+3;
 std::printf("{\"passed\":true,\"checks\":%u,\"scenarios\":%u,\"replacements\":65,\"jobs\":65,\"replacement_calls\":%u,\"replacement_saves\":%u,\"job_calls\":%u,\"job_saves\":%u,\"call_or_time_failures\":%u,\"save_failures\":%u,\"corrupt_statuses\":%u,\"job_failures\":%u,\"cpu_fixture_only\":true,\"shader_arithmetic_verified\":false,\"gpu_commands_submitted\":false}\n",checks,scenarios,replacementCalls,replacementSaves,jobCalls,jobSaves,faults,saves,corruptions,jobFailures);
}
