#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../gsp-external-vas-0.27/ExternalVAS.hpp"

// Fixed CPU proposal. This file performs no RPC, register write or scheduling.
// Same-client/device experiment: separate user-client/VAS ownership is not yet
// reproduced from the reference. Native owner and reply journal integration
// must happen in a separate derivative before hardware use.
namespace ContextSharePlan {
constexpr uint32_t Client=ExternalVAS::Client,Device=ExternalVAS::Device,Vaspace=ExternalVAS::Vaspace;
constexpr uint32_t Channel=0xcf000007U,Group=0xcf00000aU,Share=0xcf00000bU;
constexpr uint32_t GroupClass=0xa06c,ShareClass=0x9067,ChannelClass=0xc56f;
constexpr uint32_t GroupSchedule=0xa06c0101,HardwareChannelId=4;
enum class Operation : unsigned { GroupAlloc, ShareAlloc, ChannelAlloc, Schedule };
constexpr unsigned bytes(Operation op){return op==Operation::GroupAlloc?20:op==Operation::ShareAlloc?12:op==Operation::ChannelAlloc?368:op==Operation::Schedule?2:0;}
constexpr uint32_t parent(Operation op){return op==Operation::GroupAlloc?Device:op==Operation::ShareAlloc||op==Operation::ChannelAlloc?Group:0;}
constexpr uint32_t object(Operation op){return op==Operation::ShareAlloc?Share:op==Operation::ChannelAlloc?Channel:Group;}
constexpr uint32_t klass(Operation op){return op==Operation::GroupAlloc?GroupClass:op==Operation::ShareAlloc?ShareClass:op==Operation::ChannelAlloc?ChannelClass:0;}
inline void put32(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=static_cast<unsigned char>(v>>(i*8));}
inline void put64(unsigned char *p,uint64_t v){for(unsigned i=0;i<8;++i)p[i]=static_cast<unsigned char>(v>>(i*8));}
inline uint32_t get32(const unsigned char *p){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(p[i])<<(i*8);return v;}

inline bool parameters(Operation op,unsigned char *out,size_t capacity){
 const unsigned n=bytes(op);if(!n||!out||capacity<n)return false;
 for(unsigned i=0;i<n;++i)out[i]=0;
 if(op==Operation::GroupAlloc){put32(out+12,1);return true;}
 // NV_CTXSHARE_ALLOCATION_FLAGS_SUBCONTEXT_SYNC: graphics and compute on VEID 0.
 if(op==Operation::ShareAlloc){put32(out,Vaspace);put32(out+4,0);return true;}
 if(op==Operation::Schedule){out[0]=1;return true;}
 put64(out+8,0x1020003000ULL);put32(out+16,32);put32(out+20,0x200420);
 put32(out+24,Share);put32(out+28,0);put64(out+64,0x800);
 put32(out+128,0);put32(out+132,4);put32(out+244,0x14);
 constexpr unsigned offsets[4]={144,168,192,216};
 constexpr uint64_t physical[4]={0x03401000,0x03400800,0x03401000,0x03404000};
 constexpr uint64_t sizes[4]={0x1000,512,512,0x5000};
 for(unsigned i=0;i<4;++i){put64(out+offsets[i],physical[i]);put64(out+offsets[i]+8,sizes[i]);put32(out+offsets[i]+16,2);}
 return true;
}

struct ReplyParameters {bool accepted=false;uint32_t subcontext=~0U,sessionCid=~0U,subdeviceMask=~0U;};
// Call only after validating the full RM envelope, owner, transaction sequence,
// success status and checksum. This validates parameters, not a whole reply.
inline bool replyParameters(Operation op,const unsigned char *raw,size_t size,ReplyParameters &out){
 const uintptr_t a=reinterpret_cast<uintptr_t>(raw),b=reinterpret_cast<uintptr_t>(&out);
 if(!raw||a>UINTPTR_MAX-size||b>UINTPTR_MAX-sizeof(out)||(a<b+sizeof(out)&&b<a+size))return false;
 out={};const unsigned n=bytes(op);if(!n||size!=n)return false;
 unsigned char canonical[368];if(!parameters(op,canonical,sizeof(canonical)))return false;
 for(unsigned i=0;i<n;++i){
  if(op==Operation::ShareAlloc&&i>=8)continue;
  if(op==Operation::ChannelAlloc&&((i>=132&&i<140)||(i>=240&&i<244)))continue;
  if(raw[i]!=canonical[i])return false;
 }
 if(op==Operation::ShareAlloc){out.subcontext=get32(raw+8);if(out.subcontext!=0)return false;}
 if(op==Operation::ChannelAlloc){
  out.sessionCid=get32(raw+132);out.subdeviceMask=get32(raw+136);
  if(!out.sessionCid||out.sessionCid==3||out.sessionCid==~0U||out.subdeviceMask>1)return false;
 }
 out.accepted=true;return true;
}
static_assert(Group!=Share&&Group!=Channel&&Share!=Channel,"Distinct fixed RM handles");
static_assert(((0x200420U>>8)&7)==HardwareChannelId,"USERD hardware channel remains four");
}
