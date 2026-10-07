#include "CompletionTestFixture.hpp"
#include "CompletionObservationABI.hpp"
namespace O=RtxCompletionObservation037;
static uint64_t word(const std::array<uint8_t,512> &b,unsigned n){return N::P::get64(b.data()+n*8);}
static void reserved(const std::array<uint8_t,512> &b){for(unsigned n=28;n<32;++n)CHECK(!word(b,n));for(unsigned n=43;n<48;++n)CHECK(!word(b,n));for(unsigned n=59;n<64;++n)CHECK(!word(b,n));}
int main(int argc,char **argv){
 CHECK(argc==3);Fixtures f(argv[1]);const std::string out=argv[2];unsigned calls=0,valid=0,bad=0;std::array<uint8_t,512> snapshot{};
 CHECK(O::capture(nullptr,0x30603501,0,snapshot.data(),snapshot.size()));CHECK(word(snapshot,0)==O::Magic&&word(snapshot,1)==1&&word(snapshot,2)==0x30603501&&word(snapshot,3)==0);reserved(snapshot);save(out+"/empty.bin",snapshot.data(),512);
 std::array<uint64_t,4> scalars{};uint8_t input=0;
 for(unsigned selector:{76u,77u,78u})for(unsigned count=0;count<=4;++count)for(size_t n:{size_t(0),size_t(1)})for(size_t bytes:{size_t(0),size_t(511),size_t(512),size_t(513)}){
  snapshot.fill(0xa5);const auto original=snapshot;O::Call call{scalars.data(),count,n?&input:nullptr,n,snapshot.data(),bytes};
  const bool accepted=O::dispatch(nullptr,0x30603501,selector,call);CHECK(accepted==(selector==77&&count==1&&n==0&&bytes==512));if(!accepted)CHECK(snapshot==original);
 }
 O::Call alias{scalars.data(),1,nullptr,0,reinterpret_cast<uint8_t*>(scalars.data()),512};CHECK(!O::dispatch(nullptr,0x30603501,77,alias));alias.output=reinterpret_cast<uint8_t*>(&alias);CHECK(!O::dispatch(nullptr,0x30603501,77,alias));
 O::Call ignored{scalars.data(),1,&input,0,snapshot.data(),512};CHECK(O::dispatch(nullptr,0x30603501,77,ignored));
 {auto empty=std::make_unique<R::State>();empty->closed=true;CHECK(O::capture(empty.get(),0x30603501,0,snapshot.data(),512));CHECK(word(snapshot,27)==1&&word(snapshot,7)==0);save(out+"/empty-retained.bin",snapshot.data(),512);}
 for(unsigned mode=0;mode<8;++mode){auto io=std::make_unique<Interleaved>(f,mode);CHECK(io->start());CHECK(O::capture(&io->state,0x30603501,0,snapshot.data(),512));CHECK(!word(snapshot,3)&&!word(snapshot,7));
  io->counters();const auto result=io->run(1);if(mode==0)calls=io->ops;bool legacyFailure=false;
#if defined(RTX_TEST_LEGACY_OBSERVATION)
  legacyFailure=mode==2;
#endif
  CHECK(O::capture(&io->state,0x30603501,1,snapshot.data(),512));CHECK(word(snapshot,3)==1&&word(snapshot,7)==1&&word(snapshot,9)==1&&word(snapshot,10)==7&&word(snapshot,11)==1&&word(snapshot,22)==1&&word(snapshot,23)==R::ObservationOrder);reserved(snapshot);
  if(legacyFailure||mode==5||mode==7){
   CHECK(result==N::Failure::Changed&&word(snapshot,6)==10&&word(snapshot,8)==0&&word(snapshot,17)==(mode==7?20:16));
   CHECK(word(snapshot,32)==4&&word(snapshot,34)==1&&word(snapshot,35)==1&&word(snapshot,36)==1);
   CHECK(word(snapshot,41)==(mode==7?2:0)&&word(snapshot,42)==1);++bad;
  }else if(mode==6){CHECK(result==N::Failure::Timeout&&word(snapshot,6)==7&&!word(snapshot,8)&&!word(snapshot,17));++bad;}
  else{CHECK(result==N::Failure::None&&word(snapshot,8)==1&&word(snapshot,5)==1&&!word(snapshot,17));CHECK(word(snapshot,32)==5&&word(snapshot,48)==4&&word(snapshot,41)==1&&word(snapshot,42)==1);++valid;}
  CHECK(word(snapshot,33)==word(snapshot,16)&&word(snapshot,49)+1==word(snapshot,33));save(out+"/mode-"+std::to_string(mode)+".bin",snapshot.data(),512);++scenarios;
  const auto kept=snapshot;CHECK(!O::capture(&io->state,0x30603501,0,snapshot.data(),512)&&snapshot==kept);CHECK(!O::capture(&io->state,0x30603501,2,snapshot.data(),512)&&snapshot==kept);
  CHECK(!O::capture(&io->state,0x30603502,1,snapshot.data(),512)&&snapshot==kept);CHECK(!O::capture(&io->state,0x30603501,1,reinterpret_cast<uint8_t*>(&io->state),512));
  for(size_t n:{size_t(0),size_t(511),size_t(513),SIZE_MAX})CHECK(!O::capture(&io->state,0x30603501,1,snapshot.data(),n)&&snapshot==kept);
  const auto ops=io->ops;std::vector<uint8_t> state(sizeof(io->state));std::memcpy(state.data(),&io->state,state.size());CHECK(O::capture(&io->state,0x30603501,1,snapshot.data(),512)&&snapshot==kept);CHECK(io->ops==ops&&!std::memcmp(state.data(),&io->state,state.size()));
 }
 // Every native-backend failure position after one successful job. Records
 // must name job 2 and cannot present a partial observation as complete.
 for(unsigned fault=1;fault<=calls;++fault){auto io=std::make_unique<Backend>(f);CHECK(io->start());CHECK(io->run(1)==N::Failure::None);io->counters();io->failOp=fault;CHECK(io->run(2)!=N::Failure::None);
  CHECK(O::capture(&io->state,0x30603501,2,snapshot.data(),512));CHECK(word(snapshot,3)==2&&!word(snapshot,8)&&word(snapshot,6)!=0);reserved(snapshot);
  if(word(snapshot,36))CHECK(word(snapshot,34)==1&&word(snapshot,35)==1);
  if(!word(snapshot,34))CHECK(!word(snapshot,35)&&!word(snapshot,36));
  if(word(snapshot,16)){CHECK(word(snapshot,33)==word(snapshot,16)&&word(snapshot,32)>=1&&word(snapshot,32)<=5);CHECK(word(snapshot,49)+(word(snapshot,16)>1?1:0)==word(snapshot,16)-(word(snapshot,16)==1?1:0));}
  save(out+"/fault-"+std::to_string(fault)+".bin",snapshot.data(),512);++scenarios;
 }
 std::printf("{\"passed\":true,\"checks\":%u,\"scenarios\":%u,\"valid_interleavings\":%u,\"rejected_cases\":%u,\"fault_positions\":%u,\"result_bytes\":%zu,\"state_bytes\":%zu,\"cpu_only\":true,\"gpu_submissions\":0}\n",checks,scenarios,valid,bad,calls,sizeof(N::Result),sizeof(R::State));
}
