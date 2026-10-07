#pragma once
#include "../../../driver/GSPComputePrepProtocol.hpp"
#include "../GMMULeaves.hpp"

namespace ChannelCodec {
namespace R=GSPComputePrep;
using U64=unsigned long long;
constexpr unsigned Channel=0xcf000004U,Compute=0xcf000005U,Copy=0xcf000006U;
constexpr unsigned Steps=5,GRBytes=1664,PromotionBytes=560,ChannelBytes=368;
struct Buffer {unsigned id=0,kind=0;U64 physical=0,virtualAddress=0,bytes=0,allocated=0,alignment=0;bool usePhysical=false,useVirtual=false;};
struct Plan {bool valid=false;Buffer buffers[9];U64 physicalEnd=0,virtualEnd=0,backingBytes=0;};
inline void put64(unsigned char *p,U64 v){for(unsigned i=0;i<8;++i)p[i]=static_cast<unsigned char>(v>>(8*i));}
inline U64 align(U64 value,U64 boundary){return (value+boundary-1)&~(boundary-1);}
inline bool plan(const unsigned char *gr,unsigned bytes,Plan &out){
  out={};if(!gr||bytes!=GRBytes)return false;
  constexpr unsigned ids[9]={0,2,3,4,5,6,9,10,11},kinds[9]={0,16,17,18,19,20,23,24,24};
  U64 pa=0x1200000,va=0x1020200000ULL;
  for(unsigned i=0;i<9;++i){
    U64 amount=R::get32(gr+kinds[i]*8),boundary=R::get32(gr+kinds[i]*8+4);
    if(!amount||amount==0xffffffff||!boundary||boundary>0x200000||(boundary&(boundary-1)))return false;
    if(i==0)amount+=0x40000;
    if(ids[i]==5&&boundary<0x200000)boundary=0x200000;
    amount=align(amount,boundary);if(amount>0x1000000)return false;
    const U64 allocated=align(amount,4096);if(boundary<4096)boundary=4096;
    pa=align(pa,boundary);va=align(va,boundary);
    if(pa>=0x4000000||allocated>0x4000000-pa||va>=GMMULeaves::VAEnd||allocated>GMMULeaves::VAEnd-va)return false;
    out.buffers[i]={ids[i],kinds[i],pa,va,amount,allocated,boundary,i<2||i>=6,ids[i]!=10};
    pa+=allocated;va+=allocated;out.backingBytes+=allocated;
  }
  out.physicalEnd=pa;out.virtualEnd=va;out.valid=true;return true;
}
inline bool planValid(const Plan &p){
  if(!p.valid)return false;
  constexpr unsigned ids[9]={0,2,3,4,5,6,9,10,11},kinds[9]={0,16,17,18,19,20,23,24,24};
  U64 pa=0x1200000,va=0x1020200000ULL,total=0;
  for(unsigned i=0;i<9;++i){const auto &b=p.buffers[i];
    if(b.id!=ids[i]||b.kind!=kinds[i]||b.usePhysical!=(i<2||i>=6)||b.useVirtual!=(ids[i]!=10)||
       !b.bytes||b.bytes>0x1000000||b.alignment<4096||b.alignment>0x200000||(b.alignment&(b.alignment-1)))return false;
    if(b.allocated!=align(b.bytes,4096)||b.physical!=align(pa,b.alignment)||b.virtualAddress!=align(va,b.alignment))return false;
    if(b.physical>=0x4000000||b.allocated>0x4000000-b.physical||b.virtualAddress>=GMMULeaves::VAEnd||b.allocated>GMMULeaves::VAEnd-b.virtualAddress)return false;
    pa=b.physical+b.allocated;va=b.virtualAddress+b.allocated;total+=b.allocated;
  }
  return p.physicalEnd==pa&&p.virtualEnd==va&&p.backingBytes==total;
}
inline bool mappingRanges(const Plan &p,GMMULeaves::Range (&out)[10]){
  if(!planValid(p))return false;
  out[0]={GMMULeaves::VABase,0x1100000,4096};
  for(unsigned i=0;i<9;++i)out[i+1]={p.buffers[i].virtualAddress,p.buffers[i].physical,p.buffers[i].allocated};
  return GMMULeaves::rangesValid(out,10);
}
inline bool channelParameters(unsigned char *out,unsigned capacity){
  if(!out||capacity<ChannelBytes)return false;
  for(unsigned i=0;i<ChannelBytes;++i)out[i]=0;
  put64(out+8,0x1020000000ULL);R::put32(out+16,32);R::put32(out+20,0x200320);R::put32(out+28,R::Vaspace);
  put64(out+64,0x100);R::put32(out+128,1);R::put32(out+132,3);R::put32(out+244,0x1a);
  constexpr unsigned offsets[4]={144,168,192,216},addresses[4]={0x1101000,0x1100100,0x1101000,0x1102000},sizes[4]={0x1000,0x20,0x200,0x5000};
  for(unsigned i=0;i<4;++i){put64(out+offsets[i],addresses[i]);put64(out+offsets[i]+8,sizes[i]);R::put32(out+offsets[i]+16,2);}
  return true;
}
inline bool promotionParameters(const Plan &p,unsigned char *out,unsigned capacity){
  if(!out||capacity<PromotionBytes||!planValid(p))return false;
  for(unsigned i=0;i<PromotionBytes;++i)out[i]=0;
  R::put32(out,1);R::put32(out+12,R::Client);R::put32(out+16,Channel);R::put32(out+40,9);
  for(unsigned i=0;i<9;++i){const auto &b=p.buffers[i];auto *e=out+48+i*32;
    put64(e,b.usePhysical?b.physical:0);put64(e+8,b.useVirtual?b.virtualAddress:0);put64(e+16,b.usePhysical?b.bytes:0);
    R::put32(e+24,b.usePhysical?4:0);e[28]=static_cast<unsigned char>(b.id);e[30]=b.usePhysical;e[31]=b.usePhysical&&!b.useVirtual;
  }
  return true;
}
inline unsigned function(unsigned step){return step==1||step==2?76:103;}
inline unsigned params(unsigned step){return step==0?ChannelBytes:step==1?GRBytes:step==2?PromotionBytes:0;}
inline unsigned header(unsigned step){return function(step)==76?24:32;}
inline unsigned control(unsigned step){return step==1?0x20800a32:step==2?0x2080012b:0;}
inline unsigned object(unsigned step){return step==0?Channel:step==3?Compute:step==4?Copy:R::Subdevice;}
inline unsigned klass(unsigned step){return step==0?0xc56f:step==3?0xc7c0:step==4?0xc7b5:0;}
inline unsigned parent(unsigned step){return step==0?R::Device:Channel;}
inline bool request(unsigned step,const Plan &p,unsigned char *out,unsigned capacity){
  if(step>=Steps||!out||capacity<R::Page||(step==2&&!planValid(p)))return false;
  for(unsigned i=0;i<R::Page;++i)out[i]=0;
  R::put32(out+36,step+9);R::put32(out+40,1);R::put32(out+48,0x03000000);R::put32(out+52,0x43505256);
  R::put32(out+56,32+header(step)+params(step));R::put32(out+60,function(step));R::put32(out+64,~0U);R::put32(out+68,~0U);
  R::put32(out+80,R::Client);R::put32(out+84,function(step)==76?R::Subdevice:parent(step));
  if(function(step)==76){R::put32(out+88,control(step));R::put32(out+96,params(step));}
  else {R::put32(out+88,object(step));R::put32(out+92,klass(step));R::put32(out+100,params(step));}
  if(step==0&&!channelParameters(out+112,ChannelBytes))return false;
  if(step==2&&!promotionParameters(p,out+104,PromotionBytes))return false;
  unsigned sum=0;for(unsigned i=0;i<((80+header(step)+params(step)+7)&~7U);i+=4)sum^=R::get32(out+i);
  R::put32(out+32,sum);return true;
}
struct Reply {unsigned channelId=~0U,subdeviceMask=~0U;bool accepted=false;};
inline bool reply(const unsigned char *raw,unsigned bytes,unsigned sequence,unsigned step,const Plan &p,Reply &out){
  out={};if(step>=Steps)return false;
  GSPInitEvents::Record record;
  if(!GSPInitEvents::decode(raw,bytes,sequence,record)||record.function!=function(step)||record.result||R::get32(raw+68)||R::get32(raw+72)||
     record.payloadBytes!=header(step)+params(step)||R::get32(raw+80)!=R::Client)return false;
  if(function(step)==76){
    if(R::get32(raw+84)!=R::Subdevice||R::get32(raw+88)!=control(step)||R::get32(raw+92)||R::get32(raw+96)!=params(step)||R::get32(raw+100))return false;
  }else{
    if(R::get32(raw+84)!=parent(step)||R::get32(raw+88)!=object(step)||R::get32(raw+92)!=klass(step)||R::get32(raw+96)||
       R::get32(raw+100)!=params(step)||R::get32(raw+104)||R::get32(raw+108))return false;
  }
  if(step==0){
    unsigned char canonical[ChannelBytes];if(!channelParameters(canonical,ChannelBytes))return false;
    // Direct RM RPC may return the internally allocated hPhysChannelGroup at
    // 240 (kernel_channel.c AllocWithSecInfo). Its opaque value is retained in
    // the raw journal, never used for MMIO, ownership or work submission.
    // cid/subDeviceId remain diagnostics; every other backing/flag byte echoes.
    for(unsigned i=0;i<ChannelBytes;++i)
      if(!((i>=132&&i<140)||(i>=240&&i<244))&&raw[112+i]!=canonical[i])return false;
    out.channelId=R::get32(raw+112+132);out.subdeviceMask=R::get32(raw+112+136);
    if(out.subdeviceMask>1)return false;
  }
  if(step==2){
    unsigned char canonical[PromotionBytes];if(!promotionParameters(p,canonical,PromotionBytes))return false;
    for(unsigned i=0;i<PromotionBytes;++i)if(raw[104+i]!=canonical[i])return false;
  }
  out.accepted=true;return true;
}
}
