#pragma once
#include "MacGSP15Boot0.hpp"
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODMACommand.h>
#include <IOKit/IOMapper.h>
#include "PreparationProtocol.hpp"
#include "HostReadProtocol.hpp"
#include "FalconProtocol.hpp"

// Native IOKit mapping rehearsal. No MMIO mapping, GPU transfer or PCI writes.
class MacGSP15Dma {
  IOPCIDevice *pci;
  MacGSP15Boot0 checks;
  IOMapper *mapper = nullptr;
  IOBufferMemoryDescriptor *memory = nullptr;
  IODMACommand *dma = nullptr;
  bool memoryPrepared = false, dmaPrepared = false;
  const unsigned char *fwsecImage = nullptr;
  unsigned fwsecImageSize = 0, fwsecWindow = 0;
public:
  const char *mapperMode = "not-selected";
  IOReturn lastError = 0, clearError = 0, completeError = 0, memoryCompleteError = 0;
  IOReturn publishError[2] = {kIOReturnNotReady, kIOReturnNotReady};
  IOReturn fwsecPublishError = kIOReturnNotReady;
  MacGSP15Dma(IOPCIDevice *p, IOService *owner) : pci(p), checks(p, owner) {}
  bool open() { return checks.open(); }
  void close() { checks.close(); }
  Boot0::Facts facts() { return checks.facts(); }
  unsigned command() { return pci->configRead16(4); }
  bool selectMapper() {
    // First launch uses the same proven direct-map profile as the nine GSP
    // buffers. Never wait on unresolved mapper state in a kernel transaction.
    if(IOMapper::gSystem){mapperMode="launch-requires-system-no-mapper";return false;}
    // copyMapperForDevice may wait indefinitely for an unresolved provider.
    // Only accept an already resolved device mapper, or the settled system choice.
    OSObject *parent = pci->copyProperty("iommu-parent");
    if (parent) {
      parent->release();mapperMode="launch-requires-no-device-mapper";return false;
    } else {
      IOMapper *system = IOMapper::gSystem;
      // SDK IOMapper.h uses low-bit sentinel values 1/2 for pending state.
      if (reinterpret_cast<uintptr_t>(system) & 3) { mapperMode = "system-mapper-pending"; return false; }
      mapper = system;
      if (mapper) { mapper->retain(); mapperMode = "system-mapper"; }
      else mapperMode = "system-no-mapper";
    }
    return true;
  }
  static unsigned pattern(unsigned index) { return 0x52745834U ^ (index * 2654435761U); }
  bool allocate() {
    memory = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, kIODirectionOut,
      Preparation::DmaSize, Preparation::PageSize);
    if (!memory || memory->getLength() != Preparation::DmaSize || !memory->getBytesNoCopy()) return false;
    unsigned *words = static_cast<unsigned *>(memory->getBytesNoCopy());
    for (unsigned i = 0; i < Preparation::DmaSize / 4; ++i) words[i] = pattern(i);
    dma = IODMACommand::withSpecification(kIODMACommandOutputHost64, 40, Preparation::PageSize,
      IODMACommand::kMapped, Preparation::DmaSize, Preparation::PageSize, mapper);
    return dma != nullptr;
  }
  bool prepare() {
    lastError = memory->prepare(kIODirectionOut);
    if (lastError != kIOReturnSuccess) return false;
    memoryPrepared = true;
    lastError = dma->setMemoryDescriptor(memory, false);
    if (lastError != kIOReturnSuccess) return false;
    lastError = dma->prepare(0, Preparation::DmaSize);
    dmaPrepared = lastError == kIOReturnSuccess;
    return dmaPrepared;
  }
  bool segments(Preparation::Segment *out, unsigned &count, Boot0::U64 &end) {
    IODMACommand::Segment64 rows[Preparation::DmaPages] = {};
    UInt32 n = Preparation::DmaPages;
    UInt64 offset = 0;
    lastError = dma->gen64IOVMSegments(&offset, rows, &n);
    if (n > Preparation::DmaPages) return false;
    count = n; end = offset;
    for (unsigned i = 0; i < n; ++i) {
      out[i].address = rows[i].fIOVMAddr; out[i].length = rows[i].fLength;
      IOByteCount length=0;
      if(!memory || memory->getPhysicalSegment(i*Preparation::PageSize,&length)!=out[i].address ||
         length<Preparation::PageSize)return false;
    }
    return lastError == kIOReturnSuccess;
  }
  bool verifyCPU() {
    const unsigned *words = static_cast<const unsigned *>(memory->getBytesNoCopy());
    for (unsigned i = 0; i < Preparation::DmaSize / 4; ++i) if (words[i] != pattern(i)) return false;
    return true;
  }
  bool publish(unsigned phase) {
    if (phase > 1 || !dmaPrepared || !memory) return false;
    unsigned *words = static_cast<unsigned *>(memory->getBytesNoCopy());
    for (unsigned i = 0; i < HostRead::Words; ++i) words[i] = HostRead::pattern(i, phase);
    __sync_synchronize();
    publishError[phase] = dma->synchronize(kIODirectionOut);
    __sync_synchronize();
    return publishError[phase] == kIOReturnSuccess;
  }
  bool verifyPublished() {
    const unsigned *words = static_cast<const unsigned *>(memory->getBytesNoCopy());
    for (unsigned i = 0; i < Preparation::DmaSize / 4; ++i)
      if (words[i] != (i < HostRead::Words ? HostRead::pattern(i, 1) : pattern(i))) return false;
    return true;
  }
  bool publishFalcon(unsigned phase) {
    if (phase > 1 || !dmaPrepared || !memory) return false;
    unsigned *words = static_cast<unsigned *>(memory->getBytesNoCopy());
    for (unsigned i = 0; i < FalconDMA::Words; ++i) words[i] = FalconDMA::pattern(i, phase);
    __sync_synchronize();
    publishError[phase] = dma->synchronize(kIODirectionOut);
    __sync_synchronize();
    return publishError[phase] == kIOReturnSuccess;
  }
  bool verifyFalcon() {
    const unsigned *words = static_cast<const unsigned *>(memory->getBytesNoCopy());
    for (unsigned i = 0; i < Preparation::DmaSize / 4; ++i)
      if (words[i] != (i < FalconDMA::Words ? FalconDMA::pattern(i, 1) : pattern(i))) return false;
    return true;
  }
  bool publishFWSECImage(const unsigned char *image, unsigned size, unsigned offset) {
    if (!dmaPrepared || !memory || !image || size != 59648 || offset >= size ||
        offset % Preparation::DmaSize != 0) return false;
    // Caller has proven the preceding DMA idle before reusing this window.
    unsigned *words = static_cast<unsigned *>(memory->getBytesNoCopy());
    for (unsigned i = 0; i < Preparation::DmaSize / 4; ++i) words[i] = pattern(i);
    const unsigned bytes = size - offset < Preparation::DmaSize ? size - offset : Preparation::DmaSize;
    auto *out = reinterpret_cast<unsigned char *>(words);
    for (unsigned i = 0; i < bytes; ++i) out[i] = image[offset + i];
    fwsecImage = image; fwsecImageSize = size; fwsecWindow = offset;
    __sync_synchronize();
    fwsecPublishError = dma->synchronize(kIODirectionOut);
    __sync_synchronize();
    return fwsecPublishError == kIOReturnSuccess;
  }
  bool verifyFWSECImage() {
    if (!memory || !fwsecImage) return false;
    const auto *bytes = static_cast<const unsigned char *>(memory->getBytesNoCopy());
    for (unsigned i = 0; i < Preparation::DmaSize; ++i) {
      const unsigned expected = fwsecWindow + i < fwsecImageSize ? fwsecImage[fwsecWindow + i] :
        ((pattern(i / 4) >> ((i % 4) * 8)) & 255);
      if (bytes[i] != expected) return false;
    }
    return true;
  }
  bool cleanup() {
    if (dmaPrepared) {
      completeError = dma->complete();
      if (completeError != kIOReturnSuccess) return false;
      dmaPrepared = false;
    }
    // Also clears a descriptor retained before an unsuccessful prepare.
    if (dma) {
      clearError = dma->clearMemoryDescriptor();
      if (clearError != kIOReturnSuccess) return false;
    }
    if (memoryPrepared) {
      memoryCompleteError = memory->complete(kIODirectionOut);
      if (memoryCompleteError != kIOReturnSuccess) return false;
      memoryPrepared = false;
    }
    // A failed completion retains every remaining object until recovery.
    if (dma) { dma->release(); dma = nullptr; }
    if (memory) { memory->release(); memory = nullptr; }
    if (mapper) { mapper->release(); mapper = nullptr; }
    return clearError == kIOReturnSuccess && completeError == kIOReturnSuccess && memoryCompleteError == kIOReturnSuccess;
  }
};
