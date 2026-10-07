#pragma once
#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODMACommand.h>
#include <IOKit/IOMapper.h>
#include <IOKit/pci/IOPCIDevice.h>
#include "MetalDmaBacking166.hpp"

// Actual kernel-owned variable backing. Caller serializes in a sleepable
// client mutex, owns an open provider and retains this object on any ambiguous
// cleanup. No MMIO, PCI configuration writes, RM call or firmware start here.
class MacMetalDmaBacking166 {
 using U32=uint32_t;using U64=uint64_t;
 IOPCIDevice *pci;IOBufferMemoryDescriptor *memory=nullptr;IODMACommand *dma=nullptr;
 U64 *pageAddresses=nullptr,*sortScratch=nullptr;
 struct Backend {
  MacMetalDmaBacking166&s;
  U32 mapperReady(){
   if(!s.pci||IOMapper::gSystem)return kIOReturnNotReady;
   OSObject *parent=s.pci->copyProperty("iommu-parent");if(parent){parent->release();return kIOReturnNotReady;}return 0;
  }
  U32 allocatePages(U32 n){s.pageAddresses=static_cast<U64*>(IOMalloc(size_t(n)*8));return s.pageAddresses?0:kIOReturnNoMemory;}
  U32 allocateScratch(U32 n){s.sortScratch=static_cast<U64*>(IOMalloc(size_t(n)*8));return s.sortScratch?0:kIOReturnNoMemory;}
  U32 allocateMemory(U64 bytes){s.memory=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,kIODirectionInOut,vm_size_t(bytes),RTXBacking166::Page);return s.memory?0:kIOReturnNoMemory;}
  U32 zero(U64 bytes){if(!s.memory||s.memory->getLength()!=bytes||!s.memory->getBytesNoCopy())return kIOReturnBadArgument;bzero(s.memory->getBytesNoCopy(),size_t(bytes));return 0;}
  U32 createDma(U64 bytes){s.dma=IODMACommand::withSpecification(kIODMACommandOutputHost64,40,RTXBacking166::Page,IODMACommand::kMapped,bytes,RTXBacking166::Page,nullptr);return s.dma?0:kIOReturnNoMemory;}
  U32 prepareMemory(){return s.memory->prepare(kIODirectionInOut);}
  U32 attach(){return s.dma->setMemoryDescriptor(s.memory,false);}
  U32 prepareDma(U64 bytes){return s.dma->prepare(0,bytes);}
  U64*pages(){return s.pageAddresses;}U64*scratch(){return s.sortScratch;}
  U32 enumerate(U32 pageCount){
   UInt64 offset=0;U32 total=0;IODMACommand::Segment64 rows[32]={};const U64 bytes=U64(pageCount)*RTXBacking166::Page;
   for(U32 calls=0;calls<pageCount&&offset<bytes;++calls){
    const U64 before=offset;UInt32 count=pageCount-total<32?pageCount-total:32;const U32 capacity=count;
    IOReturn e=s.dma->gen64IOVMSegments(&offset,rows,&count);if(e)return e;
    if(!count||count>capacity||offset<=before||offset>bytes||offset-before!=U64(count)*RTXBacking166::Page)return kIOReturnBadArgument;
    for(U32 n=0;n<count;++n){
     IOByteCount available=0;const U64 physical=s.memory->getPhysicalSegment(before+U64(n)*RTXBacking166::Page,&available);
     // Matches the existing direct-mapping board contract. Reject a bounce or
     // IOMMU translation until its separate ownership/coherency path exists.
     if(rows[n].fLength!=RTXBacking166::Page||available<RTXBacking166::Page||physical!=rows[n].fIOVMAddr)return kIOReturnBadArgument;
     s.pageAddresses[total++]=rows[n].fIOVMAddr;
    }
   }
   return offset==bytes&&total==pageCount?0:kIOReturnUnderrun;
  }
  U32 synchronizeOut(){__sync_synchronize();auto e=s.dma->synchronize(kIODirectionOut);__sync_synchronize();return e;}
  U32 completeDma(){return s.dma->complete();}
  U32 clear(){return s.dma->clearMemoryDescriptor(false);}
  U32 completeMemory(){return s.memory->complete(kIODirectionInOut);}
  void releaseDma(){s.dma->release();s.dma=nullptr;}
  void releaseMemory(){s.memory->release();s.memory=nullptr;}
  void releasePages(U32 n){IOFree(s.pageAddresses,size_t(n)*8);s.pageAddresses=nullptr;}
  void releaseScratch(U32 n){IOFree(s.sortScratch,size_t(n)*8);s.sortScratch=nullptr;}
 };
 RTXBacking166::Lifetime lifetime;
 MacMetalDmaBacking166(const MacMetalDmaBacking166&)=delete;MacMetalDmaBacking166&operator=(const MacMetalDmaBacking166&)=delete;
