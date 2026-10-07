// Reuse the established transfer model, not historical successful results.
// Its explicit return in main keeps this renamed entry point well-defined.
#define main previous_stage_main
#include "test_fwsec_stage.cpp"
#undef main
#include "driver/FWSECExecution.hpp"

enum class ExecutionFault {
  None, RunningThenHalt, NoWpr, Timeout, StartWrite, ConfigWrite, ConfigReadback,
  MailboxError, FrtsError, CpuUnreadable, FreshCheck, HostCorruptBeforeBoot,
  HostCorruptAfterBoot, LostRegion, LostProvider, DmaGate, CpuChangesBeforeStart,
  PostReset, PostDrain, PostCleanup, PostFinalSnapshot
};

struct ExecutionIO : StageIO {
  ExecutionFault executionFault = ExecutionFault::None;
  std::array<unsigned, rtxfwsecboot::RegCount> bootRegs{};
  bool region = false, persistent = false, reportProvider = true;
  unsigned freshCalls = 0, bootReads = 0, bootWrites = 0, cpuStarts = 0, cpuPolls = 0;
  unsigned unmaps = 0, cleanups = 0, closes = 0;
  unsigned sequence = 0, freshAt = 0, startAt = 0, resetAfterStageAt = 0;
  unsigned unmapAt = 0, cleanupAt = 0, closeAt = 0;

  ExecutionIO() {
    bootRegs[rtxfwsecboot::PmcBoot0] = rtxfwsecboot::BoardBoot0;
    bootRegs[rtxfwsecboot::WprLo] = 0x1ffffe00;
  }
  bool open() {
    const bool result = StageIO::open(); region = result; return result;
  }
  bool providerHeld() const { return opened && reportProvider; }
  bool regionOwned() const { return region; }
  bool regionPersistent() const { return persistent; }
  void close() {
    assert(!persistent && !cpuStarts && !prepared && !allocated && !mapped);
    ++closes; closeAt = ++sequence; region = false; StageIO::close();
  }
  void unmapFalcon() {
    ++unmaps; unmapAt = ++sequence;
    // Staging must not invoke its old load-only cleanup before booting.
    if (cpuStarts) assert(resetAfterStageAt > startAt);
    StageIO::unmapFalcon();
  }
  bool cleanup() {
    ++cleanups; cleanupAt = ++sequence;
    assert(!mapped && unmapAt && cleanupAt > unmapAt && !cmd && !pending);
    if (cpuStarts) assert(persistent && opened && region && resetAfterStageAt > startAt);
    if (executionFault == ExecutionFault::PostCleanup) return false;
    return StageIO::cleanup();
  }
  bool imageIntact() const {
    for (unsigned i = 0; i < imem.size(); ++i) if (imem[i] != imageFixture(i * 4)) return false;
    for (unsigned i = 0; i < dmem.size(); ++i)
      if (dmem[i] != imageFixture(FWSECStage::ImemBytes + i * 4)) return false;
    return true;
  }
  bool freshForBoot() {
    ++freshCalls; freshAt = ++sequence;
    assert(freshCalls == 1 && providerHeld() && region && !persistent);
    assert(resets == 1 && resetAsserts == 1 && submissions == FWSECStage::Blocks);
    assert(imemCopies == 225 && dmemCopies == 8 && dataReads == 512);
    assert(prepared && allocated && mapped && !pending && cmd == 6);
    assert(!unmaps && !cleanups && !closes && !cleaned && imageIntact());
    // Fixture inspection establishes test ordering, not a native IMEM readback claim.
    if (executionFault == ExecutionFault::FreshCheck) return false;
    if (executionFault == ExecutionFault::HostCorruptBeforeBoot) memory.back() ^= 1;
    if (executionFault == ExecutionFault::LostRegion) region = false;
    if (executionFault == ExecutionFault::LostProvider) reportProvider = false;
    return true;
  }
  unsigned read(FalconDMA::Reg reg) {
    if (freshCalls && !resetAfterStageAt && executionFault == ExecutionFault::DmaGate && reg == DMACMD)
      return 0;
    return StageIO::read(reg);
  }
  void write(FalconDMA::Reg reg, unsigned value) {
    if (reg == Engine && value == 1 && resetAsserts == 1) {
      resetAfterStageAt = ++sequence;
      if (cpuStarts) assert(persistent && opened && region && startAt && resetAfterStageAt > startAt);
      if (executionFault == ExecutionFault::PostReset) fault = StageFault::CleanupResetFailure;
      if (executionFault == ExecutionFault::PostDrain) fault = StageFault::PCIeDrainFailure;
      if (executionFault == ExecutionFault::PostFinalSnapshot) fault = StageFault::FinalUnreadable;
    }
    StageIO::write(reg, value);
  }
  bool verifyFWSEC() {
    if (cpuStarts && !mapped && executionFault == ExecutionFault::HostCorruptAfterBoot) memory.back() ^= 1;
    return StageIO::verifyFWSEC();
  }
  unsigned bootRead(rtxfwsecboot::Reg reg) {
    ++bootReads;
    assert(freshCalls == 1 && resets == 1 && prepared && mapped && !cleaned && cmd == 6);
    assert(!unmaps && !cleanups && !closes);
    switch (reg) {
      case rtxfwsecboot::Engine: return StageIO::read(Engine);
      case rtxfwsecboot::Hwcfg2: return StageIO::read(HWCFG2);
      case rtxfwsecboot::CpuCtl:
        if (cpuStarts) {
          ++cpuPolls;
          if (executionFault == ExecutionFault::CpuUnreadable) return 0xbadf0000;
          if (executionFault == ExecutionFault::Timeout) return 2;
          if (executionFault == ExecutionFault::RunningThenHalt && cpuPolls < 3) return 2;
          return 0x10;
        }
        if (bootWrites == 7 && executionFault == ExecutionFault::CpuChangesBeforeStart) return 2;
        return StageIO::read(CPUCTL);
      case rtxfwsecboot::RiscvCpu: return StageIO::read(RISCVCPU);
      case rtxfwsecboot::CoreSelect: return StageIO::read(BCR);
      case rtxfwsecboot::DmaCmd: return StageIO::read(DMACMD);
      case rtxfwsecboot::BromAlgorithm:
        if (executionFault == ExecutionFault::ConfigReadback) return bootRegs[reg] ^ 1U;
        return bootRegs[reg];
      case rtxfwsecboot::CpuAlias: assert(false && "write-only CPU alias read"); return 0;
      default: return bootRegs[reg];
    }
  }
  bool bootWrite(rtxfwsecboot::Reg reg, unsigned value) {
    ++bootWrites;
    assert(freshAt && prepared && allocated && mapped && cmd == 6 && !pending);
    assert(opened && region && !cleaned && !unmaps && !cleanups && !closes && resets == 1);
    if (reg == rtxfwsecboot::CpuCtl || reg == rtxfwsecboot::CpuAlias) {
      assert(value == 2 && !cpuStarts && imageIntact());
      assert(submissions == 233 && bootWrites == 8);
      ++cpuStarts; startAt = ++sequence; persistent = true;
      if (executionFault == ExecutionFault::StartWrite) return false;
      if (executionFault != ExecutionFault::NoWpr) {
        bootRegs[rtxfwsecboot::WprLo] = unsigned(rtxfwsecboot::FrtsOffset >> 12) << 4;
        bootRegs[rtxfwsecboot::WprHi] = unsigned((rtxfwsecboot::FrtsOffset + rtxfwsecboot::FrtsBytes) >> 12) << 4;
      }
      if (executionFault == ExecutionFault::MailboxError) bootRegs[rtxfwsecboot::Mailbox0] = 1;
      if (executionFault == ExecutionFault::FrtsError) bootRegs[rtxfwsecboot::FrtsScratch] = 0x10000;
      return true;
    }
    bootRegs[reg] = value;
    return !(executionFault == ExecutionFault::ConfigWrite && reg == rtxfwsecboot::BromUcode);
  }
};

