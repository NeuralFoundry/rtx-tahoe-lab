#pragma once
#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODMACommand.h>
#include <IOKit/IOMapper.h>
#include <IOKit/pci/IOPCIDevice.h>
#include "GSPDmaProtocol.hpp"

// Kernel-owned host allocation rehearsal only. No PCI/MMIO access, engine
// submission, firmware execution, provider open/close, or user memory mapping.
// The caller owns the provider and serializes every call in client context;
// IODMACommand::prepare() must not run under a workloop gate or a spin lock.
class MacGSPDma {
public:
  using U32 = GSPDmaProtocol::U32;
  using U64 = GSPDmaProtocol::U64;
  struct Row {
    U64 bytes = 0;
    U32 pageCount = 0, pagesEnumerated = 0;
    bool allocated = false, dmaAllocated = false, memoryPrepared = false;
    bool descriptorAttached = false, dmaPrepared = false, everPrepared = false;
    bool synchronized = false, retained = false;
    IOReturn allocationError = kIOReturnNotReady, commandError = kIOReturnNotReady;
    IOReturn memoryPrepareError = kIOReturnNotReady, setDescriptorError = kIOReturnNotReady;
    IOReturn prepareError = kIOReturnNotReady, segmentError = kIOReturnNotReady;
    IOReturn synchronizeError = kIOReturnNotReady, completeError = kIOReturnNotReady;
    IOReturn clearError = kIOReturnNotReady, memoryCompleteError = kIOReturnNotReady;
  };
  const char *mapperMode = "not-selected";
  IOReturn lastError = kIOReturnSuccess;
  GSPDmaProtocol::Error pageValidationError = GSPDmaProtocol::Error::Ok;

private:
  static constexpr U32 Count = GSPDmaProtocol::ResourceCount;
  static constexpr U32 SegmentBatch = 32;
  static constexpr U64 PageArrayBytes = GSPDmaProtocol::TotalPages * sizeof(U64);
  IOPCIDevice *pci;
  IOMapper *mapper = nullptr;
  IOBufferMemoryDescriptor *memory[Count] = {};
  IODMACommand *dma[Count] = {};
  Row rows[Count] = {};
  const Row invalidRow = {};
  U64 *pageAddresses = nullptr;
  U64 *sortScratch = nullptr;
  bool started = false, isReady = false, cleanupAttempted = false;
  bool cleanupSucceeded = false, retainedResources = false;

  MacGSPDma(const MacGSPDma &) = delete;
  MacGSPDma &operator=(const MacGSPDma &) = delete;

