#include "driver/FWSECBootProtocol.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

// Synthetic register backend only. No firmware bytes, MMIO or OS adapter.
using namespace rtxfwsecboot;

struct BootIO {
  std::array<U32, RegCount> regs{};
  std::vector<std::pair<Reg, U32>> writes;
  unsigned reads = 0, starts = 0, polls = 0, delays = 0, finishAt = 2;
  unsigned failWrite = 0;
  Reg corruptConfigRead = RegCount, changeBeforeStart = RegCount, corruptOutcome = RegCount;
  U32 prestartValue = 0, outcomeError = 0xffffffffU;
  bool started = false, completed = false, ignoreStart = false;
  bool timeout = false, unreadableCpu = false, mailboxError = false, scratchError = false;
  bool missingWpr = false, wrongWpr = false, lowNibble = false;
  U32 upperWpr = U32((FrtsOffset + FrtsBytes) >> 12) << 4;
  BootIO() {
    regs[PmcBoot0] = BoardBoot0;
    regs[CpuCtl] = 0x10; regs[RiscvCpu] = 0x10; regs[CoreSelect] = 1;
    regs[DmaCmd] = 2; regs[Hwcfg2] = 0x47f7;
    regs[Mailbox0] = 0xcafebeef; regs[Mailbox1] = 0x12345678;
  }
  void complete() {
    completed = true; regs[CpuCtl] = 0x10;
    regs[Mailbox0] = mailboxError ? 7 : 0;
    regs[FrtsScratch] = scratchError ? 0x12340000 : 0x000055aa;
    regs[WprLo] = (U32(FrtsOffset >> 12) << 4) + (wrongWpr ? 0x10 : 0);
    regs[WprHi] = missingWpr ? 0 : upperWpr;
    if (lowNibble) { regs[WprLo] |= 0xf; regs[WprHi] |= 0xb; }
  }
  U32 read(Reg reg) {
    assert(reg != CpuAlias && reg < RegCount); ++reads;
    if (writes.size() == 7 && !started && reg == changeBeforeStart) return prestartValue;
    if (!writes.empty() && !started && reg == corruptConfigRead && writes.back().first == reg)
      return regs[reg] ^ 1;
    if (started && reg == CpuCtl) {
      ++polls;
      if (unreadableCpu) return 0xbadf1000;
      if (ignoreStart) return 0x10;
      if (!timeout && polls >= finishAt && !completed) complete();
      return completed ? 0x10 : 0;
    }
    if (started && reg == corruptOutcome) return outcomeError;
    return regs[reg];
  }
  bool write(Reg reg, U32 value) {
    assert(reg == Rm || reg == Mailbox0 || reg == BromPara || reg == BromEngine ||
           reg == BromUcode || reg == BromAlgorithm || reg == BootVector ||
           reg == CpuCtl || reg == CpuAlias);
    writes.emplace_back(reg, value);
    if (reg == CpuCtl || reg == CpuAlias) {
      assert(value == 2); started = true; ++starts;
    } else {
      regs[reg] = value;
    }
    // Simulates an error reported after a potentially delivered MMIO write.
    return writes.size() != failWrite;
  }
  void delayUs(U32 usec) { assert(usec == PollDelayUs && started); ++delays; }
};

static unsigned scenarios = 0;
static Gate ready() {
  Gate g;
  g.stageVerified = g.ramDMAIdle = g.hostBufferIntact = g.regionOwned = true;
  g.vramSize = VramBytes; g.frtsOffset = FrtsOffset; g.frtsSize = FrtsBytes;
  return g;
}
static Result check(BootIO &io, const Gate &g, const char *expected) {
  ++scenarios;
  const Result r = run(io, g);
  if (std::strcmp(r.status, expected)) {
    std::fprintf(stderr, "scenario %u: expected %s, got %s\n", scenarios, expected, r.status);
    assert(false);
  }
  assert(r.writeAttempts == io.writes.size());
  assert(r.haltPolls == io.polls && r.delayCalls == io.delays);
  assert(r.haltPolls <= PollLimit && r.delayCalls <= PollLimit);
  assert(r.startAttempted == (io.starts != 0) && io.starts <= 1);
  assert(r.sideEffectsMayRemain == r.startAttempted);
  assert(r.requiresCallerQuiescence == !io.writes.empty());
  assert(r.registersTouched == !io.writes.empty());
  assert(r.authenticatedExecutionInferred == r.passed);
  if (r.passed) {
    assert(r.halted && r.startWriteAccepted && r.outcomeRead && r.wprTransitionObserved);
    assert(r.verifiedWrites == 7 && r.writeAttempts == 8);
    assert(r.mailbox0 == 0 && (r.scratch >> 16) == 0);
    assert(wprPage(r.wprLo) == U32(FrtsOffset >> 12) && wprPage(r.wprHi));
  }
  return r;
}

