#include "driver/FWSECHostCleanup.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

// Synthetic lifecycle model only: no firmware image or hardware adapter.
using namespace FalconDMA;
enum class CleanupFault {
  None, ResetReadyUnreadable, ResetAssert, ResetRelease, Scrub,
  ResetCpuActive, ResetRiscvActive, DmaBusy, DmaUnreadable,
  PcieBusy, PcieUnreadable, FbifUnreadable, ClearDmaCtl, ClearFbif,
  ClearBase, ClearBase1, ClearOffset, ClearFbOffset, MasterDisable,
  MemoryDisable, CommandLostDuringReset, CommandLostBeforeSnapshot,
  BufferChanged, CommandChangedDuringVerify, CleanupError,
  FinalCpuNotHalted, FinalCoreInvalid, FinalDmaBusy, FinalBaseNonzero
};
struct CleanupIO {
  CleanupFault fault = CleanupFault::None;
  std::array<unsigned, Count> regs{};
  unsigned cmd = 6, resets = 0, statusReads = 0, finalReads = 0, verifyCalls = 0;
  unsigned cleanupCalls = 0, closes = 0, unmaps = 0, delays = 0;
  Reg finalUnreadable = Count;
  unsigned invalidValue = 0xffffffffU;
  bool mapped = true, opened = true, hostAllocated = true;
  bool finalPhase = false, verifiedFresh = false;
  CleanupIO() {
    regs[HWCFG2] = 0x80000000; regs[HWCFG] = 0x80420100;
    regs[CPUCTL] = regs[RISCVCPU] = 0x10; regs[BCR] = 1;
    regs[DMACMD] = 2; regs[DMACTL] = 0x80; regs[FBIFCTL] = 0x190;
    regs[TRANSCFG] = 5;
    regs[DMABASE] = 0x474a900; regs[DMABASE1] = 1;
    regs[DMAOFFSET] = 0x700; regs[FBOFFSET] = 0x700;
  }
  unsigned command() {
    if (fault == CleanupFault::CommandLostDuringReset && resets && !statusReads) cmd = 0;
    if (statusReads && mapped) {
      finalPhase = true;
      if (fault == CleanupFault::CommandLostBeforeSnapshot) cmd = 0;
    }
    if (fault == CleanupFault::CommandChangedDuringVerify && verifyCalls) cmd = 4;
    return cmd;
  }
  unsigned read(Reg reg) {
    assert(mapped && (cmd & 2) && reg < Count);
    if (finalPhase) {
      ++finalReads;
      if (reg == finalUnreadable) return invalidValue;
      if (reg == CPUCTL && fault == CleanupFault::FinalCpuNotHalted) return 0;
      if (reg == BCR && fault == CleanupFault::FinalCoreInvalid) return 0;
      if (reg == DMACMD && fault == CleanupFault::FinalDmaBusy) return 0;
      if (reg == DMABASE && fault == CleanupFault::FinalBaseNonzero) return 1;
    }
    if (reg == HWCFG2 && fault == CleanupFault::ResetReadyUnreadable) return 0xbadf0000;
    if (resets) {
      if (reg == HWCFG2 && fault == CleanupFault::Scrub) return 0x1000;
      if (reg == CPUCTL && fault == CleanupFault::ResetCpuActive) return 2;
      if (reg == RISCVCPU && fault == CleanupFault::ResetRiscvActive) return 0x80;
      if (reg == DMACMD && fault == CleanupFault::DmaBusy) return 0;
      if (reg == DMACMD && fault == CleanupFault::DmaUnreadable) return 0xbad01234;
      if (reg == FBIFCTL && fault == CleanupFault::FbifUnreadable) return 0xffffffff;
    }
    return regs[reg];
  }
  void write(Reg reg, unsigned value) {
    assert(mapped && (cmd & 2));
    assert(reg == Engine || reg == BCR || reg == DMACTL || reg == FBIFCTL ||
           reg == DMABASE || reg == DMABASE1 || reg == DMAOFFSET || reg == FBOFFSET);
    if (reg == Engine) {
      if ((value & 1) && fault == CleanupFault::ResetAssert) return;
      if (!(value & 1) && fault == CleanupFault::ResetRelease) return;
      if (!(value & 1)) ++resets;
    }
    if ((reg == DMACTL && fault == CleanupFault::ClearDmaCtl) ||
        (reg == FBIFCTL && fault == CleanupFault::ClearFbif) ||
        (reg == DMABASE && fault == CleanupFault::ClearBase) ||
        (reg == DMABASE1 && fault == CleanupFault::ClearBase1) ||
        (reg == DMAOFFSET && fault == CleanupFault::ClearOffset) ||
        (reg == FBOFFSET && fault == CleanupFault::ClearFbOffset)) return;
    regs[reg] = reg == BCR ? 1U : value;
  }
  unsigned deviceStatus() {
    assert(mapped && resets); ++statusReads;
    if (fault == CleanupFault::PcieBusy) return 0x20;
    if (fault == CleanupFault::PcieUnreadable) return 0xffff;
    return 0;
  }
  void delayUs(unsigned us) { assert(us == 10 || us == 100); assert(++delays <= 1000); }
  void setMaster(bool enabled) {
    assert(!enabled);
    if (fault != CleanupFault::MasterDisable) cmd &= ~4U;
  }
  void setMemory(bool enabled) {
    assert(!enabled);
    if (fault != CleanupFault::MemoryDisable) cmd &= ~2U;
  }
  void unmapFalcon() { ++unmaps; mapped = false; }
  bool verifyFWSEC() {
    // This must occur after both hardware quiescence and PCI disable/unmap.
    assert(cmd == 0 && !mapped && resets && statusReads && finalReads == SnapshotCount);
    ++verifyCalls;
    verifiedFresh = fault != CleanupFault::BufferChanged;
    return verifiedFresh;
  }
  bool cleanup() {
    assert(cmd == 0 && !mapped); ++cleanupCalls;
    if (fault == CleanupFault::CleanupError) return false;
    hostAllocated = false;
    return true;
  }
  void close() { assert(opened && !mapped); ++closes; opened = false; }
};

