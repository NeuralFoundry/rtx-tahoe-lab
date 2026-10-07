// Replays the real 0.24.1 allocation reply and checks the exact output boundary.
#if defined(_MSC_VER) && !defined(__clang__)
#define NV_DECLARE_ALIGNED(TYPE_VAR,ALIGN) __declspec(align(ALIGN)) TYPE_VAR
#endif
#include "changes/gsp-channel-0.23/reference/alloc_channel.h"
#include "changes/gsp-submit-0.24/transactions/ExecutionCodec.hpp"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
namespace C=ChannelCodec;namespace E=ExecutionCodec;namespace R=GSPComputePrep;
using Bytes=std::vector<unsigned char>;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static Bytes load(const char *p){std::ifstream f(p,std::ios::binary);CHECK(bool(f));return Bytes(std::istreambuf_iterator<char>(f),{});}
static void checksum(Bytes &b){R::put32(b.data()+32,0);unsigned sum=0,end=(48+R::get32(b.data()+56)+7)&~7U;CHECK(end<=b.size());for(unsigned i=0;i<end;i+=4)sum^=R::get32(b.data()+i);R::put32(b.data()+32,sum);}
int main(int argc,char **argv){
  static_assert(sizeof(NV_CHANNEL_ALLOC_PARAMS)==368,"channel ABI size");
  static_assert(offsetof(NV_CHANNEL_ALLOC_PARAMS,hPhysChannelGroup)==240,"group ABI output offset");
  static_assert(sizeof(NV_CHANNEL_ALLOC_PARAMS::hPhysChannelGroup)==4,"group ABI output size");
  static_assert(offsetof(NV_CHANNEL_ALLOC_PARAMS,internalFlags)==244,"neighbor remains fixed");
  CHECK(argc==3);const auto live=load(argv[1]),gr=load(argv[2]);CHECK(live.size()==4096&&gr.size()==4096);
  C::Plan golden;ExecutionPlan::Plan plan;CHECK(C::plan(gr.data()+104,1664,golden));
  C::Reply channel;CHECK(C::reply(live.data(),4096,16,0,golden,channel));CHECK(channel.accepted&&channel.channelId==3&&channel.subdeviceMask==0);
  CHECK(R::get32(live.data()+352)==0xc9f00000);
  Bytes execution(4096);CHECK(E::request(E::ChannelStep,100,golden,3,plan,execution.data(),4096));
  R::put32(execution.data()+64,0);R::put32(execution.data()+68,0);checksum(execution);
  E::Reply reply;CHECK(E::reply(execution.data(),4096,100,E::ChannelStep,golden,3,plan,reply));
  for(unsigned value:{0U,1U,0xc9f00000U,0xc9f00001U,~0U}){
    auto a=live,b=execution;R::put32(a.data()+352,value);R::put32(b.data()+352,value);checksum(a);checksum(b);
    CHECK(C::reply(a.data(),4096,16,0,golden,channel));CHECK(E::reply(b.data(),4096,100,E::ChannelStep,golden,3,plan,reply));
    CHECK(channel.channelId==3&&reply.channelId==4);CHECK(reply.rawToken==~0U);
  }
  // Every immutable parameter byte is checked even with a correct new checksum.
  unsigned immutable=0;
  for(unsigned i=0;i<368;++i){
    if((i>=132&&i<140)||(i>=240&&i<244))continue;
    ++immutable;auto a=live,b=execution;a[112+i]^=1;b[112+i]^=1;checksum(a);checksum(b);
    CHECK(!C::reply(a.data(),4096,16,0,golden,channel));CHECK(!channel.accepted);
    CHECK(!E::reply(b.data(),4096,100,E::ChannelStep,golden,3,plan,reply));CHECK(!reply.accepted);
  }
  for(unsigned offset:{32U,36U,48U,52U,56U,60U,64U,68U,72U,80U,84U,88U,92U,96U,100U,104U,108U}){
    auto a=live,b=execution;a[offset]^=1;b[offset]^=1;if(offset!=32){checksum(a);checksum(b);}
    CHECK(!C::reply(a.data(),4096,16,0,golden,channel));CHECK(!E::reply(b.data(),4096,100,E::ChannelStep,golden,3,plan,reply));
  }
  for(unsigned cid:{0U,3U,5U,~0U}){auto b=execution;R::put32(b.data()+244,cid);checksum(b);CHECK(E::reply(b.data(),4096,100,E::ChannelStep,golden,3,plan,reply)==(cid==5));}
  for(unsigned mask:{2U,~0U}){auto a=live,b=execution;R::put32(a.data()+248,mask);R::put32(b.data()+248,mask);checksum(a);checksum(b);CHECK(!C::reply(a.data(),4096,16,0,golden,channel));CHECK(!E::reply(b.data(),4096,100,E::ChannelStep,golden,3,plan,reply));}
  CHECK(immutable==356);
  std::printf("{\"passed\":true,\"checks\":%u,\"immutable_parameter_bytes\":%u,\"output_offset\":240,\"output_bytes\":4,\"real_reply_replayed\":true,\"hardware_accessed\":false,\"compute_verified\":false,\"metal_verified\":false}\n",checks,immutable);
}
