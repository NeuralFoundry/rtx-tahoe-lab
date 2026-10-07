#include "driver/FalconProtocol.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace FalconDMA;

// Model bus mastering, address translation, DMEM PIO and asynchronous DMA
// separately. Reads do not manufacture the expected payload: only a valid
// DMA command can copy the published host buffer over the PIO canary.
enum class Fault {
  None, OpenBusy, WrongIdentity, InitialCommand, PCIeBusy, PCIeUnavailable,
  Mapper, Allocation, Prepare, Segments, SegmentOutside40Bits, SegmentAliased,
  Map, CommandBeforeEnable, MemoryEnable, InitialUnreadable, Environment,
  EngineBusy, CPUAlreadyRunning, RISCVAlreadyRunning, ResetAssert,
  ResetRelease, ScrubStuck, CPUAfterReset, DMEMTooSmall, PIOAddress,
  PIOUnreadable, PIOCanary, PublishFirst, PublishSecond, SegmentsChanged,
  DMAFull, WrongTarget, WrongTargetReadbackLie, FBIFControl, DMAAddress,
  MasterEnable, DMATimeout, DMAUnreadable, TimeoutResetFailure,
  TimeoutDrainFailure, CleanupResetFailure, PCIeDrainFailure, CopyFirstWord,
  CopyLastWord, StaleSecondPhase, CPUBufferChanged, MasterRestore,
  MemoryRestore, TargetsClear, Cleanup, FinalSnapshotUnreadable, FinalDMAActive, FinalAddressNonzero,
  CommandAfterFirstPhase
};

struct FalconIO {
  static constexpr Boot0::U64 Address = 0x437af2000ULL;
  Fault fault = Fault::None;
  Boot0::Facts f;
  std::array<unsigned, Count> regs{};
  std::array<unsigned, Words> dmem{};
  std::vector<unsigned> memory;
  unsigned cmd = 0, phase = 0, generations = 0, submissions = 0;
  unsigned resets = 0, resetAsserts = 0, pioReads = 0, dmaReads = 0;
  unsigned delayCalls = 0, clearOffsetReads = 0, writes = 0;
  bool opened = false, allocated = false, prepared = false, mapped = false;
  bool cleaned = false, pending = false, published = false, dmaReadable = false;
  bool finalOffsetCleared = false, coreSelection = false;

  FalconIO() {
    f.identity = 0x252010de; f.subsystem = 0x104c1043;
    f.targetBDF = f.barTypesValid = true;
    f.command = 0; f.pmcsr = 8; f.link = 0x1083;
    f.bar0 = f.descriptor0 = 0xfb000000; f.length0 = 16ULL << 20;
    f.bar1 = f.descriptor1 = 0x824000000; f.length1 = 64ULL << 20;
    regs[HWCFG] = 0x200; // One 256-byte DMEM block.
    regs[DMACMD] = 2; regs[BCR] = 1; regs[FBIFCTL] = 0x40;
  }