  bool fail(IOReturn error) {
    lastError = error;
    isReady = false;
    return false;
  }
  bool selectMapper() {
    if (!pci) return fail(kIOReturnBadArgument);
    // IOBufferMemoryDescriptor also consults the system mapper internally,
    // even when our DMA command will use an already-resolved device mapper.
    // Reject the pending sentinels before either selection path can allocate.
    IOMapper *system = IOMapper::gSystem;
    if (reinterpret_cast<uintptr_t>(system) & 3) {
      mapperMode = "system-mapper-pending";
      return fail(kIOReturnNotReady);
    }
    // Reuse the already-settled mapper policy from MacDmaPreparation.
    // copyMapperForDevice() can wait indefinitely for an unresolved provider.
    OSObject *parent = pci->copyProperty("iommu-parent");
    if (parent) {
      mapper = OSDynamicCast(IOMapper, parent);
      if (!mapper) {
        parent->release();
        mapperMode = "device-parent-unresolved";
        return fail(kIOReturnNotReady);
      }
      mapperMode = "device-mapper"; // copyProperty supplied the retained reference.
    } else {
      mapper = system;
      if (mapper) {
        mapper->retain();
        mapperMode = "system-mapper";
      } else mapperMode = "system-no-mapper";
    }
    return true;
  }
  bool allocateOne(U32 resource) {
    Row &r = rows[resource];
    IOOptionBits options = kIODirectionInOut;
    if (resource == GSPDmaProtocol::Bootloader || resource == GSPDmaProtocol::Logs)
      options |= kIOMemoryPhysicallyContiguous;
    memory[resource] = IOBufferMemoryDescriptor::inTaskWithOptions(
      kernel_task, options, static_cast<vm_size_t>(r.bytes), GSPDmaProtocol::Page);
    r.allocated = memory[resource] != nullptr;
    r.allocationError = r.allocated ? kIOReturnSuccess : kIOReturnNoMemory;
    if (!r.allocated) return fail(r.allocationError);
    if (memory[resource]->getLength() != r.bytes || !memory[resource]->getBytesNoCopy()) {
      r.allocationError = kIOReturnBadArgument;
      return fail(r.allocationError);
    }
    // No uninitialized kernel data is ever returned through copyOut().
    bzero(memory[resource]->getBytesNoCopy(), static_cast<size_t>(r.bytes));
    dma[resource] = IODMACommand::withSpecification(kIODMACommandOutputHost64, 40,
      GSPDmaProtocol::Page, IODMACommand::kMapped, r.bytes, GSPDmaProtocol::Page, mapper);
    r.dmaAllocated = dma[resource] != nullptr;
    r.commandError = r.dmaAllocated ? kIOReturnSuccess : kIOReturnNoMemory;
    return r.dmaAllocated ? true : fail(r.commandError);
  }
  bool prepareOne(U32 resource) {
    Row &r = rows[resource];
    r.memoryPrepareError = memory[resource]->prepare(kIODirectionInOut);
    if (r.memoryPrepareError != kIOReturnSuccess) return fail(r.memoryPrepareError);
    r.memoryPrepared = true;
    r.setDescriptorError = dma[resource]->setMemoryDescriptor(memory[resource], false);
    if (r.setDescriptorError != kIOReturnSuccess) return fail(r.setDescriptorError);
    r.descriptorAttached = true;
    r.prepareError = dma[resource]->prepare(0, r.bytes);
    if (r.prepareError != kIOReturnSuccess) return fail(r.prepareError);
    r.dmaPrepared = r.everPrepared = true;
    return true;
  }
  bool enumerateOne(U32 resource, U32 basePage) {
    Row &r = rows[resource];
    UInt64 offset = 0;
    // Only a fixed 512-byte segment batch is on the kernel stack. The 16,212
    // exported addresses and duplicate-detection scratch reside on the heap.
    IODMACommand::Segment64 batch[SegmentBatch] = {};
    for (U32 calls = 0; calls < r.pageCount && offset < r.bytes; ++calls) {
      const UInt64 before = offset;
      const U32 remaining = r.pageCount - r.pagesEnumerated;
      UInt32 count = remaining < SegmentBatch ? remaining : SegmentBatch;
      const UInt32 capacity = count;
      r.segmentError = dma[resource]->gen64IOVMSegments(&offset, batch, &count);
      if (r.segmentError != kIOReturnSuccess) return fail(r.segmentError);
      if (!count || count > capacity || offset <= before || offset > r.bytes ||
          offset - before != static_cast<U64>(count) * GSPDmaProtocol::Page) {
        r.segmentError = kIOReturnBadArgument;
        return fail(r.segmentError);
      }
      for (UInt32 index = 0; index < count; ++index) {
        const U64 address = batch[index].fIOVMAddr;
        if (batch[index].fLength != GSPDmaProtocol::Page || !address ||
            address % GSPDmaProtocol::Page || address > (1ULL << 40) - GSPDmaProtocol::Page) {
          r.segmentError = kIOReturnBadArgument;
          return fail(r.segmentError);
        }
        pageAddresses[basePage + r.pagesEnumerated++] = address;
      }
    }
    if (offset != r.bytes || r.pagesEnumerated != r.pageCount) {
      r.segmentError = kIOReturnUnderrun;
      return fail(r.segmentError);
    }
    return true;
  }
  bool validCopy(U32 resource, U64 offset, const void *data, U32 length) const {
    return isReady && !cleanupAttempted && resource < Count && data && length &&
      length <= GSPDmaProtocol::Page && memory[resource] && rows[resource].dmaPrepared &&
      offset <= rows[resource].bytes && length <= rows[resource].bytes - offset;
  }

public:
  explicit MacGSPDma(IOPCIDevice *provider) : pci(provider) {
    for (U32 resource = 0; resource < Count; ++resource) {
      rows[resource].bytes = GSPDmaProtocol::Sizes[resource];
      rows[resource].pageCount = GSPDmaProtocol::Pages[resource];
    }
  }
  // Deliberately no implicit cleanup: the caller must establish its command=0
  // guard before cleanup, inspect its result, and keep this object/provider
  // alive when any completion failed. Destroying it cannot recover ownership.
  ~MacGSPDma() = default;
  bool ready() const { return isReady; }
  bool resourcesRetained() const { return retainedResources; }
  bool begun() const { return started; }
  const U64 *pages() const { return isReady ? pageAddresses : nullptr; }
  const Row &row(U32 resource) const { return resource < Count ? rows[resource] : invalidRow; }

