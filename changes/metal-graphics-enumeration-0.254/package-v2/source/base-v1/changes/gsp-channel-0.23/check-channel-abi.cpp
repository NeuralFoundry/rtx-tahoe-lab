// The open Linux SDK omits its MSVC spelling of this alignment macro. Preserve
// the declared alignment for Windows; clang uses NVIDIA's own definition.
#if defined(_MSC_VER) && !defined(__clang__)
#define NV_DECLARE_ALIGNED(TYPE_VAR,ALIGN) __declspec(align(ALIGN)) TYPE_VAR
#endif
#include "reference/alloc_channel.h"
#include "reference/clc56f.h"
#include "reference/clc7c0.h"
#include "reference/clc7b5.h"
#include "promote-abi-extract.hpp"
#include <cstdio>
#include <cstddef>
#include <fstream>
#include <vector>
#include <cstring>
#define FIELD(name) std::printf("\"%s\":%zu,",#name,offsetof(NV_CHANNEL_ALLOC_PARAMS,name))
int main(int argc,char**argv){
  static_assert(NV_MAX_SUBDEVICES==8,"Pinned channel ABI requires eight subdevices");
  static_assert(sizeof(NV_MEMORY_DESC_PARAMS)==24,"Pinned memory descriptor ABI");
  static_assert(sizeof(NV_CHANNEL_ALLOC_PARAMS)==368,"Pinned 570.144 channel allocation ABI");
  static_assert(AMPERE_CHANNEL_GPFIFO_A==0xc56f&&AMPERE_COMPUTE_B==0xc7c0&&AMPERE_DMA_COPY_B==0xc7b5,"Pinned Ampere classes");
  static_assert(sizeof(NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ENTRY)==32,"Promotion entry ABI");
  static_assert(sizeof(NV2080_CTRL_GPU_PROMOTE_CTX_PARAMS)==560,"Promotion parameter ABI");
  static_assert(offsetof(NV2080_CTRL_GPU_PROMOTE_CTX_PARAMS,promoteEntry)==48,"Promotion array alignment");
  if(argc==2||argc==4){
    NV_CHANNEL_ALLOC_PARAMS p{};
    p.gpFifoOffset=0x1020000000ULL;p.gpFifoEntries=32;p.flags=0x200320;p.hVASpace=0xcf000003;
    p.userdOffset[0]=0x100;p.engineType=1;p.cid=3;p.internalFlags=0x1a;
    p.instanceMem={0x01101000,0x1000,2,0};p.ramfcMem={0x01101000,0x200,2,0};
    p.userdMem={0x01100100,0x20,2,0};p.mthdbufMem={0x01102000,0x5000,2,0};
    std::ofstream f(argv[1],std::ios::binary);if(!f)return 1;
    f.write(reinterpret_cast<const char*>(&p),sizeof(p));f.close();if(!f)return 2;
  }
  if(argc==4){
    std::ifstream input(argv[2],std::ios::binary);if(!input)return 3;
    std::vector<unsigned char> raw(std::istreambuf_iterator<char>(input),{});if(raw.size()!=4096)return 4;
    auto u32=[&](unsigned off){NvU32 v=0;for(unsigned j=0;j<4;++j)v|=NvU32(raw[off+j])<<(j*8);return v;};
    if(u32(36)!=11||u32(60)!=76||u32(64)||u32(68)||u32(88)!=0x20800a32||u32(92)||u32(96)!=1664)return 5;
    unsigned sum=0;for(unsigned off=0;off<1768;off+=4)sum^=u32(off);if(sum)return 6;
    NV2080_CTRL_GPU_PROMOTE_CTX_PARAMS p{};p.engineType=1;p.hChanClient=0xc1e00004;p.hObject=0xcf000004;p.entryCount=9;
    const unsigned ids[]={0,2,3,4,5,6,9,10,11},kinds[]={0,16,17,18,19,20,23,24,24};
    NvU64 physical=0x01200000,virt=0x1020200000ULL;
    auto align=[](NvU64 value,NvU64 amount){return (value+amount-1)&~(amount-1);};
    for(unsigned i=0;i<9;++i){
      NvU64 amount=u32(104+kinds[i]*8),boundary=u32(108+kinds[i]*8);
      if(!amount||amount==0xffffffff||!boundary||boundary>0x200000||(boundary&(boundary-1)))return 7;
      if(i==0)amount+=0x40000;if(ids[i]==5&&boundary<0x200000)boundary=0x200000;
      amount=align(amount,boundary);if(amount>0x1000000)return 8;
      const NvU64 allocated=align(amount,4096);if(boundary<4096)boundary=4096;
      physical=align(physical,boundary);virt=align(virt,boundary);if(physical+allocated>0x4000000)return 9;
      const bool usePhys=i<2||i>=6,useVirt=ids[i]!=10;
      auto&e=p.promoteEntry[i];e.gpuPhysAddr=usePhys?physical:0;e.gpuVirtAddr=useVirt?virt:0;e.size=usePhys?amount:0;
      e.physAttr=usePhys?4:0;e.bufferId=NvU16(ids[i]);e.bInitialize=usePhys;e.bNonmapped=usePhys&&!useVirt;
      physical+=allocated;virt+=allocated;
    }
    std::ofstream f(argv[3],std::ios::binary);if(!f)return 10;
    f.write(reinterpret_cast<const char*>(&p),sizeof(p));f.close();if(!f)return 11;
  }
  std::printf("{\"channel_bytes\":%zu,\"memory_desc_bytes\":%zu,",sizeof(NV_CHANNEL_ALLOC_PARAMS),sizeof(NV_MEMORY_DESC_PARAMS));
  FIELD(gpFifoOffset);FIELD(gpFifoEntries);FIELD(flags);FIELD(hVASpace);FIELD(hUserdMemory);FIELD(userdOffset);
  FIELD(engineType);FIELD(cid);FIELD(subDeviceId);FIELD(instanceMem);FIELD(userdMem);FIELD(ramfcMem);FIELD(mthdbufMem);
  FIELD(internalFlags);FIELD(errorNotifierMem);FIELD(eccErrorNotifierMem);FIELD(ProcessID);FIELD(tpcConfigID);
  std::printf("\"promotion_bytes\":%zu,\"promotion_entry_bytes\":%zu,\"promotion_entry_offset\":%zu,",sizeof(NV2080_CTRL_GPU_PROMOTE_CTX_PARAMS),sizeof(NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ENTRY),offsetof(NV2080_CTRL_GPU_PROMOTE_CTX_PARAMS,promoteEntry));
  std::puts("\"hardware_accessed\":false}");
}
