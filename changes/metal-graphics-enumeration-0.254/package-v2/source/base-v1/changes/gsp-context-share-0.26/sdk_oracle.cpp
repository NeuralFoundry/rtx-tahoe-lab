#include <cstdio>
#include <cstddef>
#include <cstring>
#include <utility>
#include <initializer_list>
#if defined(_MSC_VER) && !defined(__clang__)
// The published Linux SDK omits its documented MSVC branch. Supply the
// equivalent field alignment only in this oracle; the SDK remains unchanged.
#define NV_DECLARE_ALIGNED(TYPE_VAR,ALIGN) __declspec(align(ALIGN)) TYPE_VAR
#endif
#include "nvos.h"
#include "class/cl9067.h"
#include "class/cla06c.h"
#include "ctrl/ctrla06c.h"
// This program deliberately does not include ContextSharePlan.hpp.
static_assert(sizeof(NV_CHANNEL_GROUP_ALLOCATION_PARAMETERS)==20,"Group ABI");
static_assert(offsetof(NV_CHANNEL_GROUP_ALLOCATION_PARAMETERS,engineType)==12,"Group engine offset");
static_assert(offsetof(NV_CHANNEL_GROUP_ALLOCATION_PARAMETERS,bIsCallingContextVgpuPlugin)==16,"Group bool offset");
static_assert(sizeof(NV_CTXSHARE_ALLOCATION_PARAMETERS)==12,"Share ABI");
static_assert(offsetof(NV_CTXSHARE_ALLOCATION_PARAMETERS,subctxId)==8,"Share output offset");
static_assert(sizeof(NV_CHANNELGPFIFO_ALLOCATION_PARAMETERS)==368,"Channel ABI");
static_assert(offsetof(NV_CHANNELGPFIFO_ALLOCATION_PARAMETERS,hContextShare)==24,"Channel share offset");
static_assert(offsetof(NV_CHANNELGPFIFO_ALLOCATION_PARAMETERS,cid)==132,"Session CID offset");
static_assert(sizeof(NVA06C_CTRL_GPFIFO_SCHEDULE_PARAMS)==2,"Schedule ABI");
static_assert(NV_CTXSHARE_ALLOCATION_FLAGS_SUBCONTEXT_ASYNC==1,"Async flag");
static_assert(FERMI_CONTEXT_SHARE_A==0x9067&&KEPLER_CHANNEL_GROUP_A==0xa06c,"Class IDs");
static_assert(NVA06C_CTRL_CMD_GPFIFO_SCHEDULE==0xa06c0101,"Schedule control");
static bool save(const char *path,const void *p,size_t n){
 FILE *f=std::fopen(path,"wb");if(!f)return false;const bool ok=std::fwrite(p,1,n,f)==n;return std::fclose(f)==0&&ok;
}
int main(int argc,char **argv){
 if(argc!=2)return 1;
 NV_CHANNEL_GROUP_ALLOCATION_PARAMETERS group;std::memset(&group,0,sizeof(group));group.engineType=1;
 NV_CTXSHARE_ALLOCATION_PARAMETERS share;std::memset(&share,0,sizeof(share));
 share.hVASpace=0xcf000003;share.flags=NV_CTXSHARE_ALLOCATION_FLAGS_SUBCONTEXT_ASYNC;
 NV_CHANNELGPFIFO_ALLOCATION_PARAMETERS channel;std::memset(&channel,0,sizeof(channel));
 channel.gpFifoOffset=0x1020003000ULL;channel.gpFifoEntries=32;channel.flags=0x200420;
 channel.hContextShare=0xcf00000b;channel.hVASpace=0;channel.userdOffset[0]=0x800;
 channel.engineType=0;channel.cid=4;channel.internalFlags=0x14;
 channel.instanceMem.base=0x03401000;channel.instanceMem.size=0x1000;channel.instanceMem.addressSpace=2;
 channel.userdMem.base=0x03400800;channel.userdMem.size=512;channel.userdMem.addressSpace=2;
 channel.ramfcMem.base=0x03401000;channel.ramfcMem.size=512;channel.ramfcMem.addressSpace=2;
 channel.mthdbufMem.base=0x03404000;channel.mthdbufMem.size=0x5000;channel.mthdbufMem.addressSpace=2;
 NVA06C_CTRL_GPFIFO_SCHEDULE_PARAMS schedule;std::memset(&schedule,0,sizeof(schedule));schedule.bEnable=1;
 unsigned char output[402];unsigned off=0;
 for(const auto &part : {std::pair<const void*,size_t>{&group,sizeof(group)}, {&share,sizeof(share)}, {&channel,sizeof(channel)}, {&schedule,sizeof(schedule)}}){
  std::memcpy(output+off,part.first,part.second);off+=static_cast<unsigned>(part.second);
 }
 if(off!=sizeof(output)||!save(argv[1],output,sizeof(output)))return 2;
 std::printf("{\"passed\":true,\"sdk_abi\":true,\"payload_bytes\":402,\"hardware_accessed\":false}\n");
}
