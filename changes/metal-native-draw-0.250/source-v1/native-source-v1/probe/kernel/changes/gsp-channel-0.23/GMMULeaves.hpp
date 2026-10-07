#pragma once
#if defined(KERNEL)
#include <sys/types.h>
#include <stdint.h>
#include <stddef.h>
#else
#include <cstddef>
#include <cstdint>
#endif

// Portable serializer only; caller owns all buffers. No hardware access.
namespace GMMULeaves {
constexpr uint64_t Page=4096,OldBase=0x1002000,NewBase=0x1005000,LeaseEnd=0x1010000;
constexpr uint64_t VABase=0x1020000000ULL,VAEnd=VABase+(1ULL<<29),BAR1End=0x4000000;
constexpr size_t OldBytes=3*Page,ParentOffset=2*Page+129*8,MaxChildBytes=LeaseEnd-NewBase;
constexpr uint64_t ParentValue=(NewBase>>4)|0x22,Kind=6;
struct Range {uint64_t va,pa,bytes;};
struct Result {size_t childBytes=0,leafTables=0,mappedPages=0;uint64_t parentValue=0;};
inline uint64_t read64(const uint8_t *p) {uint64_t v=0;for(unsigned i=0;i<8;++i)v|=uint64_t(p[i])<<(8*i);return v;}
inline void write64(uint8_t *p,uint64_t v) {for(unsigned i=0;i<8;++i)p[i]=uint8_t(v>>(8*i));}
inline bool rangesValid(const Range *r,size_t n) {
  if(!r||n==0||n>10)return false;
  for(size_t i=0;i<n;++i) {
    const auto &a=r[i];
    if((a.va|a.pa|a.bytes)&4095||a.bytes==0||a.bytes>(16ULL<<20))return false;
    if(a.va<VABase||a.va>=VAEnd||a.bytes>VAEnd-a.va)return false;
    const bool ring=a.pa==0x1100000&&a.va==VABase&&a.bytes==Page;
    if(!ring&&(a.pa<0x1200000||a.pa>=BAR1End||a.bytes>BAR1End-a.pa))return false;
    for(size_t j=0;j<i;++j)if((a.va<r[j].va+r[j].bytes&&r[j].va<a.va+a.bytes)||
                             (a.pa<r[j].pa+r[j].bytes&&r[j].pa<a.pa+a.bytes))return false;
  }
  return true;
}
inline bool build(const uint8_t *old,size_t oldBytes,const Range *ranges,size_t count,
                  uint8_t *out,size_t capacity,Result &result) {
  result={};
  if(!old||oldBytes!=OldBytes||!out||!rangesValid(ranges,count))return false;
  if(read64(old)!=0x100322||read64(old+Page)!=0x100422||read64(old+ParentOffset)!=0)return false;
  // Reject source/output aliasing before touching caller memory.
  const auto op=reinterpret_cast<uintptr_t>(old),np=reinterpret_cast<uintptr_t>(out);
  const auto rp=reinterpret_cast<uintptr_t>(ranges);
  if(capacity>MaxChildBytes||np>UINTPTR_MAX-capacity||op>UINTPTR_MAX-OldBytes||rp>UINTPTR_MAX-count*sizeof(Range))return false;
  if(np<op+OldBytes&&op<np+capacity)return false;
  if(np<rp+count*sizeof(Range)&&rp<np+capacity)return false;
  uint8_t groups[256]={};size_t tables=0,pages=0;
  for(size_t i=0;i<count;++i)for(uint64_t off=0;off<ranges[i].bytes;off+=Page) {
    groups[(ranges[i].va+off-VABase)>>21]=1;++pages;
  }
  for(size_t i=0;i<256;++i)if(groups[i])groups[i]=uint8_t(++tables);
  const size_t bytes=(tables+1)*Page;
  if(bytes>capacity||bytes>MaxChildBytes)return false;
  for(size_t i=0;i<bytes;++i)out[i]=0;
  for(size_t i=0;i<256;++i)if(groups[i]) {
    write64(out+i*16,0x20);write64(out+i*16+8,((NewBase+groups[i]*Page)>>4)|2);
  }
  for(size_t i=0;i<count;++i)for(uint64_t off=0;off<ranges[i].bytes;off+=Page) {
    const uint64_t va=ranges[i].va+off,pa=ranges[i].pa+off;
    write64(out+groups[(va-VABase)>>21]*Page+((va>>12)&511)*8,(Kind<<56)|(pa>>4)|1);
  }
  result.childBytes=bytes;result.leafTables=tables;result.mappedPages=pages;result.parentValue=ParentValue;
  return true;
}
// Return false for corrupt/out-of-scope input; valid unmapped VA returns mapped=false.
inline bool walk(const uint8_t *old,size_t oldBytes,const uint8_t *child,size_t childBytes,uint64_t va,bool &mapped,uint64_t &pa) {
  mapped=false;pa=0;
  if(!old||!child||oldBytes!=OldBytes||childBytes<2*Page||childBytes>MaxChildBytes||childBytes%Page||va<VABase||va>=VAEnd)return false;
  if(read64(old)!=0x100322||read64(old+Page)!=0x100422||read64(old+ParentOffset)!=ParentValue)return false;
  const size_t group=size_t((va-VABase)>>21);const auto lo=read64(child+group*16),hi=read64(child+group*16+8);
  if(lo==0&&hi==0)return true;
  if(lo!=0x20||(hi&255)!=2||(hi>>33)!=0)return false;
  const auto physical=(hi&0x1ffffff00ULL)<<4;
  if(physical<NewBase+Page||physical-NewBase>childBytes-Page)return false;
  const auto pte=read64(child+size_t(physical-NewBase)+size_t((va>>12)&511)*8);
  if(pte==0)return true;
  constexpr uint64_t allowed=(Kind<<56)|0x1ffffff00ULL|1;
  if((pte&~allowed)!=0||(pte>>56)!=Kind||(pte&255)!=1)return false;
  const auto result=((pte&0x1ffffff00ULL)<<4)|(va&4095);
  if(!((result>=0x1100000&&result<0x1101000)||(result>=0x1200000&&result<BAR1End)))return false;
  mapped=true;pa=result;return true;
}
}
