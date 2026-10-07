#pragma once
// Shared read-only identity transport for the dedicated GSP DMA rehearsal.
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <mach/kmod.h>
#include <kern/task.h>
#include "Boot0Protocol.hpp"
#include "GSPBooterPreflight.hpp"

class MacGSPIdentity {
  IOPCIDevice *pci;
  IOService *owner;
  IOMemoryMap *maps[2] = {};
public:
  Boot0::Facts capturedFacts;
  MacGSPIdentity(IOPCIDevice *device, IOService *client) : pci(device), owner(client) {}
  bool open() { return !pci->isOpen() && pci->open(owner); }
  void close() { pci->close(owner); }
  Boot0::Facts facts() {
    Boot0::Facts f;
    f.identity = pci->configRead32(0);
    f.subsystem = pci->configRead32(0x2c);
    f.targetBDF = pci->getBusNumber() == 1 && pci->getDeviceNumber() == 0 && pci->getFunctionNumber() == 0;
    if (f.identity != 0x252010de || f.subsystem != 0x104c1043 || !f.targetBDF) return capturedFacts = f;
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
    return capturedFacts = f;
  }
  bool mapPage() {
    IODeviceMemory *memory = pci->getDeviceMemoryWithRegister(0x10);
    if (!memory || memory->getLength() != (16ULL << 20) ||
        memory->getPhysicalAddress() != capturedFacts.bar0 || capturedFacts.bar0 > 0xff000000ULL) return false;
    const unsigned offsets[2] = {0, 0x824000};
    for (unsigned i = 0; i < 2; ++i) {
      maps[i] = memory->createMappingInTask(kernel_task, 0,
        kIOMapAnywhere | kIOMapInhibitCache | kIOMapReadOnly | kIOMapUnique, offsets[i], 4096);
      if (!maps[i] || !maps[i]->getVirtualAddress() || maps[i]->getLength() != 4096 ||
          maps[i]->getPhysicalAddress() != memory->getPhysicalAddress() + offsets[i]) {
        unmapPage(); return false;
      }
    }
    return true;
  }
  void unmapPage() {
    for (unsigned i = 0; i < 2; ++i) {
      if (maps[i]) { maps[i]->release(); maps[i] = nullptr; }
    }
  }
  unsigned command() { return pci->configRead16(4); }
  void setMemory(bool enabled) { pci->setMemoryEnable(enabled); }
  unsigned readBoot0() {
    if (!maps[0] || command() != 2) return 0xffffffffU;
    __sync_synchronize();
    const UInt32 value = *reinterpret_cast<volatile const UInt32 *>(maps[0]->getVirtualAddress());
    __sync_synchronize(); return value;
  }
  unsigned readBooterFuse() {
    if (!maps[1] || command() != 2) return 0xffffffffU;
    __sync_synchronize();
    const UInt32 value = *reinterpret_cast<volatile const UInt32 *>(maps[1]->getVirtualAddress() + 0x148);
    __sync_synchronize(); return value;
  }
};

