#pragma once
#include "MacGSPBorrowedFWSECStage.hpp"
#include "FWSECExecution.hpp"
#include "FWSECRegion.hpp"
#include "GSPLaunchOwnership.hpp"

// This object is a service member and survives an attempted firmware start.
// Its one-region ledger excludes BIOS workspace; it exposes no VRAM allocator.
class MacGSPBorrowedFWSEC : public MacGSPBorrowedFWSECStage {
  IOPCIDevice *device;
  IOService *client;
  bool opened = false, pinned = false, rechecked = false;
  FWSECRegion::Ledger reservation;
  GSPLaunchOwnership::Ledger &fullReservation;
  const unsigned long long generation;
  static constexpr unsigned ExtraCount = 8;
  const unsigned pages[ExtraCount] = {0, 0x1000, 0x820000, 0x824000, 0x625000, 0x610000, 0x612000, 0x613000};
  IOMemoryMap *extraMaps[ExtraCount] = {};
  unsigned fixedRead(unsigned offset) {
    for (unsigned i = 0; i < ExtraCount; ++i) {
      if ((offset & ~4095U) != pages[i] || !extraMaps[i]) continue;
      __sync_synchronize();
      const unsigned value = *reinterpret_cast<volatile const UInt32 *>(extraMaps[i]->getVirtualAddress() + (offset & 4095));
      __sync_synchronize(); return value;
    }
    return rawRead(offset);
  }
  bool captureFresh(FWSECPreflight::Snapshot &region, FWSECDisplay::Snapshot &heads) {
    if (!providerHeld() || command() != 2) return false;
    const unsigned vram1 = fixedRead(0x1183a4), vram2 = fixedRead(0x1183a4);
    const unsigned fuse1 = fixedRead(0x8241e0), fuse2 = fixedRead(0x8241e0);
    if (vram1 != 6144 || vram1 != vram2 || fuse1 != 3 || fuse2 != 3 || command() != 2) return false;
    if (region.capture(*this, vram1) || heads.capture(*this)) return false;
    return true;
  }
public:
  FWSECPreflight::Snapshot fresh;
  FWSECDisplay::Snapshot display;
  unsigned recheckPolls = 0;
  MacGSPBorrowedFWSEC(IOPCIDevice *pci, IOService *owner, const void *rom, unsigned fuse, unsigned signature, GSPLaunchOwnership::Ledger &full, unsigned long long gen)
    : MacGSPBorrowedFWSECStage(pci, owner, rom, fuse, signature), device(pci), client(owner), fullReservation(full), generation(gen) {}
  bool open() { return opened = MacGSPBorrowedFWSECStage::open(); }
  bool providerHeld() const { return opened && device->isOpen(client); }
  bool regionOwned() const { return reservation.owned(); }
  bool regionPersistent() const { return reservation.persistent(); }
  void close() {
    if (reservation.persistent()) return;
    if (opened) MacGSPBorrowedFWSECStage::close();
    opened = false;
    reservation.releaseBeforeStart();
  }
  bool mapFalcon() {
    if (!MacGSPBorrowedFWSECStage::mapFalcon()) return false;
    IODeviceMemory *memory = device->getDeviceMemoryWithRegister(0x10);
    if (!memory) { unmapFalcon(); return false; }
    for (unsigned i = 0; i < ExtraCount; ++i) {
      extraMaps[i] = memory->createMappingInTask(kernel_task, 0,
        kIOMapAnywhere | kIOMapInhibitCache | kIOMapReadOnly | kIOMapUnique, pages[i], 4096);
      if (!extraMaps[i] || !extraMaps[i]->getVirtualAddress() || extraMaps[i]->getLength() != 4096 ||
          extraMaps[i]->getPhysicalAddress() != memory->getPhysicalAddress() + pages[i]) {
        unmapFalcon(); return false;
      }
    }
    return true;
  }
  void unmapFalcon() {
    for (unsigned i = 0; i < ExtraCount; ++i) if (extraMaps[i]) { extraMaps[i]->release(); extraMaps[i] = nullptr; }
    MacGSPBorrowedFWSECStage::unmapFalcon();
  }
  unsigned readPreflight(unsigned index) {
    return index < FWSECPreflight::Count ? fixedRead(FWSECPreflight::Offsets[index]) : 0xffffffffU;
  }
  unsigned readDisplay(unsigned index) {
    return index < 6 ? fixedRead(FWSECDisplay::Offsets[index]) : 0xffffffffU;
  }
  bool environmentReady(unsigned *out) {
    if (!MacGSPBorrowedFWSECStage::environmentReady(out) || !captureFresh(fresh, display)) return false;
    return reservation.claim(fresh, display, providerHeld());
  }
  bool freshForBoot() {
    if (command() != 6 || !providerHeld() || !reservation.owned()) return false;
    if (!FalconDMA::waitBits(*this, FalconDMA::DMACMD, 3, 2, 200, 100, recheckPolls)) return false;
    setMaster(false);
    if (command() != 2) return false;
    FWSECPreflight::Snapshot current;
    FWSECDisplay::Snapshot heads;
    if (!captureFresh(current, heads) || !reservation.matches(current, heads, providerHeld())) return false;
    fresh = current; display = heads;
    setMaster(true);
    rechecked = command() == 6;
    return rechecked;
  }
  unsigned bootRead(rtxfwsecboot::Reg reg) {
    if (unsigned(reg) >= rtxfwsecboot::RegCount || reg == rtxfwsecboot::CpuAlias ||
        !providerHeld() || !reservation.owned() || !rechecked || command() != 6) return 0xffffffffU;
    return fixedRead(rtxfwsecboot::Addresses[reg]);
  }
  bool bootWrite(rtxfwsecboot::Reg reg, unsigned value) {
    using namespace rtxfwsecboot;
    // Match the caller's attempted-write bookkeeping even if a subsequent
    // PCI/ownership check fails. No late error may lose the persistent claim.
    if (reg == CpuCtl || reg == CpuAlias) {
      if (value != 2 || reservation.persistent()) return false;
      const bool permitted=fullReservation.noteStartAttempted(generation);
      reservation.markStartAttempted();
      if (!pinned) { client->retain(); pinned = true; }
      if (!permitted) return false;
    }
    if (!providerHeld() || !reservation.owned() || !rechecked || command() != 6) return false;
    switch (reg) {
      case Rm: if (value != BoardBoot0) return false; break;
      case Mailbox0: case BootVector: if (value != 0) return false; break;
      case BromPara: if (value != SignatureDmemOffset) return false; break;
      case BromEngine: if (value != EngineId) return false; break;
      case BromUcode: if (value != UcodeId) return false; break;
      case BromAlgorithm: if (value != 1) return false; break;
      case CpuCtl: case CpuAlias:
        break;
      default: return false;
    }
    return writeFalconPage(Addresses[reg], value);
  }
};
