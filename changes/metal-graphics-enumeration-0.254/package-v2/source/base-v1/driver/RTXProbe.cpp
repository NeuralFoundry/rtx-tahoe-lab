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

class MacBoot0Transport {
  IOPCIDevice *pci;
  IOService *owner;
  static constexpr unsigned MapCount = 9;
  const unsigned pages[MapCount] = {0, 0x118000, 0x1fa000, 0x824000,
    0x820000, 0x625000, 0x110000, 0x111000, 0x1000};
  IOMemoryMap *mappings[MapCount] = {};
  IOMemoryMap *romMapping = nullptr;
  UInt32 *romBuffer = nullptr;
public:
  MacBoot0Transport(IOPCIDevice *device, IOService *client) : pci(device), owner(client) {}
  ~MacBoot0Transport() { if (romBuffer) IOFree(romBuffer, Preparation::RomSize); }
  const void *romBytes() const { return romBuffer; }
  bool open() { return !pci->isOpen() && pci->open(owner); }
  void close() { pci->close(owner); }
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


#include "MacFWSECExecution.hpp"

struct StateAndRom {
  GPUState::Snapshot state;
  Preparation::RomResult rom;
  FWSECFuse::Snapshot fuse;
  FWSECPreflight::Snapshot preflight;
  template <class IO> const char *operator()(IO &io) {
    if (const char *error = state(io)) return error;
    if (const char *error = fuse(io)) return error;
    if (const char *error = Preparation::captureRom(io, rom)) return error;
    return preflight.capture(io, state.first[GPUState::VramMiB]);
  }
};

class RTXProbe : public IOService {
  OSDeclareDefaultStructors(RTXProbe)
  MacFWSECExecution *execution = nullptr;
  FWSECExecution::Result executionResult;
  bool holdLoaded = false;
public:
  bool start(IOService *provider) override;
  void stop(IOService *provider) override;
  bool willTerminate(IOService *provider, IOOptionBits options) override {
    if (holdLoaded) return false;
    return IOService::willTerminate(provider, options);
  }
};

OSDefineMetaClassAndStructors(RTXProbe, IOService)

bool RTXProbe::start(IOService *provider) {
  IOPCIDevice *pci = OSDynamicCast(IOPCIDevice, provider);
  if (!pci || !IOService::start(provider)) return false;
  // Also enforce exact target in code, independently of the personality.
  const UInt32 identity = pci->configRead32(0x00);
  if (identity != 0x252010de || pci->configRead32(0x2c) != 0x104c1043 ||
      pci->getBusNumber() != 1 || pci->getDeviceNumber() != 0 ||
      pci->getFunctionNumber() != 0) {
    IOLog("RTXProbe: target unavailable or identity mismatch; no further access\n");
    IOService::stop(provider);
    return false;
  }

  setProperty("ProbeVersion", "0.9.0");
  setProperty("Mode", "bounded-fwsec-execute");
  setProperty("ReadOnly", false);
  setProperty("MMIOReadOnly", false);
  setProperty("IdentificationMMIOReadOnly", true);
  setProperty("PCIIdentity", identity, 32);
  setProperty("PCISubsystemIdentity", pci->configRead32(0x2c), 32);
  setProperty("PCICommand", pci->configRead16(0x04), 16);
  setProperty("PCIStatus", pci->configRead16(0x06), 16);
  setProperty("PCIRevisionAndClass", pci->configRead32(0x08), 32);

  // Fixed-size standard header snapshot; never perform BAR sizing writes.
  UInt32 header[16];
  for (unsigned i = 0; i < 16; ++i) header[i] = pci->configRead32(i * 4);
  OSData *data = OSData::withBytes(header, sizeof(header));
  if (data) { setProperty("PCIHeader", data); data->release(); }

  UInt8 pm = 0;
  if (pci->findPCICapability(0x01, &pm) && pm >= 0x40 && pm <= 0xf8) {
    const UInt16 pmcsr = pci->configRead16(pm + 4);
    setProperty("PMCapabilityOffset", pm, 8);
    setProperty("PMCSR", pmcsr, 16);
    setProperty("PCIDState", pmcsr & 3, 8);
  }
  UInt8 pcie = 0;
  if (pci->findPCICapability(0x10, &pcie) && pcie >= 0x40 && pcie <= 0xe8) {
    setProperty("PCIeCapabilityOffset", pcie, 8);
    setProperty("PCIeLinkStatus", pci->configRead16(pcie + 0x12), 16);
    setProperty("PCIeDeviceStatus", pci->configRead16(pcie + 0x0a), 16);
  }

  // Extended capability list: bounded, aligned, with cycle detection.
  UInt16 offset = 0x100;
  bool visited[1024] = {};
  for (unsigned count = 0; count < 256 && offset; ++count) {
    if (offset < 0x100 || offset > 0xffc || (offset & 3) || visited[offset / 4]) {
      setProperty("ExtendedCapabilityError", "Invalid or cyclic capability list");
      break;
    }
    visited[offset / 4] = true;
    const UInt32 cap = pci->extendedConfigRead32(offset);
    if (!cap || cap == 0xffffffff) break;
    if ((cap & 0xffff) == 0x15 && offset <= 0xff4) {
      setProperty("ResizableBAROffset", offset, 16);
      // Capture every advertised entry, including the VRAM BAR, without resizing.
      setProperty("ResizableBARCapability0", pci->extendedConfigRead32(offset + 4), 32);
      const UInt32 control = pci->extendedConfigRead32(offset + 8);
      setProperty("ResizableBARControl0", control, 32);
      const unsigned entries = (control >> 5) & 7;
      if (entries < 1 || entries > 6 || unsigned(offset) + 4 + entries * 8 > 0x1000) {
        setProperty("ResizableBARError", "Invalid entry count or range");
      } else {
        UInt32 values[12] = {};
        for (unsigned i = 0; i < entries * 2; ++i)
          values[i] = pci->extendedConfigRead32(offset + 4 + i * 4);
        OSData *rebar = OSData::withBytes(values, entries * 8);
        if (rebar) { setProperty("ResizableBAREntries", rebar); rebar->release(); }
      }
    }
    offset = (cap >> 20) & 0xfff;
  }

  for (unsigned bar = 0; bar < 6; ++bar) {
    const UInt32 raw = pci->configRead32(0x10 + bar * 4);
    char key[40];
    snprintf(key, sizeof(key), "BAR%uConfigLow", bar);
    setProperty(key, raw, 32);
    const bool is64 = !(raw & 1) && (raw & 6) == 4;
    if (is64 && bar == 5) {
      setProperty("BARLayoutError", "64-bit BAR has no upper register");
      break;
    }
    UInt64 address = raw & ((raw & 1) ? ~UInt64(3) : ~UInt64(15));
    if (is64) address |= UInt64(pci->configRead32(0x10 + (bar + 1) * 4)) << 32;
    snprintf(key, sizeof(key), "BAR%uConfigAddress", bar);
    setProperty(key, address, 64);
    IODeviceMemory *memory = pci->getDeviceMemoryWithRegister(UInt8(0x10 + bar * 4));
    if (memory) {
      snprintf(key, sizeof(key), "BAR%uDescriptorAddress", bar);
      setProperty(key, memory->getPhysicalAddress(), 64);
      snprintf(key, sizeof(key), "BAR%uDescriptorLength", bar);
      setProperty(key, memory->getLength(), 64);
    }
    if (is64) ++bar;
  }
  MacBoot0Transport transport(pci, this);
  StateAndRom extra;
  const Boot0::Result boot = Boot0::run(transport, extra);
  const GPUState::Snapshot &state = extra.state;
  setProperty("FuseStatus", extra.fuse.status);
  setProperty("FuseOffset", FWSECFuse::Offset, 32);
  setProperty("FuseFirst", extra.fuse.first, 32);
  setProperty("FuseSecond", extra.fuse.second, 32);
  setProperty("FuseReads", extra.fuse.reads, 32);
  setProperty("FuseSignatureMask", FWSECFuse::Mask, 32);
  setProperty("FuseSignatureCount", FWSECFuse::Count, 32);
  setProperty("FuseSignatureIndex", UInt32(extra.fuse.index), 32);
  setProperty("FusePassed", extra.fuse.index >= 0 && boot.passed);
  setProperty("FirmwareExecuted", false);
  setProperty("StateComplete", state.complete);
  setProperty("StatePassed", state.complete && boot.passed);
  UInt32 stateRows[GPUState::Count * 4] = {};
  for (unsigned i = 0; i < GPUState::Count; ++i) {
    stateRows[i * 4] = GPUState::Offsets[i];
    stateRows[i * 4 + 1] = state.first[i];
    stateRows[i * 4 + 2] = state.second[i];
    stateRows[i * 4 + 3] = state.reads[i];
  }
  OSData *stateData = OSData::withBytes(stateRows, sizeof(stateRows));
  if (stateData) { setProperty("StateRegisters", stateData); stateData->release(); }
  setProperty("MMIOStatus", boot.status);
  setProperty("MMIOPassed", boot.passed);
  setProperty("MMIOReadCount", boot.reads, 32);
  setProperty("MMIOBoot0First", boot.first, 32);
  setProperty("MMIOBoot0Second", boot.second, 32);
  setProperty("MMIOMemoryEnableAttempted", boot.enableAttempted);
  setProperty("MMIOCommandBefore", boot.commandBefore, 16);
  setProperty("MMIOCommandDuring", boot.commandDuring, 16);
  setProperty("MMIOCommandAfter", boot.commandAfter, 16);
  setProperty("MMIORestoreVerified", boot.restoreVerified);
  setProperty("ROMStatus", extra.rom.status);
  setProperty("ROMReadWords", extra.rom.wordsRead, 32);
  setProperty("ROMCapturedBytes", extra.rom.bytesCaptured, 32);
  setProperty("ROMStable", extra.rom.stable);
  setProperty("ROMPassed", boot.passed && extra.rom.stable);
  if (extra.rom.stable && boot.passed && transport.romBytes()) {
    OSData *rom = OSData::withBytes(transport.romBytes(), Preparation::RomSize);
    if (rom) { setProperty("VBIOSShadow", rom); rom->release(); }
    else setProperty("ROMExportError", "Allocation failed");
  }
  const FWSECPreflight::Snapshot &preflight = extra.preflight;
  setProperty("PreflightStatus", preflight.status);
  setProperty("PreflightComplete", preflight.complete);
  setProperty("PreflightPassed", boot.passed && preflight.complete && preflight.layoutValid);
#define PBOOL(name, member) setProperty("Preflight" name, preflight.member)
  PBOOL("DisplaySupported", displaySupported); PBOOL("WorkspaceValid", workspaceValid);
  PBOOL("RequiresRelocation", requiresRelocation); PBOOL("LayoutValid", layoutValid);
  PBOOL("EngineIdle", engineIdle); PBOOL("WprClear", wprClear);
#undef PBOOL
#define PNUM(name, member) setProperty("Preflight" name, preflight.member, 64)
  PNUM("VramBytes", vramBytes); PNUM("WorkspaceAddress", workspaceAddress);
  PNUM("WorkspaceBoundary", workspaceBoundary); PNUM("FrtsOffset", frtsOffset); PNUM("FrtsEnd", frtsEnd);
  PNUM("WprLo", wprLo); PNUM("WprHi", wprHi);
#undef PNUM
  setProperty("PreflightRegionOwned", false);
  setProperty("PreflightExecutionReady", false);
  setProperty("DMATransferExecuted", false);
  UInt32 preflightRows[FWSECPreflight::Count * 4] = {};
  for (unsigned i = 0; i < FWSECPreflight::Count; ++i) {
    preflightRows[i * 4] = FWSECPreflight::Offsets[i];
    preflightRows[i * 4 + 1] = preflight.first[i];
    preflightRows[i * 4 + 2] = preflight.second[i];
    preflightRows[i * 4 + 3] = preflight.reads[i];
  }
  OSData *preflightData = OSData::withBytes(preflightRows, sizeof(preflightRows));
  if (preflightData) { setProperty("PreflightRegisters", preflightData); preflightData->release(); }
  if (boot.passed && preflight.complete && preflight.layoutValid && extra.fuse.index >= 0) {
    execution = new MacFWSECExecution(pci, this, transport.romBytes(), extra.fuse.first, unsigned(extra.fuse.index));
    if (execution) {
      FWSECExecution::run(*execution, executionResult);
      execution->forgetCapturedRom();
    }
  }
  auto &fwsec = executionResult.stage;
  auto &falcon = fwsec.lifecycle;
  auto &fwboot = executionResult.boot;
  if (execution) {
    setProperty("FalconMapperMode", execution->mapperMode);
    setProperty("FalconLastIOReturn", UInt32(execution->lastError), 32);
    setProperty("FalconClearIOReturn", UInt32(execution->clearError), 32);
    setProperty("FalconCompleteIOReturn", UInt32(execution->completeError), 32);
    setProperty("FalconMemoryCompleteIOReturn", UInt32(execution->memoryCompleteError), 32);
    setProperty("FWSECPublishIOReturn", UInt32(execution->fwsecPublishError), 32);
  } else {
    setProperty("FalconMapperMode", "not-selected");
    const char *keys[] = {"FalconLastIOReturn", "FalconClearIOReturn", "FalconCompleteIOReturn",
      "FalconMemoryCompleteIOReturn", "FWSECPublishIOReturn"};
    for (unsigned i = 0; i < 5; ++i) setProperty(keys[i], UInt32(kIOReturnNotReady), 32);
  }
  setProperty("FalconStatus", falcon.status);
#define FNUM(name, member) setProperty("Falcon" name, falcon.member, 32)
#define FBOOL(name, member) setProperty("Falcon" name, falcon.member)
  FNUM("Count", count); setProperty("FalconEnd", falcon.end, 64);
  FNUM("CommandBefore", commandBefore); FNUM("CommandEnabled", commandEnabled); FNUM("CommandAfter", commandAfter);
  FNUM("DeviceStatusBefore", deviceStatusBefore); FNUM("DeviceStatusAfter", deviceStatusAfter);
  FNUM("InitialReads", initialReads); FNUM("FinalReads", finalReads);
  FNUM("ResetCount", resetCount); FNUM("ResetPolls", resetPolls); FNUM("DrainPolls", drainPolls);
  FNUM("MismatchPhase", mismatchPhase); FNUM("MismatchWord", mismatchWord); FNUM("MismatchValue", mismatchValue);
  FBOOL("Prepared", prepared); FBOOL("MemoryAttempted", memoryAttempted); FBOOL("MasterAttempted", masterAttempted);
  FBOOL("ResetAttempted", resetAttempted); FBOOL("TargetsAttempted", targetsAttempted);
  FBOOL("Quiescent", quiescent); FBOOL("TargetsCleared", targetsCleared);
  FBOOL("CleanupVerified", cleanupVerified); FBOOL("ResourcesRetained", resourcesRetained);
  FBOOL("CpuIntact", cpuIntact); FBOOL("Passed", passed);
#undef FNUM
#undef FBOOL
#define FDATA(name, member) do { \
  OSData *blob = OSData::withBytes(falcon.member, sizeof(falcon.member)); \
  if (blob) { setProperty("Falcon" name, blob); blob->release(); } \
} while (false)
  FDATA("Segments", segments); FDATA("Initial", initial); FDATA("Final", final); FDATA("Environment", environment);
#undef FDATA
  setProperty("FWSECBoardMatched", fwsec.boardMatched);
  setProperty("FWSECImageSHA256", FWSECPayload::ImageSHA256);
  setProperty("FWSECOriginalImageSHA256", FWSECPayload::OriginalImageSHA256);
  setProperty("FWSECRomSHA256", FWSECPayload::RomSHA256);
  setProperty("FWSECImageSize", FWSECPayload::ImageSize, 32);
  setProperty("FWSECSignatureIndex", FWSECPayload::SignatureIndex, 32);
#define SNUM(name, member) setProperty("FWSEC" name, fwsec.member, 32)
  SNUM("Hwcfg", hwcfg); SNUM("CanaryMatched", canaryMatched); SNUM("PublishCount", publishCount);
  SNUM("DmaPolls", dmaPolls); SNUM("ImemSubmitted", imemSubmitted); SNUM("ImemCompleted", imemCompleted);
  SNUM("DmemSubmitted", dmemSubmitted); SNUM("DmemCompleted", dmemCompleted);
  SNUM("DmemReads", dmemReads); SNUM("DmemMatched", dmemMatched);
  SNUM("MismatchWord", mismatchWord); SNUM("MismatchValue", mismatchValue);
#undef SNUM
#define SDATA(name, member) do { \
  OSData *blob = OSData::withBytes(fwsec.member, sizeof(fwsec.member)); \
  if (blob) { setProperty("FWSEC" name, blob); blob->release(); } \
} while (false)
  SDATA("Completions", completions); SDATA("Dmem", dmem);
