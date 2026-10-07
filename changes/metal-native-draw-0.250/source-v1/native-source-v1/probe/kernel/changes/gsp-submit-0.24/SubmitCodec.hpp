#pragma once
#include "../gsp-channel-0.23/transactions/ChannelCodec.hpp"
// CPU-only proposal for a FUTURE execution channel. No device/queue adapter.
namespace SubmitCodec {
namespace R=GSPComputePrep;
using U64=unsigned long long;
constexpr unsigned Channel=0xcf000007U,Controls[3]={0xa06f0104,0xa06f0103,0xc36f0108},Sizes[3]={4,2,4};
constexpr U64 CommandVA=0x1020001000ULL,FenceVA=0x1020002000ULL;
constexpr unsigned FenceValue=0x30602401;
inline bool request(unsigned step,unsigned sequence,unsigned char *out,unsigned capacity){
  if(step>=3||!out||capacity<4096||sequence==~0U)return false;
  for(unsigned i=0;i<4096;++i)out[i]=0;
  R::put32(out+36,sequence);R::put32(out+40,1);R::put32(out+48,0x03000000);R::put32(out+52,0x43505256);
  R::put32(out+56,56+Sizes[step]);R::put32(out+60,76);R::put32(out+64,~0U);R::put32(out+68,~0U);
  R::put32(out+80,R::Client);R::put32(out+84,Channel);R::put32(out+88,Controls[step]);R::put32(out+96,Sizes[step]);
  if(step==0)R::put32(out+104,1);else if(step==1)out[104]=1;else R::put32(out+104,~0U);
  unsigned checksum=0;for(unsigned i=0;i<((104+Sizes[step]+7)&~7U);i+=4)checksum^=R::get32(out+i);
  R::put32(out+32,checksum);return true;
}
struct Reply {bool accepted=false;unsigned rawToken=~0U;};
inline bool reply(const unsigned char *raw,unsigned bytes,unsigned sequence,unsigned step,Reply &out){
  out={};if(step>=3)return false;GSPInitEvents::Record r;
  if(!GSPInitEvents::decode(raw,bytes,sequence,r)||r.function!=76||r.result||r.payloadBytes!=24+Sizes[step]||
    R::get32(raw+68)||R::get32(raw+72)||R::get32(raw+80)!=R::Client||R::get32(raw+84)!=Channel||
    R::get32(raw+88)!=Controls[step]||R::get32(raw+92)||R::get32(raw+96)!=Sizes[step]||R::get32(raw+100))return false;
  if(step==0&&R::get32(raw+104)!=1)return false;
  if(step==1&&(raw[104]!=1||raw[105]!=0))return false;
  if(step==2){out.rawToken=R::get32(raw+104);if(out.rawToken==~0U)return false;}
  out.accepted=true;return true; // rawToken is NOT a validated doorbell value.
}
inline bool userd(unsigned offset,unsigned declared,unsigned backing){
  return !(offset&3)&&declared>=512&&offset<=backing&&declared<=backing-offset;
}
inline bool entry(U64 address,unsigned bytes,U64 &out){
  out=0;if(!address||address>=(1ULL<<40)||(address&3)||!bytes||(bytes&3)||bytes>4096||bytes>(1ULL<<40)-address)return false;
  out=address|(1ULL<<41)|(static_cast<U64>(bytes/4)<<42);return true;
}
inline bool increment(unsigned method,unsigned subchannel,unsigned count,unsigned &out){
  out=0;if(method>0x3ffc||(method&3)||subchannel>=8||!count||count>8191||count>(0x4000-method)/4)return false;
  out=(1U<<29)|(count<<16)|(subchannel<<13)|(method/4);return true;
}
inline bool fence(unsigned char *out,unsigned capacity){
  if(!out||capacity<20)return false;unsigned header=0;if(!increment(0x10,0,4,header))return false;
  R::put32(out,header);R::put32(out+4,unsigned(FenceVA>>32));R::put32(out+8,unsigned(FenceVA&0xffffffff));
  R::put32(out+12,FenceValue);R::put32(out+16,0x01000002);return true;
}
}
