#pragma once
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODMACommand.h>
#include <IOKit/IOMapper.h>
#include <IOKit/pci/IOPCIDevice.h>
#include "GSPFlushProtocol.hpp"

// No MMIO here. The service serializes allocation/cleanup in client context.
// No implicit destructor cleanup: an exposed or uncertain mapping stays alive.
class MacGSPFlushPage {
 using U32=GSPFlush075::U32;using U64=GSPFlush075::U64;
 IOPCIDevice *pci;IOBufferMemoryDescriptor *memory=nullptr;IODMACommand *dma=nullptr;
 static_assert(sizeof(IOReturn)==sizeof(U32),"IOKit status width");
 struct Backend {
  MacGSPFlushPage&s;
  U32 mapperReady(){
   if(!s.pci)return static_cast<U32>(kIOReturnBadArgument);
   // Match the current driver live contract: only direct, settled mappings.
   if(IOMapper::gSystem)return static_cast<U32>(kIOReturnNotReady);
   OSObject *parent=s.pci->copyProperty("iommu-parent");
   if(parent){parent->release();return static_cast<U32>(kIOReturnNotReady);}return static_cast<U32>(kIOReturnSuccess);
  }
  U32 allocate(){
   s.memory=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,kIODirectionInOut|kIOMemoryPhysicallyContiguous,GSPFlush075::Page,GSPFlush075::Page);
   return static_cast<U32>(s.memory?kIOReturnSuccess:kIOReturnNoMemory);
  }
  U32 zero(){
   if(!s.memory||s.memory->getLength()!=GSPFlush075::Page||!s.memory->getBytesNoCopy())return static_cast<U32>(kIOReturnBadArgument);
   bzero(s.memory->getBytesNoCopy(),GSPFlush075::Page);return static_cast<U32>(kIOReturnSuccess);
  }
  U32 createDma(){
   s.dma=IODMACommand::withSpecification(kIODMACommandOutputHost64,40,GSPFlush075::Page,IODMACommand::kMapped,GSPFlush075::Page,GSPFlush075::Page,nullptr);
   return static_cast<U32>(s.dma?kIOReturnSuccess:kIOReturnNoMemory);
  }
  U32 prepareMemory(){return static_cast<U32>(s.memory->prepare(kIODirectionInOut));}
  U32 attach(){return static_cast<U32>(s.dma->setMemoryDescriptor(s.memory,false));}
  U32 prepareDma(){return static_cast<U32>(s.dma->prepare(0,GSPFlush075::Page));}
  U32 segment(U64&address,U64&bytes,U32&segments){
   UInt64 offset=0;UInt32 count=1;IODMACommand::Segment64 one={};
   const auto e=s.dma->gen64IOVMSegments(&offset,&one,&count);
   if(e!=kIOReturnSuccess)return static_cast<U32>(e);
   if(offset!=GSPFlush075::Page||count!=1)return static_cast<U32>(kIOReturnBadArgument);
   address=one.fIOVMAddr;bytes=one.fLength;segments=count;return static_cast<U32>(kIOReturnSuccess);
  }
  U32 physical(U64&address,U64&bytes){IOByteCount length=0;address=s.memory->getPhysicalSegment(0,&length);bytes=length;return static_cast<U32>(kIOReturnSuccess);}
  U32 synchronize(){__sync_synchronize();const auto e=s.dma->synchronize(kIODirectionOut);__sync_synchronize();return static_cast<U32>(e);}
  U32 verifyZero(){
   const auto*p=static_cast<const unsigned char*>(s.memory->getBytesNoCopy());if(!p)return static_cast<U32>(kIOReturnBadArgument);
   for(unsigned i=0;i<GSPFlush075::Page;++i)if(p[i])return static_cast<U32>(kIOReturnBadArgument);return static_cast<U32>(kIOReturnSuccess);
  }
  U32 completeDma(){return static_cast<U32>(s.dma->complete());}
  U32 clear(){return static_cast<U32>(s.dma->clearMemoryDescriptor(false));}
  U32 completeMemory(){return static_cast<U32>(s.memory->complete(kIODirectionInOut));}
  void releaseDma(){s.dma->release();s.dma=nullptr;}
  void releaseMemory(){s.memory->release();s.memory=nullptr;}
 };
public:
 GSPFlush075::Memory lifetime;
 explicit MacGSPFlushPage(IOPCIDevice*p):pci(p){}
 MacGSPFlushPage(const MacGSPFlushPage&)=delete;
 MacGSPFlushPage&operator=(const MacGSPFlushPage&)=delete;
 ~MacGSPFlushPage()=default;
 bool prepare(){Backend b{*this};return lifetime.prepare(b);}
 bool cleanup(){Backend b{*this};return lifetime.cleanup(b);}
};