  bool open() { return opened = fault != Fault::OpenBusy; }
  void close() { assert(opened && !mapped); opened = false; }
  Boot0::Facts facts() {
    if (fault == Fault::WrongIdentity) f.identity ^= 1;
    if (fault == Fault::InitialCommand) f.command = 4;
    return f;
  }
  unsigned command() { return cmd; }
  unsigned deviceStatus() {
    if (!resets && fault == Fault::PCIeBusy) return 0x20;
    if (!resets && fault == Fault::PCIeUnavailable) return 0xffff;
    if (resets >= 2 && fault == Fault::PCIeDrainFailure) return 0x20;
    return 0;
  }
  bool selectMapper() { return fault != Fault::Mapper; }
  bool allocate() {
    allocated = true; memory.resize(Preparation::DmaSize / 4);
    return fault != Fault::Allocation;
  }
  bool prepare() { assert(allocated); return prepared = fault != Fault::Prepare; }
  bool segments(Preparation::Segment *s, unsigned &count, Boot0::U64 &end) {
    assert(prepared); ++generations;
    count = Preparation::DmaPages; end = Preparation::DmaSize;
    for (unsigned i = 0; i < count; ++i) {
      s[i].address = Address + i * 8192; s[i].length = 4096;
    }
    if (fault == Fault::SegmentOutside40Bits) s[0].address = 1ULL << 40;
    if (fault == Fault::SegmentAliased) s[1].address = s[0].address;
    if (fault == Fault::SegmentsChanged && generations > 1) s[0].address += 65536;
    return fault != Fault::Segments;
  }
  bool mapFalcon() {
    assert(prepared && cmd == 0);
    if (fault == Fault::CommandBeforeEnable) cmd = 2;
    return mapped = fault != Fault::Map;
  }
  void unmapFalcon() { mapped = false; }
  void setMemory(bool value) {
    if ((value && fault == Fault::MemoryEnable) || (!value && fault == Fault::MemoryRestore)) return;
    cmd = value ? cmd | 2U : cmd & ~2U;
  }
  void setMaster(bool value) {
    if ((value && fault == Fault::MasterEnable) || (!value && fault == Fault::MasterRestore)) return;
    cmd = value ? cmd | 4U : cmd & ~4U;
  }
  bool environmentReady(unsigned values[3]) {
    values[0] = 0; values[1] = 0x8b8f; values[2] = 0x3ff;
    return fault != Fault::Environment;
  }
  void delayUs(unsigned value) {
    assert(value == 10 || value == 100);
    assert(++delayCalls <= 2000); // A hung mock cannot hide an unbounded poll.
  }

  unsigned read(Reg reg) {
    assert(mapped && (cmd & 2) && reg < Count);
    if (!resetAsserts) {
      if (fault == Fault::InitialUnreadable && reg == HWCFG2) return 0xbad0fb00;
      if (fault == Fault::EngineBusy && reg == Engine) return 1;
      if (fault == Fault::CPUAlreadyRunning && reg == CPUCTL) return 2;
      if (fault == Fault::RISCVAlreadyRunning && reg == RISCVCPU) return 0x80;
    }
    if (resetAsserts && fault == Fault::ScrubStuck && reg == HWCFG2) return 0x1000;
    if (resets && fault == Fault::CPUAfterReset && reg == CPUCTL) return 2;
    if (resets && fault == Fault::DMEMTooSmall && reg == HWCFG) return 0;
    if (reg == DMACMD) {
      if (resets == 1 && !submissions && fault == Fault::DMAFull) return 1;
      if (pending && fault == Fault::DMAUnreadable) return 0xbadf0000;
      if (resets >= 2 && fault == Fault::TimeoutDrainFailure) return 0;
    }
    if (reg == TRANSCFG && fault == Fault::WrongTargetReadbackLie && regs[reg] == 4) return 5;
    if (reg == FBOFFSET && finalOffsetCleared && ++clearOffsetReads > 1 &&
        fault == Fault::FinalSnapshotUnreadable) return 0xffffffff;
    if (finalOffsetCleared && reg == DMACMD && fault == Fault::FinalDMAActive) return 0;
    if (finalOffsetCleared && reg == DMABASE && fault == Fault::FinalAddressNonzero) return 0x1234;
    if (reg == DMEMD) {
      const unsigned offset = regs[DMEMC] & 0x00ffffff;
      assert(offset < Bytes && !(offset & 3));
      if (dmaReadable) ++dmaReads; else ++pioReads;
      unsigned value = dmem[offset / 4];
      if (!dmaReadable && fault == Fault::PIOUnreadable) value = 0xbad0fb00;
      if (!dmaReadable && fault == Fault::PIOCanary && offset == 28) value ^= 1;
      if (fault == Fault::CommandAfterFirstPhase && dmaReads == Words) cmd = 2;
      return value;
    }
    return regs[reg];
  }

