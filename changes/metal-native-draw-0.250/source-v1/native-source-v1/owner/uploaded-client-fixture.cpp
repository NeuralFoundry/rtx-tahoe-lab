#include "UploadedRuntimeTestFixture.hpp"
#include "ReusableABI.hpp"
#include "CompletionObservationABI.hpp"
namespace A=RtxReusableABI035;
struct Owner {unsigned long long generation=0x130603601;unsigned phase=18,command=6;bool owned=true,pinned=true,lease=true;};
int main(int argc,char **argv){
 CHECK(argc==3);Fixtures f(argv[1]);const std::string out=argv[2];auto io=std::make_unique<Backend>(f);CHECK(io->start());
 std::array<uint8_t,256> uploadInfo{};CHECK(io->upload.info(uploadInfo.data(),uploadInfo.size()));save(out+"/upload-info.bin",uploadInfo.data(),uploadInfo.size());
 Owner owner;std::array<A::U64,64> info{};std::array<A::U64,128> job{};std::array<uint8_t,N::WireBytes> wire{};std::array<uint8_t,A::PlanBytes> plan{};A::Snapshot snapshot{wire.data(),plan.data(),0};
 A::info(&io->state,owner,true,info.data());save(out+"/initial-info.bin",info.data(),512);
 save(out+"/initial-root.bin",io->captureRoot.data(),12288);save(out+"/initial-children.bin",io->captureChildren.data(),40960);save(out+"/initial-device.bin",io->captureDevice.data(),36864);
 for(uint64_t serial=1;serial<=65;++serial){
  CHECK(io->run(serial)==N::Failure::None);CHECK(A::snapshot(io->state,snapshot));CHECK(A::job(io->state,serial,job.data()));A::info(&io->state,owner,true,info.data());
  CHECK(info[4]==serial&&info[15]&&info[16]==serial&&job[9]&&job[40]==4&&job[41]==serial);
  std::array<uint8_t,512> observation{};CHECK(RtxCompletionObservation037::capture(&io->state,owner.generation,serial,observation.data(),observation.size()));
  const auto prefix=out+"/job-"+std::to_string(serial);
  save(prefix+"-observation.bin",observation.data(),observation.size());
  save(prefix+"-request.bin",wire.data(),wire.size());save(prefix+"-plan.bin",plan.data(),plan.size());save(prefix+"-info.bin",info.data(),512);save(prefix+"-job.bin",job.data(),1024);
  save(prefix+"-root.bin",io->captureRoot.data(),12288);save(prefix+"-children.bin",io->captureChildren.data(),40960);save(prefix+"-device.bin",io->captureDevice.data(),36864);
 }
 std::printf("{\"passed\":true,\"checks\":%u,\"jobs\":65,\"writes\":%u,\"notifications\":%u,\"reads\":%u,\"backend_operations\":%u,\"state_bytes\":%zu,\"cpu_simulated\":true,\"gpu_commands_submitted\":false,\"metal_verified\":false}\n",checks,io->writes,io->notifications,io->reads,io->ops,sizeof(R::State));
}
