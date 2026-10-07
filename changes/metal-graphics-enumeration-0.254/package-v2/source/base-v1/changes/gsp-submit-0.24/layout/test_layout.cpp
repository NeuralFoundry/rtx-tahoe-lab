#if defined(_MSC_VER) && !defined(__clang__)
#define NV_DECLARE_ALIGNED(TYPE_VAR,ALIGN) __declspec(align(ALIGN)) TYPE_VAR
#endif
#include "../../gsp-channel-0.23/reference/alloc_channel.h"
#include "../../gsp-channel-0.23/promote-abi-extract.hpp"
#include "vendor-internal-flags.hpp"
#include "ExecutionTables.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
namespace P=ExecutionPlan;namespace T=ExecutionTables;namespace C=ChannelCodec;namespace L=GMMULeaves;namespace R=GSPComputePrep;
using Bytes=std::vector<unsigned char>;
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"line %u: %s\n",unsigned(__LINE__),#x);std::exit(1);}}while(0)
#define LOW(field) (0?field)
#define HIGH(field) (1?field)
#define SET(field,value) (unsigned(value)<<LOW(field))
static void save(const std::string &dir,const char *name,const void *data,size_t size){
  std::ofstream f(dir+"/"+name,std::ios::binary);CHECK(bool(f));f.write(static_cast<const char*>(data),size);f.close();CHECK(bool(f));
}
struct Tree {
  Bytes root=Bytes(L::OldBytes),child=Bytes(L::MaxChildBytes),scratch=Bytes(L::OldBytes),expected=Bytes(L::MaxChildBytes),out=Bytes(L::MaxChildBytes,0x5a);
  unsigned childBytes=0;T::Result result;
  explicit Tree(const C::Plan &golden){
    L::write64(root.data(),0x100322);L::write64(root.data()+4096,0x100422);
    L::write64(root.data()+8192+128*8,0x1122334455667788ULL);
    L::Range ranges[10];L::Result r;CHECK(C::mappingRanges(golden,ranges));
    CHECK(L::build(root.data(),unsigned(root.size()),ranges,10,child.data(),unsigned(child.size()),r));childBytes=unsigned(r.childBytes);child.resize(childBytes);
    L::write64(root.data()+L::ParentOffset,L::ParentValue);
  }
  bool merge(const C::Plan &g,const P::Plan &p,unsigned capacity=unsigned(L::MaxChildBytes)){
    return T::merge(root.data(),unsigned(root.size()),child.data(),childBytes,g,p,scratch.data(),expected.data(),out.data(),capacity,result);
  }
  void rejected(const C::Plan &g,const P::Plan &p,unsigned capacity=unsigned(L::MaxChildBytes)){
    std::fill(out.begin(),out.end(),static_cast<unsigned char>(0x5a));const auto beforeRoot=root,beforeChild=child;
    CHECK(!merge(g,p,capacity));CHECK(!result.valid);CHECK(root==beforeRoot&&child==beforeChild);
    CHECK(std::all_of(out.begin(),out.end(),[](unsigned char v){return v==0x5a;}));
  }
};
static void walkRanges(const Tree &t,const L::Range *ranges,unsigned count){
  for(unsigned i=0;i<count;++i)for(P::U64 off=0;off<ranges[i].bytes;off+=4096){
    for(unsigned edge:{0U,4095U}){bool mapped=false;uint64_t physical=0;
      CHECK(L::walk(t.root.data(),t.root.size(),t.out.data(),t.result.childBytes,ranges[i].va+off+edge,mapped,physical));
      CHECK(mapped&&physical==ranges[i].pa+off+edge);
    }
  }
}
static void testABI(const C::Plan &g,const P::Plan &p,Bytes &channel,Bytes &physical,Bytes &virt){
  static_assert(sizeof(NV_CHANNEL_ALLOC_PARAMS)==368&&sizeof(NV_MEMORY_DESC_PARAMS)==24,"channel SDK ABI");
  static_assert(sizeof(NV2080_CTRL_GPU_PROMOTE_CTX_PARAMS)==560&&sizeof(NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ENTRY)==32,"promotion SDK ABI");
  static_assert(HIGH(NVOS04_FLAGS_CHANNEL_USERD_INDEX_VALUE)==10&&LOW(NVOS04_FLAGS_CHANNEL_USERD_INDEX_VALUE)==8,"slot ABI");
  const unsigned flags=SET(NVOS04_FLAGS_CHANNEL_USERD_INDEX_VALUE,4)|SET(NVOS04_FLAGS_CHANNEL_USERD_INDEX_PAGE_FIXED,1)|0x20;
  const unsigned internal=SET(NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_PRIVILEGE,NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_PRIVILEGE_USER)|
    SET(NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_ERROR_NOTIFIER_TYPE,ERROR_NOTIFIER_TYPE_NONE)|
    SET(NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_ECC_ERROR_NOTIFIER_TYPE,ERROR_NOTIFIER_TYPE_NONE);
  static_assert(flags==P::Flags&&internal==P::InternalFlags,"SDK channel flag fields");
  NV_CHANNEL_ALLOC_PARAMS sdk{};sdk.gpFifoOffset=0x1020003000ULL;sdk.gpFifoEntries=32;sdk.flags=flags;sdk.hVASpace=0;sdk.hContextShare=0xcf00000b;
  sdk.userdOffset[0]=4*512;sdk.engineType=0;sdk.cid=4;sdk.internalFlags=internal;
  sdk.instanceMem={0x03401000,4096,2,0};sdk.ramfcMem={0x03401000,512,2,0};sdk.userdMem={0x03400800,512,2,0};sdk.mthdbufMem={0x03404000,0x5000,2,0};
  CHECK(P::channelParameters(g,3,channel.data(),unsigned(channel.size())));CHECK(std::memcmp(channel.data(),&sdk,368)==0);
  CHECK(SubmitCodec::userd(unsigned(sdk.userdOffset[0]),unsigned(sdk.userdMem.size),4096));
  CHECK(sdk.userdMem.base+0x8c+4<=0x03401000);
  for(bool mode:{true,false}){
    auto &bytes=mode?physical:virt;NV2080_CTRL_GPU_PROMOTE_CTX_PARAMS promote{};
    promote.engineType=1;promote.hChanClient=0xc1000000;promote.hObject=0xcf000007;promote.entryCount=mode?3:6;
    for(unsigned i=0;i<3;++i){auto &e=promote.promoteEntry[i];const auto &b=p.buffers[i];
      e.gpuPhysAddr=mode?b.physical:0;e.gpuVirtAddr=mode?0:b.va;e.size=mode?b.bytes:0;e.physAttr=mode?4:0;e.bufferId=NvU16(i);e.bInitialize=mode;e.bNonmapped=mode;
    }
    if(!mode)for(unsigned i=0;i<3;++i){auto &e=promote.promoteEntry[3+i];const auto &b=g.buffers[6+i];
      e.gpuVirtAddr=b.virtualAddress;e.bufferId=NvU16(b.id);
    }
    CHECK(P::promotion(p,g,mode,bytes.data(),unsigned(bytes.size())));CHECK(std::memcmp(bytes.data(),&promote,560)==0);
  }
}
int main(int argc,char **argv){
  CHECK(argc==3);std::ifstream f(argv[1],std::ios::binary);CHECK(bool(f));Bytes record(std::istreambuf_iterator<char>(f),{});
  CHECK(record.size()==4096);GSPInitEvents::Record decoded;
  CHECK(GSPInitEvents::decode(record.data(),unsigned(record.size()),11,decoded));
  CHECK(decoded.function==76&&!decoded.result&&R::get32(record.data()+68)==0&&R::get32(record.data()+80)==R::Client&&
    R::get32(record.data()+84)==R::Subdevice&&R::get32(record.data()+88)==0x20800a32&&R::get32(record.data()+96)==1664);
  Bytes gr(record.begin()+104,record.begin()+104+1664);C::Plan golden;P::Plan p;
  CHECK(C::plan(gr.data(),unsigned(gr.size()),golden));CHECK(P::make(gr.data(),unsigned(gr.size()),golden,p));
  CHECK(p.buffers[0].bytes==970752&&p.buffers[1].bytes==16384&&p.buffers[2].bytes==16384&&p.backingBytes==1003520);
  CHECK(p.physicalEnd==P::ContextBase+1003520&&p.virtualEnd==P::ContextVA+1003520);
  Bytes channel(368),physical(560),virt(560);testABI(golden,p,channel,physical,virt);
  Tree t(golden);const auto rootBefore=t.root,childBefore=t.child;CHECK(t.merge(golden,p));
  CHECK(t.root==rootBefore&&t.child==childBefore);
  CHECK(t.result.valid&&t.result.oldBytes==36864&&t.result.childBytes==40960&&t.result.addedLeafPages==1&&t.result.addedPtes==248&&t.result.existingTablePtes==3);
  L::Range oldRanges[10],newRanges[6];CHECK(C::mappingRanges(golden,oldRanges)&&P::mappings(p,golden,newRanges));
  walkRanges(t,oldRanges,10);walkRanges(t,newRanges,6);
  for(P::U64 va:{L::VABase+0x4000,P::ContextVA-4096,p.virtualEnd,L::VAEnd-1}){
    bool mapped=true;uint64_t pa=123;CHECK(L::walk(t.root.data(),t.root.size(),t.out.data(),t.result.childBytes,va,mapped,pa));CHECK(!mapped);
  }
  // Existing mappings, sparse holes, GSP-owned root entry and fixed reservations
  // survive. Only empty slots1/2/3 and PDE0group16 change in the old children.
  for(unsigned off=0;off<t.childBytes;++off){bool allowed=(off>=16*16&&off<16*16+16)||(off>=4096+8&&off<4096+32);
    if(!allowed)CHECK(t.child[off]==t.out[off]);}
  CHECK(L::read64(t.root.data()+8192+128*8)==0x1122334455667788ULL);
  save(argv[2],"channel.bin",channel.data(),channel.size());save(argv[2],"physical.bin",physical.data(),physical.size());save(argv[2],"virtual.bin",virt.data(),virt.size());
  save(argv[2],"root.bin",t.root.data(),t.root.size());save(argv[2],"golden-children.bin",t.child.data(),t.childBytes);save(argv[2],"children.bin",t.out.data(),t.result.childBytes);
  Bytes ranges(6*24);for(unsigned i=0;i<6;++i){L::write64(ranges.data()+i*24,newRanges[i].va);L::write64(ranges.data()+i*24+8,newRanges[i].pa);L::write64(ranges.data()+i*24+16,newRanges[i].bytes);}
  save(argv[2],"ranges.bin",ranges.data(),ranges.size());
  const auto defaultResult=t.result;
  // Every word of the captured golden tree is checked, including zero padding.
  for(unsigned off=0;off<t.childBytes;off+=8){t.child[off]^=1;t.rejected(golden,p);t.child[off]^=1;}
  for(unsigned off:{0U,4096U,unsigned(L::ParentOffset)}){t.root[off]^=1;t.rejected(golden,p);t.root[off]^=1;}
  for(unsigned cap:{0U,8191U,36863U,36864U,40959U,45057U})t.rejected(golden,p,cap);
  const auto goodSize=t.childBytes;for(unsigned size:{0U,8191U,36863U,45057U}){t.childBytes=size;
    // Invalid lengths reject before reading beyond the actual captured image.
    CHECK(!t.merge(golden,p));CHECK(!t.result.valid);}t.childBytes=goodSize;
  CHECK(!T::merge(nullptr,12288,t.child.data(),t.childBytes,golden,p,t.scratch.data(),t.expected.data(),t.out.data(),45056,t.result));
  CHECK(!T::merge(t.root.data(),12288,nullptr,t.childBytes,golden,p,t.scratch.data(),t.expected.data(),t.out.data(),45056,t.result));
  CHECK(!T::merge(t.root.data(),12288,t.child.data(),t.childBytes,golden,p,t.root.data(),t.expected.data(),t.out.data(),45056,t.result));
  CHECK(t.root==rootBefore);
  CHECK(!T::merge(t.root.data(),12288,t.child.data(),t.childBytes,golden,p,t.scratch.data(),t.out.data(),t.out.data(),45056,t.result));
  CHECK(!T::merge(t.root.data(),12288,t.child.data(),t.childBytes,golden,p,t.scratch.data(),t.expected.data(),t.child.data(),t.childBytes,t.result));
  CHECK(t.child==childBefore);
  auto *aliasedResult=reinterpret_cast<T::Result*>(t.out.data());const auto beforeAliased=t.out;
  CHECK(!T::merge(t.root.data(),12288,t.child.data(),t.childBytes,golden,p,t.scratch.data(),t.expected.data(),t.out.data(),45056,*aliasedResult));CHECK(t.out==beforeAliased);
  for(unsigned cid:{0U,1U,2U,4U,4095U,~0U}){std::fill(channel.begin(),channel.end(),static_cast<unsigned char>(0xa5));CHECK(!P::channelParameters(golden,cid,channel.data(),368));CHECK(channel[0]==0xa5);}
  CHECK(!P::channelParameters(golden,3,channel.data(),367));CHECK(!P::channelParameters(golden,3,nullptr,368));
  CHECK(!P::promotion(p,golden,true,physical.data(),559));CHECK(!P::promotion(p,golden,false,nullptr,560));
  auto g=golden;g.physicalEnd++;CHECK(!P::goldenFits(g));t.rejected(g,p);
  g=golden;g.buffers[0].kind++;CHECK(!P::goldenFits(g));
  g=golden;g.buffers[8].physical=P::Base;CHECK(!P::goldenFits(g));
  for(unsigned i=0;i<3;++i)for(unsigned field=0;field<7;++field){auto bad=p;auto &b=bad.buffers[i];
    switch(field){case 0:++b.id;break;case 1:++b.kind;break;case 2:++b.allocated;break;case 3:++b.alignment;break;case 4:++b.physical;break;case 5:++b.va;break;case 6:b.bytes=0;break;}
    CHECK(!P::valid(bad,golden));t.rejected(golden,bad);
  }
  auto bad=p;bad.backingBytes++;CHECK(!P::valid(bad,golden));bad=p;bad.valid=false;CHECK(!P::valid(bad,golden));
  const auto pSaved=p;
  CHECK(!P::make(reinterpret_cast<const unsigned char*>(&p),1664,golden,p));CHECK(std::memcmp(&p,&pSaved,sizeof(p))==0);
  CHECK(!P::mappings(p,golden,*reinterpret_cast<L::Range(*)[6]>(&p)));CHECK(std::memcmp(&p,&pSaved,sizeof(p))==0);
  CHECK(!P::promotion(p,golden,true,reinterpret_cast<unsigned char*>(&p),560));CHECK(std::memcmp(&p,&pSaved,sizeof(p))==0);
  for(unsigned kind:{0U,16U})for(unsigned value:{0U,3U,0x400000U,~0U}){
    auto altered=gr;R::put32(altered.data()+kind*8+4,value);P::Plan failed;
    CHECK(!P::make(altered.data(),1664,golden,failed));CHECK(!failed.valid);
  }
  for(unsigned kind:{0U,16U})for(unsigned value:{0U,~0U,0x1000001U}){
    auto altered=gr;R::put32(altered.data()+kind*8,value);P::Plan failed;CHECK(!P::make(altered.data(),1664,golden,failed));CHECK(!failed.valid);
  }
  for(unsigned total:{0x200000U,0x201000U,0x400000U,0x401000U}){
    auto altered=gr;R::put32(altered.data(),total-2*16384-0x40000);P::Plan resized;
    CHECK(P::make(altered.data(),1664,golden,resized));CHECK(resized.backingBytes==total);
    if(total>0x400000)t.rejected(golden,resized);
    else{CHECK(t.merge(golden,resized));CHECK(t.result.addedLeafPages==(total>0x200000?2U:1U));CHECK(P::mappings(resized,golden,newRanges));walkRanges(t,oldRanges,10);walkRanges(t,newRanges,6);}
  }
  // Larger reported alignment and sub-page descriptor sizes remain bounded.
  for(unsigned align:{256U,4096U,65536U,0x200000U}){auto altered=gr;R::put32(altered.data()+4,align);R::put32(altered.data()+16*8,16385);P::Plan resized;
    CHECK(P::make(altered.data(),1664,golden,resized));CHECK(P::valid(resized,golden));}
  std::printf("{\"passed\":true,\"checks\":%u,\"golden_child_bytes\":%u,\"child_bytes\":%u,\"added_leaf_pages\":%u,\"added_ptes\":%u,\"existing_table_ptes\":%u,\"private_bytes\":%llu,\"physical_end\":%llu,\"virtual_end\":%llu,\"hardware_accessed\":false,\"gpu_translation_verified\":false,\"compute_verified\":false,\"metal_verified\":false}\n",
    checks,defaultResult.oldBytes,defaultResult.childBytes,defaultResult.addedLeafPages,defaultResult.addedPtes,defaultResult.existingTablePtes,p.backingBytes,p.physicalEnd,p.virtualEnd);
}