#undef SDATA
  setProperty("FWBootStatus", fwboot.status);
#define BNUM(name, member) setProperty("FWBoot" name, fwboot.member, 32)
  BNUM("InitialReads", initialReads); BNUM("CpuBeforeStart", cpuBeforeStart); BNUM("LastCpu", lastCpu);
  BNUM("HaltPolls", haltPolls); BNUM("DelayCalls", delayCalls); BNUM("Mailbox0", mailbox0); BNUM("Mailbox1", mailbox1);
  BNUM("Scratch", scratch); BNUM("WprLo", wprLo); BNUM("WprHi", wprHi); BNUM("FailedRegister", failedRegister);
  BNUM("WriteAttempts", writeAttempts); BNUM("VerifiedWrites", verifiedWrites);
#undef BNUM
#define BBOOL(name, member) setProperty("FWBoot" name, fwboot.member)
  BBOOL("RegistersTouched", registersTouched); BBOOL("StartAttempted", startAttempted);
  BBOOL("StartWriteAccepted", startWriteAccepted); BBOOL("AliasUsed", aliasUsed); BBOOL("Halted", halted);
  BBOOL("RunningObserved", runningObserved); BBOOL("SideEffectsMayRemain", sideEffectsMayRemain);
  BBOOL("RequiresCallerQuiescence", requiresCallerQuiescence); BBOOL("OutcomeRead", outcomeRead);
  BBOOL("WprTransitionObserved", wprTransitionObserved); BBOOL("Passed", passed);
  BBOOL("AuthenticatedExecutionInferred", authenticatedExecutionInferred);
