#pragma once
#include "MacDmaPreparation.hpp"
class MacHostRead : public MacDmaPreparation {
  IOPCIDevice *pci;
  IOMemoryMap *windowMap = nullptr, *apertureMap = nullptr;
public:
  MacHostRead(IOPCIDevice *p, IOService *owner) : MacDmaPreparation(p, owner), pci(p) {}
  void setMemory(bool enabled) { pci->setMemoryEnable(enabled); }
  void setMaster(bool enabled) { pci->setBusMasterEnable(enabled); }
  bool mapHostPage(Boot0::U64 address) {
    IODeviceMemory *memory = pci->getDeviceMemoryWithRegister(0x10);
    if (!memory || !address || (address & 4095) || address > (1ULL << 40) - 4096) return false;
    const unsigned aperture = HostRead::ApertureOffset + unsigned(address & 0xffff);
    windowMap = memory->createMappingInTask(kernel_task, 0,
      kIOMapAnywhere | kIOMapInhibitCache | kIOMapUnique, 0x1000, 4096);
    apertureMap = memory->createMappingInTask(kernel_task, 0,
      kIOMapAnywhere | kIOMapInhibitCache | kIOMapUnique | kIOMapReadOnly, aperture, 4096);
    if (!windowMap || !apertureMap || !windowMap->getVirtualAddress() || !apertureMap->getVirtualAddress() ||
        windowMap->getLength() != 4096 || apertureMap->getLength() != 4096 ||
        windowMap->getPhysicalAddress() != memory->getPhysicalAddress() + 0x1000 ||
        apertureMap->getPhysicalAddress() != memory->getPhysicalAddress() + aperture) {
      unmapHostPage(); return false;
    }
    return true;
  }
  void unmapHostPage() {
    if (apertureMap) { apertureMap->release(); apertureMap = nullptr; }
    if (windowMap) { windowMap->release(); windowMap = nullptr; }
  }
  unsigned readWindow() {
    __sync_synchronize();
    const unsigned value = *reinterpret_cast<volatile const UInt32 *>(windowMap->getVirtualAddress() + 0x700);
    __sync_synchronize(); return value;
  }
  void writeWindow(unsigned value) {
    // The only GPU register write in this adapter. No arbitrary-offset API.
    __sync_synchronize();
    *reinterpret_cast<volatile UInt32 *>(windowMap->getVirtualAddress() + 0x700) = value;
    __sync_synchronize();
  }
  unsigned readHostWord(unsigned index) {
    if (index >= HostRead::Words) return 0xffffffffU;
    __sync_synchronize();
    const unsigned value = *reinterpret_cast<volatile const UInt32 *>(apertureMap->getVirtualAddress() + index * 4);
    __sync_synchronize(); return value;
  }
};