  void write(Reg reg, unsigned value) {
    assert(mapped && (cmd & 2) && reg < Count); ++writes;
    // CPU start/control, firmware upload, IMEM, and all unlisted writes fail
    // the tests regardless of what result the protocol later reports.
    assert(reg == Engine || reg == BCR || reg == TRANSCFG || reg == FBIFCTL ||
           reg == DMACTL || reg == DMABASE || reg == DMABASE1 ||
           reg == DMAOFFSET || reg == FBOFFSET || reg == DMEMC ||
           reg == DMEMD || reg == DMACMD);
    if (reg == Engine) {
      assert(value == 0 || value == 1);
      if (value == 1) {
        ++resetAsserts;
        if (fault == Fault::ResetAssert ||
            (submissions && (fault == Fault::TimeoutResetFailure || fault == Fault::CleanupResetFailure))) return;
      }
      if (!value && fault == Fault::ResetRelease) return;
      if (!value && (regs[Engine] & 1)) {
        ++resets; pending = false; dmaReadable = false; dmem.fill(0);
        regs[DMACTL] = 0; regs[DMACMD] = 2;
        regs[DMABASE] = regs[DMABASE1] = regs[DMAOFFSET] = regs[FBOFFSET] = 0;
        regs[TRANSCFG] = 0; regs[FBIFCTL] = 0x40;
      }
      regs[reg] = value; return;
    }
    if (reg == BCR) {
      assert(value == 0); regs[reg] = 1; coreSelection = true; return;
    }
    if (reg == DMEMC) {
      assert(!(value & ~0x00ffffffU) && value < Bytes && !(value & 3));
      if (fault == Fault::PIOAddress) { regs[reg] = value ^ 4; return; }
      regs[reg] = value; return;
    }
    if (reg == DMEMD) {
      const unsigned offset = regs[DMEMC];
      assert(offset < Bytes && !(offset & 3));
      dmem[offset / 4] = value; dmaReadable = false; return;
    }
    if (reg == TRANSCFG) {
      if (fault == Fault::WrongTarget || fault == Fault::WrongTargetReadbackLie) value = 4;
    }
    if (reg == FBIFCTL && (value & 0x80) && fault == Fault::FBIFControl) return;
    if (reg == DMABASE && value && fault == Fault::DMAAddress) return;
    if (reg == DMACTL && value == 1 && fault == Fault::TargetsClear) return;
    if (reg == FBOFFSET && value == 0 && submissions && resets >= 2) {
      finalOffsetCleared = true; clearOffsetReads = 0;
    }
    if (reg != DMACMD) { regs[reg] = value; return; }

    assert(value == 0x600); // Exactly ctx0/DMEM/read/256 bytes.
    assert(cmd == 6 && prepared && published && !pending);
    assert(++submissions <= 2);
    assert(regs[DMAOFFSET] == 0 && regs[FBOFFSET] == 0);
    assert(!(regs[DMACTL] & 1) && (regs[FBIFCTL] & 0x80));
    const Boot0::U64 address = ((Boot0::U64(regs[DMABASE1] & 0x1ff) << 32) |
                              regs[DMABASE]) << 8;
    assert(address == Address); // Decode actual programmed DMA address.
    pending = true; regs[DMACMD] = 0;
    if (fault == Fault::DMATimeout || fault == Fault::DMAUnreadable ||
        fault == Fault::TimeoutResetFailure || fault == Fault::TimeoutDrainFailure) return;
    if ((regs[TRANSCFG] & 7) == 5) {
      for (unsigned i = 0; i < Words; ++i) dmem[i] = memory.at(i);
      if (fault == Fault::CopyFirstWord) dmem[0] ^= 1;
      if (fault == Fault::CopyLastWord) dmem[Words - 1] ^= 1;
    }
    pending = false; regs[DMACMD] = 2; dmaReadable = true;
  }

  bool publishFalcon(unsigned nextPhase) {
    assert(prepared && nextPhase < 2 && cmd == (nextPhase ? 6U : 2U));
    phase = nextPhase;
    if ((fault == Fault::PublishFirst && !phase) || (fault == Fault::PublishSecond && phase)) return false;
    // The payload in memory is independent from read(DMEMD). Stale publication
    // on phase two copies phase one's bytes and must fail comparison.
    for (unsigned i = 0; i < memory.size(); ++i)
      memory[i] = pattern(i, (i < Words && fault != Fault::StaleSecondPhase) ? phase : 0);
    published = true; return true;
  }
  bool verifyFalcon() {
    if (fault == Fault::CPUBufferChanged) memory.back() ^= 1;
    for (unsigned i = 0; i < memory.size(); ++i)
      if (memory[i] != pattern(i, i < Words ? phase : 0)) return false;
    return true;
  }
  bool cleanup() {
    assert(!pending && !(cmd & 4));
    if (submissions) {
      assert((regs[DMACTL] & 1) && !(regs[FBIFCTL] & 0x80));
      assert(regs[DMABASE] == 0 && regs[DMABASE1] == 0);
      for (unsigned word : dmem) assert(word == 0);
    }
    cleaned = true; allocated = prepared = false; memory.clear();
    return fault != Fault::Cleanup;
  }
};