#undef BBOOL
  OSData *bootData = OSData::withBytes(fwboot.initial, sizeof(fwboot.initial));
  if (bootData) { setProperty("FWBootInitial", bootData); bootData->release(); }
  setProperty("FWRegionOwned", executionResult.regionOwned);
  setProperty("FWRegionPersistent", executionResult.regionPersistent);
  setProperty("FWRegionRechecked", executionResult.regionRechecked);
  setProperty("FWProviderHeld", executionResult.providerHeld);
  setProperty("FWRegionOffset", FWSECRegion::Offset, 64);
  setProperty("FWRegionSize", FWSECRegion::Size, 64);
  setProperty("FWRegionVram", FWSECRegion::VramBytes, 64);
  setProperty("FWExecutionPassed", executionResult.passed);
  setProperty("FirmwareExecuted", fwboot.passed);
  setProperty("DMATransferExecuted", executionResult.anyDMA);
  UInt32 freshRows[FWSECPreflight::Count * 4] = {}, displayRows[FWSECDisplay::Count * 4] = {};
  for (unsigned i = 0; i < FWSECPreflight::Count; ++i) {
    freshRows[i * 4] = FWSECPreflight::Offsets[i];
    if (execution) {
      freshRows[i * 4 + 1] = execution->fresh.first[i];
      freshRows[i * 4 + 2] = execution->fresh.second[i];
      freshRows[i * 4 + 3] = execution->fresh.reads[i];
    }
  }
  for (unsigned i = 0; i < FWSECDisplay::Count; ++i) {
    displayRows[i * 4] = FWSECDisplay::Offsets[i];
    if (execution) {
      displayRows[i * 4 + 1] = execution->display.first[i];
      displayRows[i * 4 + 2] = execution->display.second[i];
      displayRows[i * 4 + 3] = execution->display.reads[i];
    }
  }
  OSData *freshData = OSData::withBytes(freshRows, sizeof(freshRows));
  if (freshData) { setProperty("FWRegionRegisters", freshData); freshData->release(); }
  OSData *displayData = OSData::withBytes(displayRows, sizeof(displayRows));
  if (displayData) { setProperty("FWDisplayRegisters", displayData); displayData->release(); }
  setProperty("FWDisplayStatus", execution ? execution->display.status : "not-run");
  setProperty("FWRecheckPolls", execution ? execution->recheckPolls : 0, 32);
  holdLoaded = executionResult.providerHeld || falcon.resourcesRetained;
  // A START write pins this service before it reaches hardware. Also pin an
  // exceptional pre-start retention; no automatic unload is safe in that case.
  if (holdLoaded && !fwboot.startAttempted) retain();
  setProperty("FWRetainedUntilPlatformReset", holdLoaded);
  if (execution && !holdLoaded) { delete execution; execution = nullptr; }
  setProperty("ProbeComplete", true);
  IOLog("RTXProbe: BOOT0 %s; reads=%u restore=%u\n", boot.status, boot.reads, unsigned(boot.restoreVerified));
  registerService();
  return true;
}

void RTXProbe::stop(IOService *provider) {
  if (holdLoaded) {
    IOLog("RTXProbe: firmware/device state retained; platform reset required before teardown\n");
    return;
  }
  if (execution) { delete execution; execution = nullptr; }
  IOLog("RTXProbe: detached before firmware start; host transaction finished\n");
  IOService::stop(provider);
}

extern "C" {
extern kern_return_t _start(kmod_info_t *, void *);
extern kern_return_t _stop(kmod_info_t *, void *);
KMOD_EXPLICIT_DECL(local.emre.RTXProbe, "0.9.0", _start, _stop)
__attribute__((visibility("hidden"))) kmod_start_func_t *_realmain = nullptr;
__attribute__((visibility("hidden"))) kmod_stop_func_t *_antimain = nullptr;
__attribute__((visibility("hidden"))) int _kext_apple_cc = __APPLE_CC__;
}