static unsigned scenarios = 0;
static Result staged() {
  Result r;
  r.status = "stage-loaded"; r.commandBefore = 0; r.commandEnabled = 6;
  r.prepared = r.memoryAttempted = r.masterAttempted = r.resetAttempted = r.targetsAttempted = true;
  r.cpuIntact = true; r.resetCount = 1;
  return r;
}
static rtxfwsecboot::Result booted(bool success = true) {
  rtxfwsecboot::Result b;
  b.status = success ? "fwsec-frts-execution-verified" : "fwsec-boot-halt-timeout";
  b.startAttempted = b.startWriteAccepted = true;
  b.sideEffectsMayRemain = b.requiresCallerQuiescence = true;
  b.passed = success; b.halted = success; b.haltPolls = success ? 2 : 20000;
  return b;
}
static void check(CleanupIO &io, Result &r, bool stageGood, bool anyDMA,
                  const rtxfwsecboot::Result &boot, const char *expected) {
  ++scenarios;
  const auto savedBoot = boot;
  FWSECHostCleanup::finish(io, r, stageGood, anyDMA, boot);
  if (std::strcmp(r.status, expected)) {
    std::fprintf(stderr, "cleanup scenario %u: expected %s, got %s\n", scenarios, expected, r.status);
    assert(false);
  }
  assert(boot.status == savedBoot.status && boot.passed == savedBoot.passed &&
         boot.startAttempted == savedBoot.startAttempted && boot.haltPolls == savedBoot.haltPolls &&
         boot.sideEffectsMayRemain == savedBoot.sideEffectsMayRemain);
  const bool providerHeld = boot.startAttempted || r.resourcesRetained;
  assert(io.unmaps == 1 && io.closes == (providerHeld ? 0U : 1U));
  assert(io.opened == providerHeld && !io.mapped);
  if (!io.cleanupCalls) assert(r.resourcesRetained);
  if (r.resourcesRetained) assert(!r.cleanupVerified);
  assert(r.resourcesRetained == io.hostAllocated);
  assert(io.verifyCalls <= 1 && r.drainPolls <= 400);
  if (boot.startAttempted && io.cleanupCalls) {
    assert(boot.passed && io.verifyCalls == 1 && io.verifiedFresh && r.commandAfter == 0);
    assert(r.quiescent && r.targetsCleared && r.finalReads == SnapshotCount);
  }
  if (r.passed) assert(stageGood && r.cpuIntact && r.cleanupVerified && !r.resourcesRetained);
}