public:
 explicit MacMetalDmaBacking166(IOPCIDevice*p):pci(p){}
 ~MacMetalDmaBacking166()=default; // Ownership is not recovered by destruction.
 const RTXBacking166::Info&info()const{return lifetime.info();}
 bool held()const{return lifetime.held();}
 bool prepare(U64 bytes){Backend b{*this};return lifetime.prepare(b,bytes);}
 bool cleanup(){Backend b{*this};return lifetime.cleanup(b);}
 bool publish(){Backend b{*this};return lifetime.publish(b);}
 bool expose(){return lifetime.expose();}
 const U64*pages()const{return info().ready&&!info().cleanupAttempted?pageAddresses:nullptr;}
 bool physicalPage(U32 index,U64 &address,U64 &bytes)const{
  if(!pages()||index>=info().pages)return false;IOByteCount length=0;
  address=memory->getPhysicalSegment(U64(index)*RTXBacking166::Page,&length);bytes=length;return true;
 }
 bool write(U64 offset,const void *data,U64 bytes){
  if(!data||!lifetime.canWrite()||bytes>4096||!RTXBacking166::range(offset,bytes,info().logical))return false;
  bcopy(data,static_cast<uint8_t*>(memory->getBytesNoCopy())+offset,size_t(bytes));return lifetime.copiedIn();
 }
 // Private page-table publisher194 only. A byte copy does not establish an
 // atomic live PDE/PTE update; use one aligned release store and the existing
 // DMA publication epoch. Caller owns serialization and page-table lifetime.
 bool writeTableEntry(U64 offset,U64 value){
  if(!lifetime.canWrite()||offset%8||!RTXBacking166::range(offset,8,info().logical)||!memory||!memory->getBytesNoCopy())return false;
  auto*p=reinterpret_cast<U64*>(static_cast<uint8_t*>(memory->getBytesNoCopy())+offset);
  if(reinterpret_cast<uintptr_t>(p)%alignof(U64))return false;
  __atomic_store_n(p,value,__ATOMIC_RELEASE);return lifetime.copiedIn();
 }
 bool read(U64 offset,void *data,U64 bytes)const{
  if(!data||!info().ready||info().cleanupAttempted||bytes>4096||!RTXBacking166::range(offset,bytes,info().logical))return false;
  bcopy(static_cast<const uint8_t*>(memory->getBytesNoCopy())+offset,data,size_t(bytes));return true;
 }
 bool importForObservation(){
  if(!info().ready||!info().exposed||info().cleanupAttempted||!dma||info().writeEpoch!=info().publishedEpoch)return false;
  __sync_synchronize();const auto status=dma->synchronize(kIODirectionIn);__sync_synchronize();return status==kIOReturnSuccess;
 }
 bool paddingZero()const{
  if(!info().ready||info().cleanupAttempted)return false;const auto*p=static_cast<const uint8_t*>(memory->getBytesNoCopy());
  for(U64 i=info().logical;i<info().mapped;++i)if(p[i])return false;return true;
 }
};
