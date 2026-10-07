#pragma once
// One-shot board-specific FWSEC FRTS execution with persistent device ownership.
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <libkern/c++/OSData.h>
#include <mach/kmod.h>
#include <kern/task.h>
#include "Boot0Protocol.hpp"
#include "StateSnapshot.hpp"
#include "PreparationProtocol.hpp"
#include "FuseSelection.hpp"
#include "FWSECPreflight.hpp"

class MacGSP15Boot0 {
  IOPCIDevice *pci;
  IOService *owner;
  static constexpr unsigned MapCount = 9;
  const unsigned pages[MapCount] = {0, 0x118000, 0x1fa000, 0x824000,
    0x820000, 0x625000, 0x110000, 0x111000, 0x1000};
  IOMemoryMap *mappings[MapCount] = {};
  IOMemoryMap *romMapping = nullptr;
  UInt32 *romBuffer = nullptr;
public:
  MacGSP15Boot0(IOPCIDevice *device, IOService *client) : pci(device), owner(client) {}
  ~MacGSP15Boot0() { if (romBuffer) IOFree(romBuffer, Preparation::RomSize); }
  const void *romBytes() const { return romBuffer; }
  bool open() { return pci->isOpen(owner); }
  void close() {} // The GSP coordinator alone owns/closes the provider.
  Boot0::Facts facts() {
    Boot0::Facts f;
    f.identity = pci->configRead32(0);
    f.subsystem = pci->configRead32(0x2c);
    f.targetBDF = pci->getBusNumber() == 1 && pci->getDeviceNumber() == 0 && pci->getFunctionNumber() == 0;
    if (f.identity != 0x252010de || f.subsystem != 0x104c1043 || !f.targetBDF) return f;
    f.command = pci->configRead16(4);
    UInt8 offset = 0;
    if (pci->findPCICapability(1, &offset) && offset >= 0x40 && offset <= 0xf8)
      f.pmcsr = pci->configRead16(offset + 4);
    offset = 0;
    if (pci->findPCICapability(0x10, &offset) && offset >= 0x40 && offset <= 0xe8)
      f.link = pci->configRead16(offset + 0x12);
    const UInt32 low0 = pci->configRead32(0x10), low1 = pci->configRead32(0x14);
    f.barTypesValid = (low0 & 7) == 0 && (low1 & 15) == 12;
    f.bar0 = low0 & 0xfffffff0U;
    f.bar1 = (Boot0::U64(pci->configRead32(0x18)) << 32) | (low1 & 0xfffffff0U);
    if (IODeviceMemory *memory = pci->getDeviceMemoryWithRegister(0x10)) {
      f.descriptor0 = memory->getPhysicalAddress(); f.length0 = memory->getLength();
    }
    if (IODeviceMemory *memory = pci->getDeviceMemoryWithRegister(0x14)) {
      f.descriptor1 = memory->getPhysicalAddress(); f.length1 = memory->getLength();
    }
    return f;
  }
  bool mapPage() {
    IODeviceMemory *memory = pci->getDeviceMemoryWithRegister(0x10);
    if (!memory) return false;
    for (unsigned i = 0; i < MapCount; ++i) {
      const unsigned offset = pages[i];
      mappings[i] = memory->createMappingInTask(kernel_task, 0,
        kIOMapAnywhere | kIOMapInhibitCache | kIOMapReadOnly | kIOMapUnique, offset, 4096);
      if (!mappings[i] || !mappings[i]->getVirtualAddress() || mappings[i]->getLength() != 4096 ||
          mappings[i]->getPhysicalAddress() != memory->getPhysicalAddress() + offset) {
        unmapPage(); return false;
      }
    }
    romMapping = memory->createMappingInTask(kernel_task, 0,
      kIOMapAnywhere | kIOMapInhibitCache | kIOMapReadOnly | kIOMapUnique,
      Preparation::RomOffset, Preparation::RomSize);
    if (!romMapping || !romMapping->getVirtualAddress() || romMapping->getLength() != Preparation::RomSize ||
        romMapping->getPhysicalAddress() != memory->getPhysicalAddress() + Preparation::RomOffset) {
      unmapPage(); return false;
    }
    return true;
  }
  void unmapPage() {
    if (romMapping) { romMapping->release(); romMapping = nullptr; }
    for (unsigned i = 0; i < MapCount; ++i) {
      if (mappings[i]) { mappings[i]->release(); mappings[i] = nullptr; }
    }
  }
  unsigned command() { return pci->configRead16(4); }
  bool allocateRom() {
    romBuffer = static_cast<UInt32 *>(IOMalloc(Preparation::RomSize));
    return romBuffer != nullptr;
  }
  unsigned romWord(unsigned offset) {
    if (!romMapping || (offset & 3) || offset > Preparation::RomSize - 4) return 0xffffffffU;
    return *reinterpret_cast<volatile const UInt32 *>(romMapping->getVirtualAddress() + offset);
  }
  void storeRom(unsigned offset, unsigned value) { romBuffer[offset / 4] = value; }
  unsigned savedRomWord(unsigned offset) { return romBuffer[offset / 4]; }
  void setMemory(bool enabled) { pci->setMemoryEnable(enabled); }
  unsigned readFuse() {
    __sync_synchronize();
    const UInt32 value = *reinterpret_cast<volatile const UInt32 *>(
      mappings[3]->getVirtualAddress() + (FWSECFuse::Offset & 4095U));
    __sync_synchronize(); return value;
  }
  unsigned readBoot0() {
    // x86_64 host and NVIDIA use little-endian. Exactly one volatile 32-bit load.
    __sync_synchronize();
    const UInt32 value = *reinterpret_cast<volatile const UInt32 *>(mappings[0]->getVirtualAddress());
    __sync_synchronize();
    return value;
  }
  unsigned readPreflight(unsigned index) {
    if (index >= FWSECPreflight::Count) return 0xffffffffU;
    const unsigned offset = FWSECPreflight::Offsets[index];
    for (unsigned p = 0; p < MapCount; ++p) {
      if ((offset & ~4095U) != pages[p] || !mappings[p]) continue;
      __sync_synchronize();
      const UInt32 value = *reinterpret_cast<volatile const UInt32 *>(
        mappings[p]->getVirtualAddress() + (offset & 4095U));
      __sync_synchronize();
      return value;
    }
    return 0xffffffffU;
  }
  unsigned readFixed(unsigned index) {
    if (index >= GPUState::Count) return 0xffffffffU;
    const unsigned offset = GPUState::Offsets[index];
    for (unsigned p = 0; p < 3; ++p) {
      if ((offset & ~4095U) != GPUState::Pages[p] || !mappings[p]) continue;
      __sync_synchronize();
      const UInt32 value = *reinterpret_cast<volatile const UInt32 *>(
        mappings[p]->getVirtualAddress() + (offset & 4095U));
      __sync_synchronize();
      return value;
    }
    return 0xffffffffU;
  }
};


