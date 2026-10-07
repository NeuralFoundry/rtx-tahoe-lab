#include "RuntimeTestFixture.hpp"
#include "ReusableABI.hpp"
namespace A=RtxReusableABI035;
struct Owner {unsigned long long generation=0x30603501;unsigned phase=18,command=6;bool owned=true,pinned=true,lease=true;};
int main(int argc,char **argv){
 CHECK(argc==3);Fixtures f(argv[1]);const std::string out=argv[2];auto io=std::make_unique<Backend>(f);CHECK(io->start());
 std::array<uint8_t,N::WireBytes> wire{};std::array<uint8_t,A::PlanBytes> plan{};A::Snapshot snapshot{wire.data(),plan.data(),0};
 std::array<A::U64,64> info{};std::array<A::U64,128> job{};Owner owner;
 const uint8_t *source=nullptr;unsigned bytes=0;unsigned calls=0;
 save(out+"/initial-device.bin",io->captureDevice.data(),36864);
 for(uint64_t serial=1;serial<=65;++serial){
  io->counters();CHECK(io->run(serial)==N::Failure::None);if(serial==1)calls=io->ops;
  CHECK(A::snapshot(io->state,snapshot));CHECK(!A::snapshot(io->state,snapshot));CHECK(A::job(io->state,serial,job.data()));
  A::info(&io->state,owner,true,info.data());CHECK(info[4]==serial&&info[16]==serial&&info[15]&&info[18]==0);
  CHECK(job[9]&&job[17]&&job[20]&&job[26]&&job[40]==4&&job[41]==serial&&job[43]&&job[44]==22&&job[45]==90112);
  CHECK(job[50]&&job[51]&&job[52]);CHECK(!A::job(io->state,serial+1,job.data()));
  for(unsigned part=0;part<7;++part){CHECK(A::data(io->state,part>=5?0:serial,part,snapshot,source,bytes));CHECK(source&&bytes);
   CHECK(A::span(0,bytes<4096?bytes:4096,bytes));CHECK(!A::span(bytes,1,bytes));CHECK(!A::span(UINT64_MAX,1,bytes));CHECK(!A::span(0,0,bytes));CHECK(!A::span(0,4097,bytes));}
  CHECK(!A::data(io->state,serial+1,0,snapshot,source,bytes));CHECK(!A::data(io->state,serial,5,snapshot,source,bytes));CHECK(!A::data(io->state,serial,7,snapshot,source,bytes));
  if(serial==1||serial==32||serial==33||serial==65){const auto prefix=out+"/job-"+std::to_string(serial);
   save(prefix+"-request.bin",wire.data(),wire.size());save(prefix+"-plan.bin",plan.data(),plan.size());save(prefix+"-info.bin",info.data(),512);save(prefix+"-job.bin",job.data(),1024);
   save(prefix+"-root.bin",io->captureRoot.data(),12288);save(prefix+"-children.bin",io->captureChildren.data(),40960);save(prefix+"-device.bin",io->captureDevice.data(),36864);
  }
 }
 ++scenarios;
 for(unsigned fault=1;fault<=calls;++fault){
  io=std::make_unique<Backend>(f);CHECK(io->start());CHECK(io->run(1)==N::Failure::None);snapshot.serial=0;CHECK(A::snapshot(io->state,snapshot));
  io->counters();io->failOp=fault;CHECK(io->run(2)!=N::Failure::None&&io->state.closed);CHECK(A::job(io->state,2,job.data()));CHECK(!job[9]);
  CHECK(!A::data(io->state,2,3,snapshot,source,bytes));CHECK(!A::data(io->state,2,4,snapshot,source,bytes));
  CHECK(A::snapshot(io->state,snapshot));CHECK(A::data(io->state,2,3,snapshot,source,bytes)&&bytes==N::WireBytes);
  const auto *capture=A::active(io->state);unsigned exposed=0;
  for(unsigned part=0;part<3;++part){const bool ok=A::data(io->state,2,part,snapshot,source,bytes);
   if(!capture||capture->serial!=2)CHECK(!ok);
   else if(ok)exposed+=bytes;
  }
  CHECK(!capture||capture->serial!=2||exposed==capture->bytes);
  if(io->state.window.serial!=2)CHECK(job[20]==0&&job[21]==0&&job[26]==0);
  if(io->state.completed.serial!=2)CHECK(job[52]==0);
  if(fault==1){CHECK(exposed==0&&job[40]==0);save(out+"/failed-before-window-job.bin",job.data(),1024);}
  ++scenarios;
 }
 io=std::make_unique<Backend>(f);CHECK(io->start());CHECK(io->run(1)==N::Failure::None);
 A::Snapshot alias{io->captureDevice.data(),plan.data(),0};CHECK(!A::snapshot(io->state,alias));
 alias={wire.data(),wire.data(),0};CHECK(!A::snapshot(io->state,alias));++scenarios;
 CHECK(!A::selector(67)&&A::selector(68)&&A::selector(71)&&!A::selector(72));
 std::printf("{\"passed\":true,\"checks\":%u,\"scenarios\":%u,\"jobs\":65,\"fault_positions_after_first_job\":%u,\"stale_capture_rejected\":true,\"snapshot_alias_rejected\":true,\"state_bytes\":%zu,\"cpu_simulated\":true,\"gpu_commands_submitted\":false,\"metal_verified\":false}\n",checks,scenarios,calls,sizeof(R::State));
}
