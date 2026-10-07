#pragma once
#include "../../gsp-channel-0.23/transactions/ChannelCodec.hpp"
#include "../SubmitCodec.hpp"
#include "../../gsp-context-share-0.26/ContextSharePlan.hpp"

// Candidate execution-channel layout only. Allocation/publication requires
// same-run golden success and separate native owner/lease checks.
namespace ExecutionPlan {
namespace C=ChannelCodec;namespace R=GSPComputePrep;namespace L=GMMULeaves;
using U64=unsigned long long;
constexpr unsigned Channel=0xcf000007U,Compute=0xcf000008U,Copy=0xcf000009U,Graphics=0xcf00000cU;
static_assert(Graphics!=Channel&&Graphics!=Compute&&Graphics!=Copy&&Graphics!=ContextSharePlan::Group&&Graphics!=ContextSharePlan::Share,"Distinct graphics object");
constexpr unsigned Group=ContextSharePlan::Group,Share=ContextSharePlan::Share;
constexpr unsigned Client=ExternalVAS::Client,Device=ExternalVAS::Device,Subdevice=ExternalVAS::Subdevice,Vaspace=ExternalVAS::Vaspace;
static_assert(ContextSharePlan::Client==Client&&ContextSharePlan::Device==Device&&ContextSharePlan::Vaspace==Vaspace,"External context share owner profile");
constexpr U64 Base=0x03400000,FixedEnd=Base+0x9000,ContextBase=Base+0x10000;
constexpr U64 RingVA=0x1020003000ULL,ContextVA=0x1022000000ULL;
// The USERD flags select hardware ChID4; allocation cid is an independent
// RM-session unique output (kernel_channel.c currentChannelUniqueId).
// Preserve the old request cid input bytes; never use that output as routing.
constexpr unsigned HardwareChannelId=4,AllocationCidInput=4,Flags=0x200420,InternalFlags=0x14;
static_assert(((Flags>>8)&7)+(((Flags>>12)&511)*8)==HardwareChannelId,"Fixed USERD hardware channel");
inline bool sessionId(unsigned cid,unsigned predecessor=3){return cid&&cid!=~0U&&cid!=predecessor;}
struct Buffer {unsigned id=0,kind=0;U64 bytes=0,allocated=0,alignment=0,physical=0,va=0;};
struct Plan {bool valid=false;Buffer buffers[3];U64 physicalEnd=0,virtualEnd=0,backingBytes=0;};
inline bool disjoint(const void *a,size_t an,const void *b,size_t bn){
  const auto x=reinterpret_cast<uintptr_t>(a),y=reinterpret_cast<uintptr_t>(b);
  return a&&b&&x<=UINTPTR_MAX-an&&y<=UINTPTR_MAX-bn&&!(x<y+bn&&y<x+an);
}
inline bool goldenFits(const C::Plan &g){return C::planValid(g)&&g.physicalEnd<=Base&&g.virtualEnd<=ContextVA;}
inline bool valid(const Plan &p,const C::Plan &g){
  if(!p.valid||!goldenFits(g))return false;
  U64 pa=ContextBase,va=ContextVA,total=0;
  for(unsigned i=0;i<3;++i){const auto &b=p.buffers[i];
    if(b.id!=i||b.kind!=(i?16U:0U)||!b.bytes||b.bytes>0x1000000||(!i&&b.bytes<=0x40000)||
       b.alignment<4096||b.alignment>0x200000||(b.alignment&(b.alignment-1))||b.allocated!=C::align(b.bytes,4096)||
       b.physical!=C::align(pa,b.alignment)||b.va!=C::align(va,b.alignment))return false;
    if(b.physical>=L::BAR1End||b.allocated>L::BAR1End-b.physical||b.va>=L::VAEnd||b.allocated>L::VAEnd-b.va)return false;
    pa=b.physical+b.allocated;va=b.va+b.allocated;total+=b.allocated;
  }
  if(p.buffers[1].bytes!=p.buffers[2].bytes||p.buffers[1].alignment!=p.buffers[2].alignment)return false;
  return p.physicalEnd==pa&&p.virtualEnd==va&&p.backingBytes==total;
}
inline bool make(const unsigned char *freshGR,unsigned bytes,const C::Plan &golden,Plan &out){
  if(!disjoint(freshGR,bytes,&out,sizeof(out))||!disjoint(&golden,sizeof(golden),&out,sizeof(out)))return false;
  out={};if(bytes!=1664||!goldenFits(golden))return false;
  U64 pa=ContextBase,va=ContextVA;
  for(unsigned i=0;i<3;++i){const unsigned kind=i?16:0;
    U64 amount=R::get32(freshGR+kind*8),alignment=R::get32(freshGR+kind*8+4);
    if(!amount||amount==0xffffffff||!alignment||alignment>0x200000||(alignment&(alignment-1)))return false;
    if(!i)amount+=0x40000;amount=C::align(amount,alignment);
    if(amount>0x1000000)return false;const U64 allocated=C::align(amount,4096);
    if(alignment<4096)alignment=4096;pa=C::align(pa,alignment);va=C::align(va,alignment);
    if(pa>=L::BAR1End||allocated>L::BAR1End-pa||va>=L::VAEnd||allocated>L::VAEnd-va)return false;
    out.buffers[i]={i,kind,amount,allocated,alignment,pa,va};pa+=allocated;va+=allocated;out.backingBytes+=allocated;
  }
  out.physicalEnd=pa;out.virtualEnd=va;out.valid=true;return valid(out,golden);
}
inline bool channelParameters(const C::Plan &golden,unsigned goldenChannelId,unsigned char *out,unsigned capacity){
  if(!out||capacity<368||goldenChannelId!=3||!goldenFits(golden)||!disjoint(&golden,sizeof(golden),out,368))return false;
  for(unsigned i=0;i<368;++i)out[i]=0;
  C::put64(out+8,RingVA);R::put32(out+16,32);R::put32(out+20,Flags);R::put32(out+24,Share);R::put32(out+28,0); // Explicit context share supplies the VA space.
  C::put64(out+64,0x800);R::put32(out+128,0);R::put32(out+132,AllocationCidInput);R::put32(out+244,InternalFlags);
  constexpr unsigned offsets[4]={144,168,192,216};
  constexpr U64 physical[4]={Base+0x1000,Base+0x800,Base+0x1000,Base+0x4000},sizes[4]={0x1000,512,512,0x5000};
  for(unsigned i=0;i<4;++i){C::put64(out+offsets[i],physical[i]);C::put64(out+offsets[i]+8,sizes[i]);R::put32(out+offsets[i]+16,2);}
  return true;
}
inline bool promotion(const Plan &p,const C::Plan &golden,bool physical,unsigned char *out,unsigned capacity){
  if(!out||capacity<560||!valid(p,golden)||!disjoint(&p,sizeof(p),out,560)||!disjoint(&golden,sizeof(golden),out,560))return false;
  for(unsigned i=0;i<560;++i)out[i]=0;
  R::put32(out,1);R::put32(out+12,Client);R::put32(out+16,Channel);R::put32(out+40,3);
  for(unsigned i=0;i<3;++i){const auto &b=p.buffers[i];auto *e=out+48+i*32;
    C::put64(e,physical?b.physical:0);C::put64(e+8,physical?0:b.va);C::put64(e+16,physical?b.bytes:0);
    R::put32(e+24,physical?4:0);e[28]=static_cast<unsigned char>(i);e[30]=physical;e[31]=physical;
  }
  if(!physical){
    // Promote the existing FECS/register-map VAs for this execution address
    // space. Their backing and PTEs belong to the verified golden tree.
    // Keep physical initialization restricted to the three private buffers.
    // Promote all seven mapped global graphics buffers: bundle CB, pagepool,
    // attribute CB, RTV CB, FECS and both register maps. Existing golden PTEs
    // already back these VAs; none of their contents are reinitialized here.
    R::put32(out+40,10);
    for(unsigned i=0;i<7;++i){const auto &b=golden.buffers[2+i];auto *e=out+48+(i+3)*32;
      C::put64(e+8,b.virtualAddress);e[28]=static_cast<unsigned char>(b.id);
    }
  }
  return true;
}
inline bool mappings(const Plan &p,const C::Plan &golden,L::Range (&out)[6]){
  if(!valid(p,golden)||!disjoint(&p,sizeof(p),out,sizeof(out))||!disjoint(&golden,sizeof(golden),out,sizeof(out)))return false;
  out[0]={SubmitCodec::CommandVA,Base+0x2000,4096};out[1]={SubmitCodec::FenceVA,Base+0x3000,4096};out[2]={RingVA,Base,4096};
  for(unsigned i=0;i<3;++i)out[3+i]={p.buffers[i].va,p.buffers[i].physical,p.buffers[i].allocated};
  return L::rangesValid(out,6);
}
}
