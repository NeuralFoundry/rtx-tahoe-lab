#pragma once
#include "MacDmaPreparation.hpp"
class MacFalcon : public MacDmaPreparation {
  IOPCIDevice *pci;
  IOMemoryMap *maps[4] = {};
  const bool fwsecStage;
  static unsigned page(unsigned i) {
    const unsigned pages[] = {0x110000, 0x111000, 0x118000, 0x1fa000};
    return pages[i];
  }
protected:
  unsigned rawRead(unsigned offset) {
    for (unsigned i = 0; i < 4; ++i) {
      if (maps[i] && (offset & ~4095U) == page(i)) {
        __sync_synchronize();
        const unsigned value = *reinterpret_cast<volatile const UInt32 *>(maps[i]->getVirtualAddress() + (offset & 4095));
        __sync_synchronize(); return value;
      }
    }
    return 0xffffffffU;
  }
  // Only the two already mapped Falcon pages are writable. Derived firmware
  // adapters must additionally restrict each caller to its fixed register/value.
  bool writeFalconPage(unsigned offset, unsigned value) {
    if (offset < FalconDMA::Base || offset >= FalconDMA::Base + 8192) return false;
    const unsigned relative = offset - FalconDMA::Base, index = relative / 4096;
    if ((offset & 3) || !maps[index]) return false;
    __sync_synchronize();
    *reinterpret_cast<volatile UInt32 *>(maps[index]->getVirtualAddress() + (relative & 4095)) = value;
    __sync_synchronize();
    return true;
  }
public:
  MacFalcon(IOPCIDevice *p, IOService *owner, bool stage = false)
    : MacDmaPreparation(p, owner), pci(p), fwsecStage(stage) {}
  void setMemory(bool enabled) { pci->setMemoryEnable(enabled); }
  void setMaster(bool enabled) { pci->setBusMasterEnable(enabled); }
  void delayUs(unsigned us) { if (us <= 100) IODelay(us); }
  unsigned deviceStatus() {
    UInt8 offset = 0;
    if (!pci->findPCICapability(0x10, &offset) || offset < 0x40 || offset > 0xe8) return 0xffff;
    return pci->configRead16(offset + 0x0a);
  }
  bool mapFalcon() {
    IODeviceMemory *memory = pci->getDeviceMemoryWithRegister(0x10);
    if (!memory) return false;
    for (unsigned i = 0; i < 4; ++i) {
      maps[i] = memory->createMappingInTask(kernel_task, 0,
        kIOMapAnywhere | kIOMapInhibitCache | kIOMapUnique | (i >= 2 ? kIOMapReadOnly : 0), page(i), 4096);
      if (!maps[i] || !maps[i]->getVirtualAddress() || maps[i]->getLength() != 4096 ||
          maps[i]->getPhysicalAddress() != memory->getPhysicalAddress() + page(i)) {
        unmapFalcon(); return false;
      }
    }
    return true;
  }
  void unmapFalcon() {
    for (unsigned i = 0; i < 4; ++i) if (maps[i]) { maps[i]->release(); maps[i] = nullptr; }
  }
  bool environmentReady(unsigned *out) {
    out[0] = rawRead(0x1fa828); out[1] = rawRead(0x118128);
    if (!FalconDMA::readable(out[0]) || !FalconDMA::readable(out[1]) || out[0] != 0 || !(out[1] & 1)) return false;
    out[2] = rawRead(0x118234);
    return FalconDMA::readable(out[2]) && (out[2] & 0xff) == 0xff;
  }
  unsigned read(FalconDMA::Reg reg) {
    if (unsigned(reg) >= FalconDMA::Count) return 0xffffffffU;
    return rawRead(FalconDMA::Base + FalconDMA::Offsets[reg]);
  }
  void write(FalconDMA::Reg reg, unsigned value) {
    using namespace FalconDMA;
    switch (reg) {
      case Engine: case TRANSCFG: case FBIFCTL: case DMACTL: case DMABASE: case DMABASE1:
      case DMAOFFSET: case FBOFFSET: case DMEMD: break;
      case BCR: if (value != 0) return; break;
      case DMEMC: if (value >= (fwsecStage ? 2048U : Bytes) || (value & 3)) return; break;
      case DMACMD: if (value != 0x600 && !(fwsecStage && value == 0x614)) return; break;
      default: return; // No CPU start, firmware/BROM, interrupt or reset-global writes.
    }
    const unsigned offset = Offsets[reg], map = offset / 4096;
    if (map > 1 || !maps[map]) return;
    __sync_synchronize();
    *reinterpret_cast<volatile UInt32 *>(maps[map]->getVirtualAddress() + (offset & 4095)) = value;
    __sync_synchronize();
  }
};