static void verifyLifetime(const ExecutionIO &io, const FWSECExecution::Result &r) {
  assert(io.submissions <= 233 && io.cpuStarts <= 1 && io.freshCalls <= 1);
  assert(io.unmaps <= 1 && io.cleanups <= 1 && io.closes <= 1);
  assert(r.boot.startAttempted == bool(io.cpuStarts));
  if (io.cpuStarts) {
    assert(io.opened && io.region && io.persistent && !io.closes);
    assert(r.providerHeld && r.regionOwned && r.regionPersistent);
    assert(io.startAt > io.freshAt && io.resetAfterStageAt > io.startAt);
    assert(io.unmapAt > io.resetAfterStageAt);
    if (r.stage.lifecycle.resourcesRetained) assert(io.prepared && io.allocated && !io.cleaned);
    else assert(io.cleaned && !io.prepared && !io.allocated && io.cleanupAt > io.unmapAt);
  } else {
    assert(!r.passed && !io.persistent);
    if (io.closes) assert(!io.opened && !io.region && io.closeAt > io.cleanupAt);
  }
}

int main() {
  unsigned cases = 0;
  for (ExecutionFault mode : {ExecutionFault::None, ExecutionFault::RunningThenHalt}) {
    ExecutionIO io; io.executionFault = mode; FWSECExecution::Result r;
    FWSECExecution::run(io, r); verifyLifetime(io, r);
    assert(r.passed && r.stageVerified && r.anyDMA && r.regionRechecked);
    assert(r.boot.passed && r.boot.wprTransitionObserved && r.boot.authenticatedExecutionInferred);
    assert(io.resets == 2 && io.submissions == 233 && io.cpuStarts == 1 && io.cleanups == 1);
    assert(r.stage.lifecycle.cleanupVerified && !r.stage.lifecycle.resourcesRetained);
    assert(r.boot.haltPolls == (mode == ExecutionFault::None ? 1U : 3U));
    ++cases;
  }
  const struct { ExecutionFault fault; const char *status; } failedBoots[] = {
    {ExecutionFault::NoWpr, "fwsec-boot-wpr-outcome-mismatch"},
    {ExecutionFault::Timeout, "fwsec-boot-halt-timeout"},
    {ExecutionFault::StartWrite, "fwsec-boot-start-write-failed"},
    {ExecutionFault::MailboxError, "fwsec-boot-mailbox-error"},
    {ExecutionFault::FrtsError, "fwsec-boot-frts-error"},
    {ExecutionFault::CpuUnreadable, "fwsec-boot-cpu-unreadable"}
  };
  for (const auto &test : failedBoots) {
    ExecutionIO io; io.executionFault = test.fault; FWSECExecution::Result r;
    FWSECExecution::run(io, r); verifyLifetime(io, r);
    assert(!r.passed && r.boot.startAttempted && !r.boot.passed && !io.cleanups);
    assert(!std::strcmp(r.boot.status, test.status));
    assert(r.stage.lifecycle.resourcesRetained && !r.stage.lifecycle.cleanupVerified);
    if (test.fault == ExecutionFault::Timeout) assert(r.boot.haltPolls == rtxfwsecboot::PollLimit);
    if (test.fault == ExecutionFault::StartWrite) assert(!r.boot.startWriteAccepted && !io.cpuPolls);
    ++cases;
  }
  const struct { ExecutionFault fault; const char *status; } prestart[] = {
    {ExecutionFault::FreshCheck, "fwsec-boot-fresh-region-check-failed"},
    {ExecutionFault::HostCorruptBeforeBoot, "fwsec-boot-gate-incomplete"},
    {ExecutionFault::LostRegion, "fwsec-boot-gate-incomplete"},
    {ExecutionFault::LostProvider, "fwsec-boot-gate-incomplete"},
    {ExecutionFault::DmaGate, "fwsec-boot-gate-incomplete"},
    {ExecutionFault::ConfigWrite, "fwsec-boot-config-write-failed"},
    {ExecutionFault::ConfigReadback, "fwsec-boot-config-readback-failed"},
    {ExecutionFault::CpuChangesBeforeStart, "fwsec-boot-prestart-state-changed"}
  };
  for (const auto &test : prestart) {
    ExecutionIO io; io.executionFault = test.fault; FWSECExecution::Result r;
    FWSECExecution::run(io, r); verifyLifetime(io, r);
    assert(r.stageVerified && !r.boot.startAttempted && !io.cpuStarts && !io.persistent);
    assert(!std::strcmp(r.boot.status, test.status));
    assert(io.closes == 1 && io.cleaned && !r.providerHeld && !r.regionOwned);
    ++cases;
  }
  for (ExecutionFault mode : {ExecutionFault::PostReset, ExecutionFault::PostDrain,
       ExecutionFault::PostCleanup, ExecutionFault::PostFinalSnapshot, ExecutionFault::HostCorruptAfterBoot}) {
    ExecutionIO io; io.executionFault = mode; FWSECExecution::Result r;
    FWSECExecution::run(io, r); verifyLifetime(io, r);
    assert(r.boot.passed && !r.passed && r.stage.lifecycle.resourcesRetained);
    assert(!io.closes && !io.cleaned && io.prepared && io.allocated);
    assert(io.cleanups == (mode == ExecutionFault::PostCleanup ? 1U : 0U));
    ++cases;
  }
  for (StageFault fault : {StageFault::BoardMismatch, StageFault::OpenBusy, StageFault::WrongIdentity,
       StageFault::Allocation, StageFault::Prepare, StageFault::CorruptDmemLast,
       StageFault::DMATimeoutFirst, StageFault::TimeoutResetFailure}) {
    ExecutionIO io; io.fault = fault; FWSECExecution::Result r;
    FWSECExecution::run(io, r); verifyLifetime(io, r);
    assert(!r.stageVerified && !io.freshCalls && !io.bootReads && !io.bootWrites && !io.cpuStarts);
    if (fault == StageFault::BoardMismatch || fault == StageFault::OpenBusy) {
      assert(!io.unmaps && !io.cleanups && !io.closes && !io.allocated);
    }
    ++cases;
  }
  std::printf("FWSEC execution: %u staging/boot/cleanup integration cases passed\n", cases);
  return 0;
}
