#pragma once
#include "../root179/OwnedDataArena178.hpp"
namespace RTXDataWindow181 {
// Rechecks exactly the touched physical pages against the original DMA list
// and the acknowledged table rows. Shared by native IOKit and CPU fault tests.
template<class IO>bool stable(const RTXDataArena178::Lifetime&life,IO&io,uint32_t i,uint64_t off,uint64_t n){
 const auto&info=life.info();
 if(info.phase!=RTXDataArena178::Phase::Exposed||info.cleanupAttempted||i>=info.buffers)return false;
 const auto*b=life.buffer(i);const auto&d=io.backingInfo(i);const auto*pages=io.pages(i);const auto*rows=io.rows();
 if(!b||!rows||!d.ready||d.error||!d.exposed||d.cleanupAttempted||!pages||!d.writeEpoch||d.writeEpoch!=d.publishedEpoch||
    d.logical!=b->logical||d.mapped!=b->mapped||d.pages!=b->pages||!RTXBacking166::range(off,n,b->logical))return false;
 const uint64_t first=off/4096,last=(off+n-1)/4096;
 for(uint64_t p=first;p<=last;++p){uint64_t pa=0,extent=0;
  if(!io.physicalPage(i,uint32_t(p),pa,extent)||pa!=pages[p]||extent<4096)return false;
  const uint64_t va=b->va+p*4096;uint32_t lo=0,hi=info.totalRows;
  while(lo<hi){uint32_t mid=lo+(hi-lo)/2;if(rows[mid].va<va)lo=mid+1;else hi=mid;}
  if(lo==info.totalRows||rows[lo].va!=va||rows[lo].physical!=pa||rows[lo].owner!=uint64_t(i)+1||
     rows[lo].aperture!=RTXPageTree167::Aperture::System||rows[lo].cached||rows[lo].reserved||
     rows[lo].access!=((b->access&RTXSpans165::Write)?RTXPageTree167::Access::ReadWrite:RTXPageTree167::Access::Read))return false;
 }
 return true;
}
}