int main() {
  static_assert(Addresses[CpuAlias] == 0x110130 && Addresses[BromPara] == 0x111210, "offsets");
  static_assert(Addresses[Rm] == 0x110084 && Addresses[FrtsScratch] == 0x1438, "offsets");
  static_assert(Addresses[WprLo] == 0x1fa824 && Addresses[WprHi] == 0x1fa828, "offsets");
  static_assert(U64(PollLimit) * PollDelayUs == 2000000, "bounded requested wait");
  const Gate good = ready();
  for (bool alias : {false, true}) {
    BootIO io; if (alias) io.regs[CpuCtl] |= 0x40;
    Result r = check(io, good, "fwsec-frts-execution-verified");
    assert(r.aliasUsed == alias && io.writes.back().first == (alias ? CpuAlias : CpuCtl));
    assert(r.runningObserved && r.haltPolls == 2);
    const Reg expectedRegs[] = {Rm, Mailbox0, BromPara, BromEngine, BromUcode, BromAlgorithm, BootVector};
    const U32 expectedValues[] = {BoardBoot0, 0, 1444, 0x400, 9, 1, 0};
    for (unsigned i = 0; i < 7; ++i)
      assert(io.writes[i].first == expectedRegs[i] && io.writes[i].second == expectedValues[i]);
  }
  { BootIO io; io.finishAt = 1;
    Result r = check(io, good, "fwsec-frts-execution-verified");
    assert(!r.runningObserved && r.wprTransitionObserved); }
  { BootIO io; io.finishAt = PollLimit;
    Result r = check(io, good, "fwsec-frts-execution-verified"); assert(r.haltPolls == PollLimit); }
  { BootIO io; io.lowNibble = true; check(io, good, "fwsec-frts-execution-verified"); }
  // Only vendor-grounded upper-bound initialization is asserted; no invented
  // upper endpoint convention. This fixture is not a valid VRAM allocator.
  { BootIO io; io.upperWpr += 0x10; check(io, good, "fwsec-frts-execution-verified"); }

  for (unsigned field = 0; field < 4; ++field) {
    BootIO io; Gate g = good;
    if (field == 0) g.stageVerified = false;
    if (field == 1) g.ramDMAIdle = false;
    if (field == 2) g.hostBufferIntact = false;
    if (field == 3) g.regionOwned = false;
    check(io, g, "fwsec-boot-gate-incomplete"); assert(!io.reads && io.writes.empty());
  }
  for (unsigned field = 0; field < 6; ++field) {
    BootIO io; Gate g = good;
    if (field == 0) g.vramSize = 0;
    if (field == 1) g.vramSize += 1ULL << 30;
    if (field == 2) g.frtsOffset -= 0x1000;
    if (field == 3) g.frtsOffset += 1;
    if (field == 4) g.frtsSize = 0;
    if (field == 5) g.frtsSize *= 2;
    check(io, g, "fwsec-boot-region-mismatch"); assert(!io.reads && io.writes.empty());
  }
  for (unsigned reg = 0; reg < InitialCount; ++reg) {
    for (U32 invalid : {0xffffffffU, 0xbadf1234U, 0xbad01234U}) {
      BootIO io; io.regs[reg] = invalid;
      Result r = check(io, good, "fwsec-boot-preflight-unreadable");
      assert(r.failedRegister == reg && r.initialReads == reg + 1 && io.writes.empty());
    }
  }
  { BootIO io; io.regs[PmcBoot0] ^= 1; check(io, good, "fwsec-boot-board-mismatch"); }
  const std::pair<Reg, U32> badEngine[] = {
    {Engine, 1}, {Hwcfg2, 0x1000}, {CpuCtl, 0}, {CpuCtl, 0x12},
    {RiscvCpu, 0x80}, {CoreSelect, 0}, {CoreSelect, 0x11}, {DmaCmd, 0}, {DmaCmd, 3}
  };
  for (const auto &bad : badEngine) {
    BootIO io; io.regs[bad.first] = bad.second;
    check(io, good, "fwsec-boot-engine-not-ready"); assert(io.writes.empty());
  }
  { BootIO io; io.regs[WprHi] = 0x10;
    check(io, good, "fwsec-boot-wpr-already-initialized"); assert(io.writes.empty()); }
  { BootIO io; io.regs[WprHi] = 0xf; check(io, good, "fwsec-frts-execution-verified"); }

  for (unsigned failed = 1; failed <= 7; ++failed) {
    BootIO io; io.failWrite = failed;
    Result r = check(io, good, "fwsec-boot-config-write-failed");
    assert(r.writeAttempts == failed && r.verifiedWrites == failed - 1 && !r.startAttempted);
  }
  for (Reg reg : {Rm, Mailbox0, BromPara, BromEngine, BromUcode, BromAlgorithm, BootVector}) {
    BootIO io; io.corruptConfigRead = reg;
    Result r = check(io, good, "fwsec-boot-config-readback-failed");
    assert(r.failedRegister == reg && !r.startAttempted);
  }
  for (const auto &bad : badEngine) {
    BootIO io; io.changeBeforeStart = bad.first; io.prestartValue = bad.second;
    Result r = check(io, good, "fwsec-boot-prestart-state-changed");
    assert(r.writeAttempts == 7 && !r.startAttempted);
  }
  for (Reg reg : {CpuCtl, RiscvCpu, CoreSelect, DmaCmd, Engine, Hwcfg2, WprHi}) {
    BootIO io; io.changeBeforeStart = reg; io.prestartValue = 0xbadf0000;
    check(io, good, "fwsec-boot-prestart-state-changed");
  }
  { BootIO io; io.changeBeforeStart = WprHi; io.prestartValue = 0x10;
    check(io, good, "fwsec-boot-prestart-state-changed"); }
  { BootIO io; io.changeBeforeStart = CpuCtl; io.prestartValue = 0x50;
    Result r = check(io, good, "fwsec-frts-execution-verified"); assert(r.aliasUsed); }

  for (bool alias : {false, true}) {
    BootIO io; if (alias) io.regs[CpuCtl] |= 0x40; io.failWrite = 8;
    Result r = check(io, good, "fwsec-boot-start-write-failed");
    assert(r.startAttempted && !r.startWriteAccepted && r.sideEffectsMayRemain && !r.haltPolls);
  }
  { BootIO io; io.unreadableCpu = true;
    Result r = check(io, good, "fwsec-boot-cpu-unreadable"); assert(r.haltPolls == 1); }
  { BootIO io; io.timeout = true;
    Result r = check(io, good, "fwsec-boot-halt-timeout");
    assert(r.haltPolls == PollLimit && !r.halted && r.outcomeRead); }
  { BootIO io; io.finishAt = PollLimit + 1;
    check(io, good, "fwsec-boot-halt-timeout"); }
  { BootIO io; io.ignoreStart = true;
    Result r = check(io, good, "fwsec-boot-wpr-outcome-mismatch");
    assert(!r.runningObserved && !r.wprTransitionObserved); }
  { BootIO io; io.mailboxError = true; check(io, good, "fwsec-boot-mailbox-error"); }
  { BootIO io; io.scratchError = true; check(io, good, "fwsec-boot-frts-error"); }
  { BootIO io; io.missingWpr = true; check(io, good, "fwsec-boot-wpr-outcome-mismatch"); }
  { BootIO io; io.wrongWpr = true; check(io, good, "fwsec-boot-wpr-outcome-mismatch"); }
  for (Reg reg : {Mailbox0, Mailbox1, FrtsScratch, WprLo, WprHi}) {
    for (U32 invalid : {0xffffffffU, 0xbadf1234U, 0xbad01234U}) {
      BootIO io; io.corruptOutcome = reg; io.outcomeError = invalid;
      check(io, good, "fwsec-boot-outcome-unreadable");
    }
  }
  std::printf("%u offline FWSEC boot scenarios passed (no hardware, no firmware payload).\n", scenarios);
}
