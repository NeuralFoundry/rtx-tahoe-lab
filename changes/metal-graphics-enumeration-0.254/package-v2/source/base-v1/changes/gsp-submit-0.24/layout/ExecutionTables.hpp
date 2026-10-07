#pragma once
#include "ExecutionPlan.hpp"
namespace ExecutionTables {
namespace P=ExecutionPlan;namespace C=ChannelCodec;namespace L=GMMULeaves;
struct Result {bool valid=false;unsigned oldBytes=0,childBytes=0,addedLeafPages=0,addedPtes=0,existingTablePtes=0;};
// Heap buffers supplied by the caller: rootScratch12KiB, expectedScratch45KiB,
// out45KiB. Source root/children and out stay untouched on rejected proposals.
inline bool merge(const unsigned char *root,unsigned rootBytes,const unsigned char *child,unsigned childBytes,
 const C::Plan &golden,const P::Plan &execution,unsigned char *rootScratch,unsigned char *expectedScratch,
 unsigned char *out,unsigned capacity,Result &r){
  // An aliased status reference must not modify a source or output on failure.
  const void *statusInputs[]={root,child,&golden,&execution,rootScratch,expectedScratch,out};
  const size_t statusSizes[]={rootBytes,childBytes,sizeof(golden),sizeof(execution),L::OldBytes,L::MaxChildBytes,capacity};
  for(unsigned i=0;i<7;++i)if(statusInputs[i]&&!P::disjoint(&r,sizeof(r),statusInputs[i],statusSizes[i]))return false;
  r={};
  if(rootBytes!=L::OldBytes||childBytes<8192||childBytes>L::MaxChildBytes||childBytes%4096||capacity>L::MaxChildBytes||capacity<childBytes||
     !P::valid(execution,golden))return false;
  struct Span {const void *p;size_t bytes;};
  const Span inputs[]={{root,rootBytes},{child,childBytes},{&golden,sizeof(golden)},{&execution,sizeof(execution)}};
  const Span outputs[]={{rootScratch,L::OldBytes},{expectedScratch,L::MaxChildBytes},{out,capacity}};
  if(!P::disjoint(root,rootBytes,child,childBytes))return false;
  for(const auto &a:outputs){for(const auto &b:inputs)if(!P::disjoint(a.p,a.bytes,b.p,b.bytes))return false;}
  for(unsigned i=0;i<3;++i)for(unsigned j=0;j<i;++j)if(!P::disjoint(outputs[i].p,outputs[i].bytes,outputs[j].p,outputs[j].bytes))return false;
  if(L::read64(root)!=0x100322||L::read64(root+4096)!=0x100422||L::read64(root+L::ParentOffset)!=L::ParentValue)return false;
  for(unsigned i=0;i<rootBytes;++i)rootScratch[i]=root[i];L::write64(rootScratch+L::ParentOffset,0);
  L::Range oldRanges[10],newRanges[6];L::Result baseline;
  if(!C::mappingRanges(golden,oldRanges)||!P::mappings(execution,golden,newRanges)||
     !L::build(rootScratch,rootBytes,oldRanges,10,expectedScratch,L::MaxChildBytes,baseline)||baseline.childBytes!=childBytes)return false;
  for(unsigned i=0;i<childBytes;++i)if(child[i]!=expectedScratch[i])return false;
  for(const auto &a:newRanges)for(const auto &b:oldRanges)
    if((a.va<b.va+b.bytes&&b.va<a.va+a.bytes)||(a.pa<b.pa+b.bytes&&b.pa<a.pa+a.bytes))return false;
  unsigned char table[256]={};unsigned pages=childBytes/4096,added=0,pteCount=0,existingCount=0;
  for(unsigned group=0;group<256;++group){const auto hi=L::read64(child+group*16+8);
    if(hi)table[group]=static_cast<unsigned char>((((hi&0x1ffffff00ULL)<<4)-L::NewBase)/4096);
  }
  const unsigned oldPages=pages;
  // Validate all allocations and empty slots before modifying out.
  for(const auto &a:newRanges)for(P::U64 off=0;off<a.bytes;off+=4096){
    const unsigned group=unsigned((a.va+off-L::VABase)>>21),index=unsigned(((a.va+off)>>12)&511);
    if(!table[group]){if((pages+1)*4096>capacity)return false;table[group]=static_cast<unsigned char>(pages++);++added;}
    if(table[group]<oldPages){if(L::read64(child+unsigned(table[group])*4096+index*8))return false;++existingCount;}
    ++pteCount;
  }
  for(unsigned i=0;i<pages*4096;++i)out[i]=i<childBytes?child[i]:0;
  for(unsigned group=0;group<256;++group)if(table[group]>=oldPages){
    L::write64(out+group*16,0x20);L::write64(out+group*16+8,((L::NewBase+unsigned(table[group])*4096)>>4)|2);
  }
  for(const auto &a:newRanges)for(P::U64 off=0;off<a.bytes;off+=4096){
    const auto va=a.va+off,pa=a.pa+off;
    L::write64(out+unsigned(table[(va-L::VABase)>>21])*4096+unsigned((va>>12)&511)*8,(L::Kind<<56)|(pa>>4)|1);
  }
  r.valid=true;r.oldBytes=childBytes;r.childBytes=pages*4096;r.addedLeafPages=added;r.addedPtes=pteCount;r.existingTablePtes=existingCount;return true;
}
}