int main() {
  const auto success = booted();
  { CleanupIO io; Result r = staged();
    check(io, r, true, true, success, "fwsec-host-cleanup-verified-vram-retained");
    assert(r.passed && !r.resourcesRetained && r.resetCount == 2 && io.opened); }
  { CleanupIO io; Result r = staged(); r.cpuIntact = false;
    check(io, r, true, true, success, "fwsec-host-cleanup-verified-vram-retained");
    assert(r.passed && r.cpuIntact && io.verifyCalls == 1); }
  { CleanupIO io; Result r = staged();
    check(io, r, false, true, success, "fwsec-host-cleanup-verified-vram-retained");
    assert(!r.passed && r.cleanupVerified); }
  { CleanupIO io; Result r = staged(); auto timeout = booted(false);
    check(io, r, true, true, timeout, "fwsec-host-retained-for-uncertain-firmware");
    assert(!r.passed && r.quiescent && r.targetsCleared && !io.verifyCalls && !io.cleanupCalls); }
  { CleanupIO io; Result r = staged(); auto failedStart = booted(false);
    failedStart.startWriteAccepted = false; failedStart.status = "fwsec-boot-start-write-failed";
    check(io, r, true, true, failedStart, "fwsec-host-retained-for-uncertain-firmware"); }
  { CleanupIO io; Result r = staged(); io.fault = CleanupFault::ResetAssert;
    check(io, r, true, true, booted(false), "fwsec-host-retained-for-uncertain-firmware"); }
  { CleanupIO io; Result r = staged(); rtxfwsecboot::Result noStart;
    check(io, r, false, true, noStart, "stage-loaded"); assert(!r.passed && r.cleanupVerified); }
  { CleanupIO io; Result r = staged(); rtxfwsecboot::Result noStart;
    check(io, r, true, true, noStart, "stage-loaded"); assert(r.passed && !io.verifyCalls); }
  // Allocation/mapper errors before any exposure may release their partial
  // resources without touching mapped GPU registers or requiring a reset.
  for (bool prepared : {false, true}) {
    CleanupIO io; io.cmd = 0; io.mapped = false;
    Result r; r.commandBefore = 0; r.prepared = prepared; r.status = "early-failure";
    rtxfwsecboot::Result noStart;
    check(io, r, false, false, noStart, "early-failure");
    assert(r.cleanupVerified && !io.resets && !io.verifyCalls && !r.passed);
  }
  { CleanupIO io; io.cmd = 2; Result r; r.commandBefore = 0;
    r.memoryAttempted = true; r.status = "before-reset-failure";
    rtxfwsecboot::Result noStart;
    check(io, r, false, false, noStart, "before-reset-failure");
    assert(r.cleanupVerified && r.commandAfter == 0 && !io.resets); }
  { CleanupIO io; io.cmd = 2; io.fault = CleanupFault::MemoryDisable;
    Result r; r.commandBefore = 0; r.memoryAttempted = true;
    rtxfwsecboot::Result noStart;
    check(io, r, false, false, noStart, "fwsec-host-quiescence-failed-resources-retained");
    assert(!io.cleanupCalls && io.opened); }

  for (CleanupFault fault : {
      CleanupFault::ResetReadyUnreadable, CleanupFault::ResetAssert, CleanupFault::ResetRelease,
      CleanupFault::Scrub, CleanupFault::ResetCpuActive, CleanupFault::ResetRiscvActive,
      CleanupFault::DmaBusy, CleanupFault::DmaUnreadable, CleanupFault::PcieBusy,
      CleanupFault::PcieUnreadable, CleanupFault::FbifUnreadable, CleanupFault::ClearDmaCtl,
      CleanupFault::ClearFbif, CleanupFault::ClearBase, CleanupFault::ClearBase1,
      CleanupFault::ClearOffset, CleanupFault::ClearFbOffset, CleanupFault::MasterDisable,
      CleanupFault::MemoryDisable, CleanupFault::CommandLostDuringReset,
      CleanupFault::CommandLostBeforeSnapshot, CleanupFault::CommandChangedDuringVerify,
      CleanupFault::FinalCpuNotHalted, CleanupFault::FinalCoreInvalid,
      CleanupFault::FinalDmaBusy, CleanupFault::FinalBaseNonzero}) {
    CleanupIO io; io.fault = fault; Result r = staged();
    check(io, r, true, true, success, "fwsec-host-quiescence-failed-resources-retained");
    assert(!r.passed && !io.cleanupCalls && io.opened);
  }
  { CleanupIO io; io.fault = CleanupFault::BufferChanged; Result r = staged();
    check(io, r, true, true, success, "fwsec-host-buffer-changed-resources-retained");
    assert(!r.passed && !r.cpuIntact && io.verifyCalls == 1); }
  { CleanupIO io; io.fault = CleanupFault::CleanupError; Result r = staged();
    check(io, r, true, true, success, "fwsec-host-cleanup-failed-resources-retained");
    assert(!r.passed && !r.cleanupVerified && r.resourcesRetained && io.opened); }
  { CleanupIO io; io.fault = CleanupFault::CleanupError; Result r = staged();
    rtxfwsecboot::Result noStart;
    check(io, r, false, true, noStart, "fwsec-host-cleanup-failed-resources-retained");
    assert(r.resourcesRetained && io.opened && io.cleanupCalls == 1); }
  { CleanupIO io; io.cmd = 0; io.mapped = false; io.fault = CleanupFault::CleanupError;
    Result r; r.commandBefore = 0; r.status = "early-failure";
    rtxfwsecboot::Result noStart;
    check(io, r, false, false, noStart, "fwsec-host-cleanup-failed-resources-retained");
    assert(r.resourcesRetained && io.opened && io.cleanupCalls == 1 && !io.resets); }
  { CleanupIO io; io.fault = CleanupFault::PcieBusy; Result r = staged();
    rtxfwsecboot::Result noStart;
    check(io, r, false, true, noStart, "fwsec-host-quiescence-failed-resources-retained");
    assert(r.resourcesRetained && io.opened && !io.cleanupCalls); }
  // Every final register can contradict an earlier successful reset/clear.
  for (unsigned reg = 0; reg < SnapshotCount; ++reg) {
    for (unsigned invalid : {0xffffffffU, 0xbadf1000U, 0xbad01234U}) {
      CleanupIO io; io.finalUnreadable = Reg(reg); io.invalidValue = invalid; Result r = staged();
      check(io, r, true, true, success, "fwsec-host-quiescence-failed-resources-retained");
      assert(!r.quiescent && !r.targetsCleared && !io.verifyCalls && !io.cleanupCalls);
    }
  }
  // Stale lifecycle success flags cannot bypass a fresh failed teardown.
  { CleanupIO io; io.fault = CleanupFault::ResetAssert; Result r = staged();
    r.quiescent = r.targetsCleared = r.cleanupVerified = r.passed = true;
    check(io, r, true, true, success, "fwsec-host-quiescence-failed-resources-retained");
    assert(!r.passed && !r.cleanupVerified); }
  { CleanupIO io; io.cmd = 0; Result r = staged();
    check(io, r, true, true, success, "fwsec-host-quiescence-failed-resources-retained");
    assert(!io.verifyCalls && !io.cleanupCalls); }
  std::printf("%u FWSEC host-cleanup scenarios passed (synthetic backend only).\n", scenarios);
}
