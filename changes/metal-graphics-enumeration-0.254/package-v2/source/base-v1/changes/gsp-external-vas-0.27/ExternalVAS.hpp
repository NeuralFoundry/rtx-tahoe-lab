#pragma once
#include <stddef.h>
#include <stdint.h>

// CPU protocol only. A native caller must own and retain the existing root,
// descendants and provider before publishing SET_PAGE_DIRECTORY. This header
// neither performs IO nor authorizes a channel, work submission or cleanup.
namespace ExternalVAS {
constexpr uint32_t Client=0xc1000000U,Root=0xcf000010U,Device=0xcf000011U,
 Subdevice=0xcf000012U,Vaspace=0xcf000013U;
constexpr uint64_t RootPhysical=0x1002000ULL,RequestedBase=0x1000ULL,
 RequestedSize=0x1fffffb000000ULL,RequiredLo=0x1000000000ULL,RequiredHi=0x1040000000ULL;
constexpr unsigned Steps=5,FirstSequence=14,FinalProducer=19,RequestBytes=Steps*4096;
constexpr unsigned RootEntries=4,ExternalFlags=0x48,DirectoryFlags=8;
inline unsigned paramBytes(unsigned step){constexpr unsigned n[]={120,56,4,48,48};return step<Steps?n[step]:0;}
inline unsigned function(unsigned step){return step<4?103:54;}
inline uint32_t object(unsigned step){constexpr uint32_t a[]={Root,Device,Subdevice,Vaspace};return step<4?a[step]:0;}
inline uint32_t parent(unsigned step){return step==0?0:step==1?Client:Device;}
inline uint32_t klass(unsigned step){constexpr uint32_t a[]={0,0x80,0x2080,0x90f1};return step<4?a[step]:0;}
inline void put32(unsigned char *p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=static_cast<unsigned char>(n>>(8*i));}
inline void put64(unsigned char *p,uint64_t n){for(unsigned i=0;i<8;++i)p[i]=static_cast<unsigned char>(n>>(8*i));}
inline uint32_t get32(const unsigned char *p){uint32_t n=0;for(unsigned i=0;i<4;++i)n|=uint32_t(p[i])<<(8*i);return n;}
inline uint64_t get64(const unsigned char *p){uint64_t n=0;for(unsigned i=0;i<8;++i)n|=uint64_t(p[i])<<(8*i);return n;}
inline bool parameters(unsigned step,unsigned char *out,size_t capacity){
 const unsigned n=paramBytes(step);if(!out||!n||capacity<n)return false;
 for(unsigned i=0;i<n;++i)out[i]=0;
 if(step==1)put32(out+4,Client);
 if(step==3){put32(out+4,ExternalFlags);put64(out+8,RequestedSize);put64(out+40,RequestedBase);}
 if(step==4){put32(out,Client);put32(out+4,Device);put32(out+8,~0U);put64(out+16,RootPhysical);
  put32(out+24,RootEntries);put32(out+28,DirectoryFlags);put32(out+32,Vaspace);put32(out+40,1);put32(out+44,~0U);}
 return true;
}
inline uint32_t checksum(const unsigned char *raw,unsigned used){uint32_t n=0;for(unsigned i=0;i<((used+7)&~7U);i+=4)n^=get32(raw+i);return n;}
inline bool request(unsigned step,unsigned char *out,size_t capacity){
 if(step>=Steps||!out||capacity<4096)return false;
 for(unsigned i=0;i<4096;++i)out[i]=0;
 const unsigned header=step<4?32:0,n=paramBytes(step);
 put32(out+36,FirstSequence+step);put32(out+40,1);put32(out+48,0x3000000);put32(out+52,0x43505256);
 put32(out+56,32+header+n);put32(out+60,function(step));put32(out+64,~0U);put32(out+68,~0U);
 if(step<4){put32(out+80,Client);put32(out+84,parent(step));put32(out+88,object(step));put32(out+92,klass(step));put32(out+100,n);}
 if(!parameters(step,out+80+header,n))return false;
 put32(out+32,checksum(out,80+header+n));return true;
}
struct Reply {bool accepted=false;uint64_t base=0,size=0,internalLo=0,internalHi=0;uint32_t bigPage=0;};
inline bool reply(unsigned step,uint32_t sequence,const unsigned char *raw,size_t bytes,Reply &out){
 const uintptr_t a=reinterpret_cast<uintptr_t>(raw),b=reinterpret_cast<uintptr_t>(&out);
 if(!raw||a>UINTPTR_MAX-bytes||b>UINTPTR_MAX-sizeof(out)||(a<b+sizeof(out)&&b<a+bytes))return false;
 out={};if(step>=Steps||sequence==~0U||bytes!=4096)return false;
 // The actual 570.144 function54 response is a status-only, empty ACK.
 // It does not echo the request's 48 parameter bytes. Unused slot tail is stale.
 const unsigned header=step<4?32:0,n=step<4?paramBytes(step):0,used=80+header+n;
 for(unsigned i=0;i<32;++i)if(raw[i])return false;
 if(get32(raw+36)!=sequence||get32(raw+40)!=1||get32(raw+44)||get32(raw+48)!=0x3000000||get32(raw+52)!=0x43505256||
    get32(raw+56)!=32+header+n||get32(raw+60)!=function(step)||get32(raw+64)||get32(raw+68)||get32(raw+72)||get32(raw+76)||checksum(raw,used))return false;
 for(unsigned i=used;i<((used+7)&~7U);++i)if(raw[i])return false;
 if(step==4){out.accepted=true;return true;}
 if(step<4&&(get32(raw+80)!=Client||get32(raw+84)!=parent(step)||get32(raw+88)!=object(step)||get32(raw+92)!=klass(step)||
    get32(raw+96)||get32(raw+100)!=n||get32(raw+104)||get32(raw+108)))return false;
 unsigned char expected[120];if(!parameters(step,expected,sizeof(expected)))return false;
 const unsigned char *p=raw+80+header;
 if(step==0)put32(expected,Client); // Root ALLOC returns hClient in its first parameter.
 if(step==3){
  if(get32(p)||get32(p+4)!=ExternalFlags||get32(p+36))return false;
  out.base=get64(p+40);out.size=get64(p+8);out.internalLo=get64(p+16);out.internalHi=get64(p+24);out.bigPage=get32(p+32);
  if(out.base<RequestedBase||out.base>RequiredLo||out.base%4096||out.size<RequiredHi||out.size>(1ULL<<49)||out.size%4096||
     (out.bigPage&&out.bigPage!=65536&&out.bigPage!=131072))return false;
  if((out.internalLo||out.internalHi)&&(out.internalLo>out.internalHi||out.internalHi>=out.size||
     !(out.internalHi<RequiredLo||out.internalLo>=RequiredHi)))return false;
 }else for(unsigned i=0;i<n;++i)if(p[i]!=expected[i])return false;
 out.accepted=true;return true;
}
// Monotonic CPU journal. Native lifecycle/queue ownership remains a separate gate.
struct Journal {unsigned next=0;uint32_t nextSequence=0;bool failed=false;Reply vas;};
inline bool consume(Journal &j,const unsigned char *raw,size_t bytes){
 if(j.failed||j.next>=Steps)return false;
 Reply r;if(!reply(j.next,j.nextSequence,raw,bytes,r)){j.failed=true;return false;}
 if(j.next==3)j.vas=r;++j.next;++j.nextSequence;return true;
}
static_assert(RootEntries*(1ULL<<47)==(1ULL<<49),"49-bit Ampere root coverage");
static_assert(Client!=0xc1e00004U&&Vaspace!=0xcf000003U,"Separate bootstrap and user objects");
}