  bool begin() {
    if (started || cleanupAttempted) { lastError = kIOReturnBusy; return false; }
    started = true;
    if (!selectMapper()) return false;
    pageAddresses = static_cast<U64 *>(IOMalloc(static_cast<vm_size_t>(PageArrayBytes)));
    sortScratch = static_cast<U64 *>(IOMalloc(static_cast<vm_size_t>(PageArrayBytes)));
    if (!pageAddresses || !sortScratch) return fail(kIOReturnNoMemory);
    bzero(pageAddresses, static_cast<size_t>(PageArrayBytes));
    U32 basePage = 0;
    for (U32 resource = 0; resource < Count; ++resource) {
      if (!allocateOne(resource) || !prepareOne(resource) || !enumerateOne(resource, basePage)) return false;
      basePage += rows[resource].pageCount;
    }
    pageValidationError = GSPDmaProtocol::validatePages(pageAddresses, basePage,
      sortScratch, GSPDmaProtocol::TotalPages);
    if (pageValidationError != GSPDmaProtocol::Error::Ok) return fail(kIOReturnBadArgument);
    IOFree(sortScratch, static_cast<vm_size_t>(PageArrayBytes));
    sortScratch = nullptr;
    isReady = true;
    lastError = kIOReturnSuccess;
    return true;
  }
  bool copyIn(U32 resource, U64 offset, const void *data, U32 length) {
    if (!validCopy(resource, offset, data, length)) { lastError = kIOReturnBadArgument; return false; }
    auto *destination = static_cast<unsigned char *>(memory[resource]->getBytesNoCopy());
    if (!destination) return fail(kIOReturnNotReady);
    bcopy(data, destination + static_cast<size_t>(offset), length);
    rows[resource].synchronized = false;
    lastError = kIOReturnSuccess;
    return true;
  }
  bool copyOut(U32 resource, U64 offset, void *data, U32 length) {
    if (!validCopy(resource, offset, data, length)) { lastError = kIOReturnBadArgument; return false; }
    const auto *source = static_cast<const unsigned char *>(memory[resource]->getBytesNoCopy());
    if (!source) return fail(kIOReturnNotReady);
    bcopy(source + static_cast<size_t>(offset), data, length);
    lastError = kIOReturnSuccess;
    return true;
  }
  bool synchronizeAll() {
    if (!isReady || cleanupAttempted) { lastError = kIOReturnNotReady; return false; }
    for (U32 resource = 0; resource < Count; ++resource) rows[resource].synchronized = false;
    __sync_synchronize();
    for (U32 resource = 0; resource < Count; ++resource) {
      Row &r = rows[resource];
      // Out publishes uploaded CPU bytes to any IODMACommand bounce buffer.
      // Apple rejects simultaneous In|Out synchronization; In alone would
      // import bounce-buffer data over the uploaded CPU bytes.
      r.synchronizeError = dma[resource]->synchronize(kIODirectionOut);
      if (r.synchronizeError != kIOReturnSuccess) return fail(r.synchronizeError);
      r.synchronized = true;
    }
    __sync_synchronize();
    lastError = kIOReturnSuccess;
    return true;
  }
  bool cleanup() {
    if (cleanupAttempted) return cleanupSucceeded;
    cleanupAttempted = true;
    isReady = false;
    bool good = true;
    IOReturn firstError = kIOReturnSuccess;
    // Complete/clear every resource before releasing ANY object. Each failed
    // operation prevents dependent operations on that resource. A sticky
    // failure retains all objects and the mapper; repeated cleanup is inert.
    for (U32 resource = 0; resource < Count; ++resource) {
      Row &r = rows[resource];
      if (r.dmaPrepared) {
        r.completeError = dma[resource]->complete();
        if (r.completeError != kIOReturnSuccess) {
          if (good) firstError = r.completeError;
          good = false;
          continue;
        }
        r.dmaPrepared = false;
      }
      // Also clear after unsuccessful set/prepare: the command may retain a
      // descriptor even when preparation did not complete successfully.
      if (dma[resource]) {
        r.clearError = dma[resource]->clearMemoryDescriptor(false);
        if (r.clearError != kIOReturnSuccess) {
          if (good) firstError = r.clearError;
          good = false;
          continue;
        }
        r.descriptorAttached = false;
      }
      if (r.memoryPrepared) {
        r.memoryCompleteError = memory[resource]->complete(kIODirectionInOut);
        if (r.memoryCompleteError != kIOReturnSuccess) {
          if (good) firstError = r.memoryCompleteError;
          good = false;
          continue;
        }
        r.memoryPrepared = false;
      }
    }
    if (!good) {
      retainedResources = true;
      for (U32 resource = 0; resource < Count; ++resource)
        rows[resource].retained = memory[resource] || dma[resource];
      lastError = firstError;
      return false;
    }
    for (U32 resource = 0; resource < Count; ++resource) {
      if (dma[resource]) { dma[resource]->release(); dma[resource] = nullptr; }
      if (memory[resource]) { memory[resource]->release(); memory[resource] = nullptr; }
      rows[resource].allocated = rows[resource].dmaAllocated = rows[resource].retained = false;
    }
    if (pageAddresses) { IOFree(pageAddresses, static_cast<vm_size_t>(PageArrayBytes)); pageAddresses = nullptr; }
    if (sortScratch) { IOFree(sortScratch, static_cast<vm_size_t>(PageArrayBytes)); sortScratch = nullptr; }
    if (mapper) { mapper->release(); mapper = nullptr; }
    cleanupSucceeded = true;
    retainedResources = false;
    lastError = kIOReturnSuccess;
    return true;
  }
};
