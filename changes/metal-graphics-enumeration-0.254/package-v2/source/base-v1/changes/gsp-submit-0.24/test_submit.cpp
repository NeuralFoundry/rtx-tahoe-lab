#include "SubmitCodec.hpp"
#include "vendor-extract.hpp"
#include "../gsp-channel-0.23/reference/clc56f.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <fstream>
namespace S=SubmitCodec;using Bytes=std::vector<unsigned char>;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line%d:%s\n",__LINE__,#x);std::abort();}}while(0)
#define FIELD(v,f) (((v)>>(0?f))&((1ULL<<((1?f)-(0?f)+1))-1))
static_assert(sizeof(NVA06F_CTRL_BIND_PARAMS)==4&&sizeof(NVA06F_CTRL_GPFIFO_SCHEDULE_PARAMS)==2,"SDK control sizes");
static_assert(offsetof(NVA06F_CTRL_GPFIFO_SCHEDULE_PARAMS,bSkipSubmit)==1,"SDK schedule bytes");
static_assert(sizeof(NVC36F_CTRL_CMD_GPFIFO_GET_WORK_SUBMIT_TOKEN_PARAMS)==4,"SDK token size");
static_assert(sizeof(AmpereAControlGPFifo)==512&&offsetof(AmpereAControlGPFifo,GPPut)==0x8c&&offsetof(AmpereAControlGPFifo,GPGet)==0x88,"Full SDK USERD layout");
static_assert(S::Controls[0]==NVA06F_CTRL_CMD_BIND&&S::Controls[1]==NVA06F_CTRL_CMD_GPFIFO_SCHEDULE&&S::Controls[2]==NVC36F_CTRL_CMD_GPFIFO_GET_WORK_SUBMIT_TOKEN,"SDK opcodes");
static void seal(Bytes &p){S::R::put32(p.data()+32,0);unsigned sum=0;for(unsigned i=0;i<((48+S::R::get32(p.data()+56)+7)&~7U);i+=4)sum^=S::R::get32(p.data()+i);S::R::put32(p.data()+32,sum);}
int main(int argc,char **argv){
  CHECK(argc==2);Bytes fixture(3*4096+28),packet(4096);S::Reply reply;
  for(unsigned step=0;step<3;++step){
    CHECK(S::request(step,20+step,fixture.data()+step*4096,4096));
    packet.assign(fixture.begin()+step*4096,fixture.begin()+(step+1)*4096);
    CHECK(!S::reply(packet.data(),4096,20+step,step,reply));
    S::R::put32(packet.data()+64,0);S::R::put32(packet.data()+68,0);
    if(step==2)S::R::put32(packet.data()+104,37);seal(packet);
    CHECK(S::reply(packet.data(),4096,20+step,step,reply)&&reply.accepted);
    if(step==2)CHECK(reply.rawToken==37);
    for(unsigned off:{36U,40U,44U,48U,52U,56U,60U,64U,68U,72U,76U,80U,84U,88U,92U,96U,100U}){
      auto bad=packet;bad[off]^=1;seal(bad);CHECK(!S::reply(bad.data(),4096,20+step,step,reply));}
    CHECK(!S::reply(packet.data(),4095,20+step,step,reply));
    auto bad=packet;bad[110]^=1;CHECK(!S::reply(bad.data(),4096,20+step,step,reply));
    if(step<2){bad=packet;bad[104]^=1;seal(bad);CHECK(!S::reply(bad.data(),4096,20+step,step,reply));}
    else for(unsigned token:{0U,1U,37U,0x12345678U,0xfffffffeU,0xffffffffU}){
      bad=packet;S::R::put32(bad.data()+104,token);seal(bad);CHECK(S::reply(bad.data(),4096,22,2,reply)==(token!=~0U));}
  }
  CHECK(!S::request(3,20,packet.data(),4096));CHECK(!S::request(0,~0U,packet.data(),4096));CHECK(!S::request(0,20,packet.data(),4095));
  CHECK(!S::userd(0x100,0x20,4096));CHECK(S::userd(0x100,512,4096));CHECK(!S::userd(3588,512,4096));CHECK(!S::userd(1,512,4096));
  S::U64 entry=0;
  for(S::U64 address:{4ULL,0xfffffff0ULL,S::CommandVA,(1ULL<<40)-4096})for(unsigned size=4;size<=4096;size+=4){
    CHECK(S::entry(address,size,entry));const unsigned lo=unsigned(entry),hi=unsigned(entry>>32);
    CHECK(FIELD(lo,NVC56F_GP_ENTRY0_GET)==((address&0xffffffff)>>2));
    CHECK(FIELD(hi,NVC56F_GP_ENTRY1_GET_HI)==address>>32);
    CHECK(FIELD(hi,NVC56F_GP_ENTRY1_LENGTH)==size/4);
    CHECK(FIELD(hi,NVC56F_GP_ENTRY1_LEVEL)==NVC56F_GP_ENTRY1_LEVEL_SUBROUTINE);
    CHECK(FIELD(hi,NVC56F_GP_ENTRY1_SYNC)==NVC56F_GP_ENTRY1_SYNC_PROCEED);
  }
  for(S::U64 address:{0ULL,1ULL,1ULL<<40})CHECK(!S::entry(address,4,entry)&&entry==0);
  for(unsigned size:{0U,3U,4100U})CHECK(!S::entry(4,size,entry)&&entry==0);
  CHECK(!S::entry((1ULL<<40)-4,8,entry));
  unsigned header=0;for(unsigned sub=0;sub<8;++sub)for(unsigned count=1;count<=1024;++count){
    CHECK(S::increment(0x10,sub,count,header));CHECK(FIELD(header,NVC56F_DMA_INCR_ADDRESS)==4);
    CHECK(FIELD(header,NVC56F_DMA_INCR_SUBCHANNEL)==sub);CHECK(FIELD(header,NVC56F_DMA_INCR_COUNT)==count);
    CHECK(FIELD(header,NVC56F_DMA_INCR_OPCODE)==NVC56F_DMA_INCR_OPCODE_VALUE);
  }
  CHECK(!S::increment(1,0,1,header));CHECK(!S::increment(0,8,1,header));CHECK(!S::increment(0,0,0,header));CHECK(!S::increment(0x3ffc,0,2,header));
  CHECK(S::fence(fixture.data()+12288,20));CHECK(!S::fence(packet.data(),19));CHECK(S::entry(S::CommandVA,20,entry));
  ChannelCodec::put64(fixture.data()+12288+20,entry);
  const auto semaphore=S::R::get32(fixture.data()+12288+16);
  CHECK(FIELD(semaphore,NVC56F_SEMAPHORED_OPERATION)==NVC56F_SEMAPHORED_OPERATION_RELEASE);
  CHECK(FIELD(semaphore,NVC56F_SEMAPHORED_RELEASE_SIZE)==NVC56F_SEMAPHORED_RELEASE_SIZE_4BYTE);
  CHECK(FIELD(semaphore,NVC56F_SEMAPHORED_RELEASE_WFI)==NVC56F_SEMAPHORED_RELEASE_WFI_EN);
  std::ofstream f(argv[1],std::ios::binary);CHECK(bool(f));f.write(reinterpret_cast<const char*>(fixture.data()),std::streamsize(fixture.size()));CHECK(bool(f));
  std::printf("{\"passed\":true,\"checks\":%u,\"control_sizes\":[4,2,4],\"userd_bytes\":512,\"gpput_offset\":140,\"hardware_accessed\":false,\"doorbell_ready\":false,\"compute_verified\":false}\n",checks);
}