static void bounded(const FalconIO &io, const Result &r) {
  assert(!io.opened && !io.mapped && io.submissions <= 2);
  assert(r.resetCount <= 2 && r.resetPolls <= 600 && r.drainPolls <= 400);
  assert(r.initialReads <= SnapshotCount && r.finalReads <= SnapshotCount);
  for (const Phase &p : r.phase) {
    assert(p.submitted <= 1 && p.polls <= 600 && p.reads <= Words);
    assert(p.pioMatched <= Words && p.matched <= Words);
  }
}

int main() {
  unsigned cases = 0;
  for (bool selectCore : {false, true}) {
    FalconIO io; if (selectCore) io.regs[BCR] = 0x10;
    const auto r = run(io); bounded(io, r);
    assert(r.passed && !std::strcmp(r.status, "Falcon-DMA-host-to-DMEM-verified"));
    assert(io.cleaned && !io.prepared && io.cmd == 0 && io.submissions == 2);
    assert(r.quiescent && r.targetsCleared && r.cpuIntact && !r.resourcesRetained);
    assert(r.resetCount == 2 && io.dmaReads == 2 * Words && io.pioReads == 2 * Words);
    assert(io.coreSelection == selectCore);
    for (unsigned phase = 0; phase < 2; ++phase) {
      assert(r.phase[phase].pioMatched == Words && r.phase[phase].matched == Words);
      for (unsigned i = 0; i < Words; ++i) assert(r.phase[phase].data[i] == pattern(i, phase));
    }
    ++cases;
  }

  struct Case { Fault fault; const char *status; unsigned submissions; bool retain; };
  const Case failures[] = {
    {Fault::OpenBusy, "falcon-device-busy", 0, false},
    {Fault::WrongIdentity, nullptr, 0, false},
    {Fault::InitialCommand, nullptr, 0, false},
    {Fault::PCIeBusy, "falcon-PCIe-not-idle", 0, false},
    {Fault::PCIeUnavailable, "falcon-PCIe-not-idle", 0, false},
    {Fault::Mapper, "falcon-allocation-failed", 0, false},
    {Fault::Allocation, "falcon-allocation-failed", 0, false},
    {Fault::Prepare, "falcon-prepare-failed", 0, false},
    {Fault::Segments, "falcon-segments-invalid", 0, false},
    {Fault::SegmentOutside40Bits, "falcon-segments-invalid", 0, false},
    {Fault::SegmentAliased, "falcon-segments-invalid", 0, false},
    {Fault::Map, "falcon-map-failed", 0, false},
    {Fault::CommandBeforeEnable, "falcon-command-changed", 0, false},
    {Fault::MemoryEnable, "falcon-memory-enable-failed", 0, false},
    {Fault::InitialUnreadable, "falcon-register-unreadable", 0, false},
    {Fault::Environment, "falcon-environment-not-ready", 0, false},
    {Fault::EngineBusy, "falcon-engine-not-idle", 0, false},
    {Fault::CPUAlreadyRunning, "falcon-engine-not-idle", 0, false},
    {Fault::RISCVAlreadyRunning, "falcon-engine-not-idle", 0, false},
    {Fault::ResetAssert, "falcon-reset-failed", 0, false},
    {Fault::ResetRelease, "falcon-reset-failed", 0, false},
    {Fault::ScrubStuck, "falcon-reset-failed", 0, false},
    {Fault::CPUAfterReset, "falcon-reset-failed", 0, false},
    {Fault::DMEMTooSmall, "falcon-DMEM-size-invalid", 0, false},
    {Fault::PIOAddress, "falcon-PIO-address-failed", 0, false},
    {Fault::PIOUnreadable, "falcon-PIO-canary-failed", 0, false},
    {Fault::PIOCanary, "falcon-PIO-canary-failed", 0, false},
    {Fault::PublishFirst, "falcon-publish-failed", 0, false},
    {Fault::PublishSecond, "falcon-publish-failed", 1, false},
    {Fault::SegmentsChanged, "falcon-publish-failed", 0, false},
    {Fault::DMAFull, "falcon-DMA-not-idle", 0, false},
    {Fault::WrongTarget, "falcon-FBIF-setup-failed", 0, false},
    {Fault::WrongTargetReadbackLie, "falcon-DMA-data-mismatch", 1, false},
    {Fault::FBIFControl, "falcon-FBIF-setup-failed", 0, false},
    {Fault::DMAAddress, "falcon-DMA-address-failed", 0, false},
    {Fault::MasterEnable, "falcon-master-enable-failed", 0, false},
    {Fault::DMATimeout, "falcon-DMA-timeout", 1, false},
    {Fault::DMAUnreadable, "falcon-DMA-timeout", 1, false},
    {Fault::TimeoutResetFailure, "falcon-quiescence-failed-resources-retained", 1, true},
    {Fault::TimeoutDrainFailure, "falcon-quiescence-failed-resources-retained", 1, true},
    {Fault::CleanupResetFailure, "falcon-quiescence-failed-resources-retained", 2, true},
    {Fault::PCIeDrainFailure, "falcon-quiescence-failed-resources-retained", 2, true},
    {Fault::CopyFirstWord, "falcon-DMA-data-mismatch", 1, false},
    {Fault::CopyLastWord, "falcon-DMA-data-mismatch", 1, false},
    {Fault::StaleSecondPhase, "falcon-DMA-data-mismatch", 2, false},
    {Fault::CPUBufferChanged, "falcon-CPU-buffer-changed", 2, false},
    {Fault::MasterRestore, "falcon-quiescence-failed-resources-retained", 2, true},
    {Fault::MemoryRestore, "falcon-quiescence-failed-resources-retained", 2, true},
    {Fault::TargetsClear, "falcon-quiescence-failed-resources-retained", 2, true},
    {Fault::Cleanup, "falcon-cleanup-failed", 2, false},
    {Fault::FinalSnapshotUnreadable, "falcon-quiescence-failed-resources-retained", 2, true},
    {Fault::FinalDMAActive, "falcon-quiescence-failed-resources-retained", 2, true},
    {Fault::FinalAddressNonzero, "falcon-quiescence-failed-resources-retained", 2, true},
    {Fault::CommandAfterFirstPhase, "falcon-command-changed", 1, false}
  };
  for (const Case &test : failures) {
    FalconIO io; io.fault = test.fault; const auto r = run(io);
    bounded(io, r);
    if (r.passed || (test.status && std::strcmp(r.status, test.status)))
      std::fprintf(stderr, "Fault %u: passed=%d status=%s expected=%s\n",
                   unsigned(test.fault), r.passed, r.status, test.status ? test.status : "any failure");
    assert(!r.passed && io.submissions == test.submissions);
    if (test.status) assert(!std::strcmp(r.status, test.status));
    assert(r.resourcesRetained == test.retain);
    if (test.retain) assert(io.prepared && io.allocated && !io.cleaned);
    else if (test.fault != Fault::OpenBusy) assert(io.cleaned && !io.prepared && !io.allocated);
    else assert(!io.cleaned && !io.allocated && io.writes == 0);
    if (test.fault == Fault::DMATimeout) {
      assert(r.phase[0].polls == 202 && r.quiescent && r.targetsCleared);
      assert(!io.pending && r.cleanupVerified);
    }
    if (test.fault == Fault::TimeoutResetFailure) assert(io.pending && !r.quiescent);
    if (test.fault == Fault::StaleSecondPhase) {
      assert(r.phase[0].matched == Words && r.phase[1].matched == 0);
      assert(r.mismatchPhase == 1 && r.mismatchWord == 0);
    }
    if (test.fault == Fault::CopyLastWord) assert(r.mismatchWord == Words - 1 && r.phase[0].matched == Words - 1);
    ++cases;
  }
  std::printf("Falcon DMA: %u success, transfer, fault and retention cases passed\n", cases);
}
