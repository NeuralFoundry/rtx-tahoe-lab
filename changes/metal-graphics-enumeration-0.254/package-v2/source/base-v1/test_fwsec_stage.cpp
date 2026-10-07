#include "driver/FWSECStageProtocol.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace FalconDMA;

enum class StageFault {
  None, BoardMismatch, OpenBusy, WrongIdentity, InitialCommand, PCIeBusy,
  Mapper, Allocation, Prepare, Segments, SegmentHigh, SegmentAliased,
  SegmentUnaligned, SegmentShort, SegmentsChangedFirst, SegmentsChangedSecond,
  Map, MemoryEnable, Environment, EngineBusy, CPUAlreadyRunning,
  RISCVAlreadyRunning, ResetAssert, ResetRelease, ScrubStuck, CPUAfterReset,
  ImemTooSmall, DmemTooSmall, HWCFGUnreadable, PIOAddress, PIOCanary,
  PIOUnreadable, PublishFirst, PublishSecond, PublishFourth, StaleLastWindow,
  DMAFull, WrongTarget, WrongTargetReadbackLie, FBIFControl, DMAAddress,
  MasterEnable, DMATimeoutFirst, DMATimeoutLast, DMAUnreadable,
  CompletionInvalid, DropDmemFirst, DropDmemLast, CorruptDmemFirst,
  CorruptDmemLast, CPUBufferChanged, CPUFirstWindowChanged,
  CPUSecondWindowChanged, CPUThirdWindowChanged, CommandBetweenBlocks,
  TimeoutResetFailure, TimeoutDrainFailure, CleanupResetFailure,
  PCIeDrainFailure, MasterRestore, MemoryRestore, TargetsClear, Cleanup,
  FinalUnreadable, FinalDMAActive, FinalAddressNonzero,
  SilentImemDrop
};

// Synthetic fixture only: no executable firmware. Distinct words across all
// four host windows make reuse of an old window detectable through DMEM.
static unsigned imageFixture(unsigned byteOffset) {
  assert(byteOffset < FWSECStage::ImageBytes && !(byteOffset & 3));
  const unsigned index = byteOffset / 4;
  return 0x73574d49U ^ (index * 2654435761U) ^ ((index + 17U) * 2246822519U);
}

struct StageIO {
  StageFault fault = StageFault::None;
  Boot0::Facts f;
  std::array<unsigned, Count> regs{};
  std::array<unsigned, FWSECStage::ImemBytes / 4> imem{};
  std::array<unsigned, FWSECStage::DmemWords> dmem{};
  std::array<Boot0::U64, Preparation::DmaPages> addresses{{
    0x474a90000ULL, 0x48c040000ULL, 0x4391a0000ULL, 0x4ef310000ULL
  }};
  std::vector<unsigned> memory;
  std::vector<unsigned> publications;
  unsigned cmd = 0, generations = 0, submissions = 0, imemCopies = 0, dmemCopies = 0;
  unsigned resets = 0, resetAsserts = 0, pendingReads = 0, commandReads = 0;
  unsigned pioReads = 0, dataReads = 0, delayCalls = 0, writes = 0, opens = 0;
  unsigned currentWindow = 0, clearOffsetReads = 0, verifications = 0;
  unsigned transferCommand = 0, transferOffset = 0;
  Boot0::U64 transferSource = 0;
  bool opened = false, allocated = false, prepared = false, mapped = false;
  bool cleaned = false, pending = false, published = false, finalOffsetCleared = false;
  bool coreSelection = false, intactBeforeReset = false, checkedBeforeReset = false;
  bool exactCapacity = false, lowAddress = false;

  StageIO() {
    f.identity = 0x252010de; f.subsystem = 0x104c1043;
    f.targetBDF = f.barTypesValid = true;
    f.command = 0; f.pmcsr = 8; f.link = 0x1083;
    f.bar0 = f.descriptor0 = 0xfb000000; f.length0 = 16ULL << 20;
    f.bar1 = f.descriptor1 = 0x824000000; f.length1 = 64ULL << 20;
    regs[HWCFG] = 0x80420100; regs[HWCFG2] = 0x80000000;
    regs[CPUCTL] = 0x10; regs[RISCVCPU] = 0x10;
    regs[DMACMD] = 2; regs[BCR] = 1; regs[FBIFCTL] = 0x190;
    regs[TRANSCFG] = 0x110; regs[DMACTL] = 0x80;
  }

  bool imageMatchesBoard() { return fault != StageFault::BoardMismatch; }
  unsigned imageWord(unsigned byteOffset) { return imageFixture(byteOffset); }
  bool open() { ++opens; return opened = fault != StageFault::OpenBusy; }
  void close() { assert(opened && !mapped); opened = false; }
  Boot0::Facts facts() {
    if (fault == StageFault::WrongIdentity) f.identity ^= 1;
    if (fault == StageFault::InitialCommand) f.command = 4;
    return f;
  }
  unsigned command() { return cmd; }
  unsigned deviceStatus() {
    if (!resets && fault == StageFault::PCIeBusy) return 0x20;
    if (resets >= 2 && fault == StageFault::PCIeDrainFailure) return 0x20;
    return 0;
  }
  bool selectMapper() { return fault != StageFault::Mapper; }
  bool allocate() {
    allocated = true; memory.resize(Preparation::DmaSize / 4);
    return fault != StageFault::Allocation;
  }
  bool prepare() { assert(allocated); return prepared = fault != StageFault::Prepare; }
  bool segments(Preparation::Segment *s, unsigned &count, Boot0::U64 &end) {
    assert(prepared); ++generations;
    count = Preparation::DmaPages; end = Preparation::DmaSize;
    for (unsigned i = 0; i < count; ++i) {
      s[i].address = addresses[i]; s[i].length = Preparation::PageSize;
    }
    if (fault == StageFault::SegmentHigh) s[0].address = 1ULL << 40;
    if (fault == StageFault::SegmentAliased) s[1].address = s[0].address;
    if (fault == StageFault::SegmentUnaligned) s[0].address += 256;
    if (fault == StageFault::SegmentShort) s[0].length = 256;
    if ((fault == StageFault::SegmentsChangedFirst && generations >= 2) ||
        (fault == StageFault::SegmentsChangedSecond && generations >= 3)) s[0].address += 65536;
    return fault != StageFault::Segments;
  }
  bool mapFalcon() { assert(prepared && cmd == 0); return mapped = fault != StageFault::Map; }
  void unmapFalcon() { mapped = false; }
  void setMemory(bool value) {
    if ((value && fault == StageFault::MemoryEnable) || (!value && fault == StageFault::MemoryRestore)) return;
    cmd = value ? cmd | 2U : cmd & ~2U;
  }
  void setMaster(bool value) {
    if ((value && fault == StageFault::MasterEnable) || (!value && fault == StageFault::MasterRestore)) return;
    cmd = value ? cmd | 4U : cmd & ~4U;
  }
  bool environmentReady(unsigned values[3]) {
    values[0] = 0; values[1] = 0x8b8f; values[2] = 0x3ff;
    return fault != StageFault::Environment;
  }
  void delayUs(unsigned value) {
    assert(value == 10 || value == 100);
    assert(++delayCalls <= FWSECStage::Blocks * 600U + 1000U);
  }

  bool timedOut() const {
    return fault == StageFault::DMATimeoutFirst || fault == StageFault::DMAUnreadable ||
      fault == StageFault::TimeoutResetFailure || fault == StageFault::TimeoutDrainFailure ||
      (fault == StageFault::DMATimeoutLast && submissions == FWSECStage::Blocks);
  }

  // Decode the physical source address into the currently published host
  // pages. No copy operation infers payload from the command ordinal.
  unsigned hostWord(Boot0::U64 source) const {
    assert(!(source & 3));
    for (unsigned page = 0; page < Preparation::DmaPages; ++page) {
      if (source >= addresses[page] && source + 4 <= addresses[page] + Preparation::PageSize)
        return memory.at(page * Preparation::PageSize / 4 + unsigned(source - addresses[page]) / 4);
    }
    assert(false && "DMA source is outside the prepared IOVM segments");
    return 0;
  }

  void completeTransfer() {
    assert(pending && prepared && published && cmd == 6);
    const bool toImem = (transferCommand & 0x10) != 0;
    assert(transferCommand == (toImem ? 0x614U : 0x600U));
    assert(!(transferOffset & 255));
    assert(transferOffset + 256 <= (toImem ? FWSECStage::ImemBytes : FWSECStage::DmemBytes));
    bool copy = (regs[TRANSCFG] & 0x10007) == 5;
    if (toImem && fault == StageFault::SilentImemDrop) copy = false;
    if (!toImem && ((fault == StageFault::DropDmemFirst && transferOffset == 0) ||
        (fault == StageFault::DropDmemLast && transferOffset == FWSECStage::DmemBytes - 256))) copy = false;
    if (copy) {
      for (unsigned word = 0; word < 64; ++word) {
        unsigned value = hostWord(transferSource + word * 4);
        if (!toImem && fault == StageFault::CorruptDmemFirst && transferOffset == 0 && word == 0) value ^= 1;
        if (!toImem && fault == StageFault::CorruptDmemLast &&
            transferOffset == FWSECStage::DmemBytes - 256 && word == 63) value ^= 1;
        if (toImem) imem.at(transferOffset / 4 + word) = value;
        else dmem.at(transferOffset / 4 + word) = value;
      }
      if (toImem) ++imemCopies; else ++dmemCopies;
    }
    pending = false; regs[DMACMD] = transferCommand | 2;
  }

  unsigned read(Reg reg) {
    assert(mapped && (cmd & 2) && reg < Count);
    if (!resetAsserts) {
      if (fault == StageFault::EngineBusy && reg == Engine) return 1;
      if (fault == StageFault::CPUAlreadyRunning && reg == CPUCTL) return 2;
      if (fault == StageFault::RISCVAlreadyRunning && reg == RISCVCPU) return 0x80;
    }
    if (resetAsserts && fault == StageFault::ScrubStuck && reg == HWCFG2) return 0x1000;
    if (resets && fault == StageFault::CPUAfterReset && reg == CPUCTL) return 2;
    if (resets && reg == HWCFG) {
      if (fault == StageFault::HWCFGUnreadable) return 0xbad0fb00;
      if (fault == StageFault::ImemTooSmall) return 224U | (8U << 9);
      if (fault == StageFault::DmemTooSmall) return 256U | (7U << 9);
      if (exactCapacity) return 225U | (8U << 9);
    }
    if (reg == DMACMD) {
      if (resets == 1 && !submissions && fault == StageFault::DMAFull) return 1;
      if (pending && fault == StageFault::DMAUnreadable) return 0xbadf0000;
      if (resets >= 2 && fault == StageFault::TimeoutDrainFailure) return 0;
      if (pending && !timedOut() && ++pendingReads == 3) completeTransfer();
      if (!pending && submissions && resets == 1 &&
          fault == StageFault::CompletionInvalid && ++commandReads == 2) return 0;
    }
    if (reg == TRANSCFG && fault == StageFault::WrongTargetReadbackLie && regs[reg] == 0x114) return 0x115;
    if (reg == FBOFFSET && finalOffsetCleared && ++clearOffsetReads > 1 && fault == StageFault::FinalUnreadable)
      return 0xffffffff;
    if (finalOffsetCleared && reg == DMACMD && fault == StageFault::FinalDMAActive) return 0;
    if (finalOffsetCleared && reg == DMABASE && fault == StageFault::FinalAddressNonzero) return 0x1234;
    if (reg == DMEMD) {
      const unsigned offset = regs[DMEMC];
      assert(offset < FWSECStage::DmemBytes && !(offset & 3));
      if (!submissions) ++pioReads; else ++dataReads;
      unsigned value = dmem[offset / 4];
      if (!submissions && fault == StageFault::PIOUnreadable) value = 0xbad0fb00;
      if (!submissions && fault == StageFault::PIOCanary && offset == FWSECStage::DmemBytes - 4) value ^= 1;
      return value;
    }
    return regs[reg];
  }

  void inspectStagedBytes() {
    checkedBeforeReset = true; intactBeforeReset = true;
    for (unsigned word = 0; word < imem.size(); ++word)
      if (imem[word] != imageFixture(word * 4)) intactBeforeReset = false;
    for (unsigned word = 0; word < dmem.size(); ++word)
      if (dmem[word] != imageFixture(FWSECStage::ImemBytes + word * 4)) intactBeforeReset = false;
    if (fault == StageFault::None && !lowAddress) assert(intactBeforeReset);
  }

  void write(Reg reg, unsigned value) {
    assert(mapped && (cmd & 2) && reg < Count); ++writes;
    // A CPU start, BOOTVEC, BROM, IMEM PIO or any other extra write aborts
    // the test immediately, even if the reported result would be a failure.
    assert(reg == Engine || reg == BCR || reg == TRANSCFG || reg == FBIFCTL ||
      reg == DMACTL || reg == DMABASE || reg == DMABASE1 || reg == DMAOFFSET ||
      reg == FBOFFSET || reg == DMEMC || reg == DMEMD || reg == DMACMD);
    if (reg == Engine) {
      assert(value == 0 || value == 1);
      if (value == 1) {
        ++resetAsserts;
        if (resetAsserts == 2 && submissions == FWSECStage::Blocks && !pending) inspectStagedBytes();
        if (fault == StageFault::ResetAssert ||
            (submissions && (fault == StageFault::TimeoutResetFailure || fault == StageFault::CleanupResetFailure))) return;
      }
      if (!value && fault == StageFault::ResetRelease) return;
      if (!value && (regs[Engine] & 1)) {
        ++resets; pending = false; imem.fill(0); dmem.fill(0);
        regs[DMACTL] = 0; regs[DMACMD] = 2;
        regs[DMABASE] = regs[DMABASE1] = regs[DMAOFFSET] = regs[FBOFFSET] = 0;
        regs[TRANSCFG] = 0x110; regs[FBIFCTL] = 0x190;
      }
      regs[reg] = value; return;
    }
    if (reg == BCR) { assert(value == 0); regs[reg] = 1; coreSelection = true; return; }
    if (reg == DMEMC) {
      assert(!(value & ~0xffffffU) && value < FWSECStage::DmemBytes && !(value & 3));
      regs[reg] = fault == StageFault::PIOAddress ? value ^ 4 : value; return;
    }
    if (reg == DMEMD) {
      assert(!submissions && regs[DMEMC] < FWSECStage::DmemBytes && !(regs[DMEMC] & 3));
      dmem.at(regs[DMEMC] / 4) = value; return;
    }
    assert(!pending); // Never reprogram translation/base while DMA is active.
    if (reg == TRANSCFG && (fault == StageFault::WrongTarget || fault == StageFault::WrongTargetReadbackLie)) value = 0x114;
    if (reg == FBIFCTL && (value & 0x80) && fault == StageFault::FBIFControl) { regs[reg] &= ~0x80U; return; }
    if (reg == DMABASE && value && fault == StageFault::DMAAddress) return;
    if (reg == DMACTL && value == 1 && fault == StageFault::TargetsClear) return;
    if (reg == FBOFFSET && value == 0 && submissions && resets >= 2) {
      finalOffsetCleared = true; clearOffsetReads = 0;
    }
    if (reg != DMACMD) { regs[reg] = value; return; }
    assert(value == 0x614 || value == 0x600);
    assert(cmd == 6 && prepared && published && !pending);
    assert(++submissions <= FWSECStage::Blocks);
    assert(regs[DMAOFFSET] == regs[FBOFFSET]);
    assert(!(regs[DMACTL] & 1) && (regs[FBIFCTL] & 0x80));
    assert(regs[DMABASE1] == 0);
    transferSource = (Boot0::U64(regs[DMABASE]) << 8) + regs[FBOFFSET];
    transferOffset = regs[DMAOFFSET]; transferCommand = value;
    // Secure IMEM tags must retain the virtual image offset; DMEM starts at
    // zero even when its source is in the middle of a host page/window.
    const unsigned imageOffset = (value == 0x614 ? 0U : FWSECStage::ImemBytes) + transferOffset;
    const unsigned localOffset = imageOffset % Preparation::DmaSize;
    assert(transferSource == addresses[localOffset / Preparation::PageSize] + localOffset % Preparation::PageSize);
    assert(currentWindow == imageOffset - localOffset);
    pending = true; pendingReads = 0; commandReads = 0; regs[DMACMD] = value;
    if (fault == StageFault::CommandBetweenBlocks && submissions == 1) {
      completeTransfer(); cmd = 2;
    }
  }

  bool publishFWSEC(unsigned imageWindowOffset) {
    assert(prepared && !pending && imageWindowOffset < FWSECStage::ImageBytes);
    assert(!(imageWindowOffset % Preparation::DmaSize));
    assert(cmd == (imageWindowOffset ? 6U : 2U));
    publications.push_back(imageWindowOffset);
    if ((fault == StageFault::PublishFirst && imageWindowOffset == 0) ||
        (fault == StageFault::PublishSecond && imageWindowOffset == Preparation::DmaSize) ||
        (fault == StageFault::PublishFourth && imageWindowOffset == 3 * Preparation::DmaSize)) return false;
    const bool stale = fault == StageFault::StaleLastWindow && imageWindowOffset == 3 * Preparation::DmaSize;
    if (!stale) {
      for (unsigned word = 0; word < memory.size(); ++word) {
        const unsigned offset = imageWindowOffset + word * 4;
        memory[word] = offset < FWSECStage::ImageBytes ? imageFixture(offset) : 0;
      }
    }
    currentWindow = imageWindowOffset; published = true; return true;
  }
  bool verifyFWSEC() {
    assert(!pending && published); ++verifications;
    if ((fault == StageFault::CPUBufferChanged && currentWindow == 3 * Preparation::DmaSize) ||
        (fault == StageFault::CPUFirstWindowChanged && currentWindow == 0) ||
        (fault == StageFault::CPUSecondWindowChanged && currentWindow == Preparation::DmaSize) ||
        (fault == StageFault::CPUThirdWindowChanged && currentWindow == 2 * Preparation::DmaSize)) memory.back() ^= 1;
    for (unsigned word = 0; word < memory.size(); ++word) {
      const unsigned offset = currentWindow + word * 4;
      if (memory[word] != (offset < FWSECStage::ImageBytes ? imageFixture(offset) : 0)) return false;
    }
    return true;
  }
  bool cleanup() {
    assert(!pending && !(cmd & 4));
    if (submissions) {
      assert(resets == 2 && (regs[DMACTL] & 1) && !(regs[FBIFCTL] & 0x80));
      assert(!regs[DMABASE] && !regs[DMABASE1] && !regs[DMAOFFSET] && !regs[FBOFFSET]);
      for (unsigned word : imem) assert(word == 0);
      for (unsigned word : dmem) assert(word == 0);
    }
    cleaned = true; allocated = prepared = false; memory.clear();
    return fault != StageFault::Cleanup;
  }
};

static void bounded(const StageIO &io, const FWSECStage::Result &out) {
  const auto &r = out.lifecycle;
  assert(!io.opened && !io.mapped && io.submissions <= FWSECStage::Blocks);
  assert(r.resetCount <= 2 && r.resetPolls <= 630 && r.drainPolls <= 400);
  assert(r.initialReads <= SnapshotCount && r.finalReads <= SnapshotCount);
  assert(out.canaryMatched <= FWSECStage::DmemWords && out.publishCount <= 4);
  assert(out.imemSubmitted <= FWSECStage::ImemBlocks && out.dmemSubmitted <= FWSECStage::DmemBlocks);
  assert(out.imemCompleted <= out.imemSubmitted && out.dmemCompleted <= out.dmemSubmitted);
  assert(out.dmemReads <= FWSECStage::DmemWords && out.dmemMatched <= out.dmemReads);
  assert(out.dmaPolls <= FWSECStage::Blocks * 600U);
  assert(out.imemSubmitted + out.dmemSubmitted == io.submissions);
}

static unsigned addressCases() {
  Preparation::Segment segments[Preparation::DmaPages];
  StageIO io;
  for (unsigned page = 0; page < Preparation::DmaPages; ++page) {
    segments[page].address = io.addresses[page]; segments[page].length = Preparation::PageSize;
  }
  unsigned cases = 0;
  for (unsigned offset = 0; offset < FWSECStage::ImageBytes; offset += 256) {
    unsigned base = 0, fbOffset = 0; Boot0::U64 source = 0;
    assert(FWSECStage::blockAddress(segments, offset, base, fbOffset, source));
    const unsigned local = offset % Preparation::DmaSize;
    assert(source == io.addresses[local / Preparation::PageSize] + local % Preparation::PageSize);
    assert(fbOffset == (offset < FWSECStage::ImemBytes ? offset : offset - FWSECStage::ImemBytes));
    assert((Boot0::U64(base) << 8) + fbOffset == source); ++cases;
  }
  unsigned base = 0, fbOffset = 0; Boot0::U64 source = 0;
  for (unsigned offset : {1U, 252U, FWSECStage::ImageBytes, 0xffffff00U}) {
    assert(!FWSECStage::blockAddress(segments, offset, base, fbOffset, source)); ++cases;
  }
  const auto original = segments[0];
  for (Boot0::U64 address : {Boot0::U64(0), Boot0::U64(0x1100), Boot0::U64(1) << 40}) {
    segments[0].address = address;
    assert(!FWSECStage::blockAddress(segments, 0, base, fbOffset, source)); ++cases;
  }
  segments[0] = original; segments[0].length = 256;
  assert(!FWSECStage::blockAddress(segments, 0, base, fbOffset, source)); ++cases;
  segments[0] = original; segments[0].address = 0x1000;
  assert(!FWSECStage::blockAddress(segments, Preparation::DmaSize, base, fbOffset, source)); ++cases;
  // The highest legal 40-bit page is accepted without truncation or overflow.
  segments[0] = original; segments[0].address = (Boot0::U64(1) << 40) - Preparation::PageSize;
  assert(FWSECStage::blockAddress(segments, 0, base, fbOffset, source));
  assert(source == segments[0].address && base == 0xfffffff0U); ++cases;
  assert(FWSECStage::blockAddress(segments, Preparation::PageSize - 256, base, fbOffset, source));
  assert(source + 256 == (Boot0::U64(1) << 40)); ++cases;
  return cases;
}

int main() {
  unsigned cases = 0;
  for (unsigned variant = 0; variant < 3; ++variant) {
    StageIO io;
    if (variant == 1) io.regs[BCR] = 0x10;
    if (variant == 2) io.exactCapacity = true;
    FWSECStage::Result out;
    if (variant == 2) FWSECStage::run(io, out); else out = FWSECStage::run(io);
    const auto &r = out.lifecycle; bounded(io, out);
    assert(r.passed && !std::strcmp(r.status, "FWSEC-staged-DMEM-verified-not-executed"));
    assert(io.cleaned && !io.prepared && io.cmd == 0 && io.submissions == FWSECStage::Blocks);
    assert(r.quiescent && r.targetsCleared && r.cpuIntact && !r.resourcesRetained);
    assert(r.resetCount == 2 && io.pioReads == FWSECStage::DmemWords && io.dataReads == FWSECStage::DmemWords);
    assert(io.checkedBeforeReset && io.intactBeforeReset);
    assert(io.imemCopies == 225 && io.dmemCopies == 8 && io.coreSelection == (variant == 1));
    assert(out.canaryMatched == 512 && out.dmemReads == 512 && out.dmemMatched == 512);
    assert(out.imemSubmitted == 225 && out.imemCompleted == 225 && out.dmemSubmitted == 8 && out.dmemCompleted == 8);
    assert(out.publishCount == 4 && io.generations == 5 && io.publications.size() == 4 && io.verifications == 4);
    assert(out.dmaPolls == FWSECStage::Blocks * 5);
    for (unsigned window = 0; window < 4; ++window) assert(io.publications[window] == window * Preparation::DmaSize);
    for (unsigned word = 0; word < FWSECStage::DmemWords; ++word)
      assert(out.dmem[word] == imageFixture(FWSECStage::ImemBytes + word * 4));
    for (unsigned block = 0; block < FWSECStage::Blocks; ++block)
      assert(out.completions[block] == (block < 225 ? 0x616U : 0x602U));
    ++cases;
  }

  struct Case { StageFault fault; const char *status; unsigned submitted; bool retained; };
  const Case failures[] = {
    {StageFault::BoardMismatch, "fwsec-board-image-mismatch", 0, false},
    {StageFault::OpenBusy, "falcon-device-busy", 0, false},
    {StageFault::WrongIdentity, nullptr, 0, false},
    {StageFault::InitialCommand, nullptr, 0, false},
    {StageFault::PCIeBusy, "falcon-PCIe-not-idle", 0, false},
    {StageFault::Mapper, "falcon-allocation-failed", 0, false},
    {StageFault::Allocation, "falcon-allocation-failed", 0, false},
    {StageFault::Prepare, "falcon-prepare-failed", 0, false},
    {StageFault::Segments, "falcon-segments-invalid", 0, false},
    {StageFault::SegmentHigh, "falcon-segments-invalid", 0, false},
    {StageFault::SegmentAliased, "falcon-segments-invalid", 0, false},
    {StageFault::SegmentUnaligned, "falcon-segments-invalid", 0, false},
    {StageFault::SegmentShort, "falcon-segments-invalid", 0, false},
    {StageFault::SegmentsChangedFirst, "fwsec-publish-failed", 0, false},
    {StageFault::SegmentsChangedSecond, "fwsec-publish-failed", 64, false},
    {StageFault::Map, "falcon-map-failed", 0, false},
    {StageFault::MemoryEnable, "falcon-memory-enable-failed", 0, false},
    {StageFault::Environment, "falcon-environment-not-ready", 0, false},
    {StageFault::EngineBusy, "falcon-engine-not-idle", 0, false},
    {StageFault::CPUAlreadyRunning, "falcon-engine-not-idle", 0, false},
    {StageFault::RISCVAlreadyRunning, "falcon-engine-not-idle", 0, false},
    {StageFault::ResetAssert, "falcon-reset-failed", 0, false},
    {StageFault::ResetRelease, "falcon-reset-failed", 0, false},
    {StageFault::ScrubStuck, "falcon-reset-failed", 0, false},
    {StageFault::CPUAfterReset, "falcon-reset-failed", 0, false},
    {StageFault::ImemTooSmall, "fwsec-memory-size-invalid", 0, false},
    {StageFault::DmemTooSmall, "fwsec-memory-size-invalid", 0, false},
    {StageFault::HWCFGUnreadable, "fwsec-memory-size-invalid", 0, false},
    {StageFault::PIOAddress, "falcon-PIO-address-failed", 0, false},
    {StageFault::PIOCanary, "fwsec-PIO-canary-failed", 0, false},
    {StageFault::PIOUnreadable, "fwsec-PIO-canary-failed", 0, false},
    {StageFault::PublishFirst, "fwsec-publish-failed", 0, false},
    {StageFault::PublishSecond, "fwsec-publish-failed", 64, false},
    {StageFault::PublishFourth, "fwsec-publish-failed", 192, false},
    {StageFault::StaleLastWindow, "fwsec-DMEM-data-mismatch", 233, false},
    {StageFault::DMAFull, "falcon-DMA-not-idle", 0, false},
    {StageFault::WrongTarget, "falcon-FBIF-setup-failed", 0, false},
    {StageFault::WrongTargetReadbackLie, "fwsec-DMEM-data-mismatch", 233, false},
    {StageFault::FBIFControl, "falcon-FBIF-setup-failed", 0, false},
    {StageFault::DMAAddress, "falcon-DMA-address-failed", 0, false},
    {StageFault::MasterEnable, "falcon-master-enable-failed", 0, false},
    {StageFault::DMATimeoutFirst, "falcon-DMA-timeout", 1, false},
    {StageFault::DMATimeoutLast, "falcon-DMA-timeout", 233, false},
    {StageFault::DMAUnreadable, "falcon-DMA-timeout", 1, false},
    {StageFault::CompletionInvalid, "fwsec-DMA-completion-invalid", 1, false},
    {StageFault::DropDmemFirst, "fwsec-DMEM-data-mismatch", 233, false},
    {StageFault::DropDmemLast, "fwsec-DMEM-data-mismatch", 233, false},
    {StageFault::CorruptDmemFirst, "fwsec-DMEM-data-mismatch", 233, false},
    {StageFault::CorruptDmemLast, "fwsec-DMEM-data-mismatch", 233, false},
    {StageFault::CPUBufferChanged, "falcon-CPU-buffer-changed", 233, false},
    {StageFault::CPUFirstWindowChanged, "falcon-CPU-buffer-changed", 64, false},
    {StageFault::CPUSecondWindowChanged, "falcon-CPU-buffer-changed", 128, false},
    {StageFault::CPUThirdWindowChanged, "falcon-CPU-buffer-changed", 192, false},
    {StageFault::CommandBetweenBlocks, "falcon-command-changed", 1, false},
    {StageFault::TimeoutResetFailure, "falcon-quiescence-failed-resources-retained", 1, true},
    {StageFault::TimeoutDrainFailure, "falcon-quiescence-failed-resources-retained", 1, true},
    {StageFault::CleanupResetFailure, "falcon-quiescence-failed-resources-retained", 233, true},
    {StageFault::PCIeDrainFailure, "falcon-quiescence-failed-resources-retained", 233, true},
    {StageFault::MasterRestore, "falcon-quiescence-failed-resources-retained", 233, true},
    {StageFault::MemoryRestore, "falcon-quiescence-failed-resources-retained", 233, true},
    {StageFault::TargetsClear, "falcon-quiescence-failed-resources-retained", 233, true},
    {StageFault::Cleanup, "falcon-cleanup-failed", 233, false},
    {StageFault::FinalUnreadable, "falcon-quiescence-failed-resources-retained", 233, true},
    {StageFault::FinalDMAActive, "falcon-quiescence-failed-resources-retained", 233, true},
    {StageFault::FinalAddressNonzero, "falcon-quiescence-failed-resources-retained", 233, true}
  };
  for (const auto &test : failures) {
    StageIO io; io.fault = test.fault;
    const auto out = FWSECStage::run(io); const auto &r = out.lifecycle; bounded(io, out);
    if (r.passed || io.submissions != test.submitted || (test.status && std::strcmp(r.status, test.status)))
      std::fprintf(stderr, "Stage fault %u: passed=%d status=%s submissions=%u; expected=%s/%u\n",
        unsigned(test.fault), r.passed, r.status, io.submissions, test.status ? test.status : "any failure", test.submitted);
    assert(!r.passed && io.submissions == test.submitted);
    if (test.status) assert(!std::strcmp(r.status, test.status));
    assert(r.resourcesRetained == test.retained);
    if (test.retained) assert(io.prepared && io.allocated && !io.cleaned);
    else if (test.fault == StageFault::BoardMismatch || test.fault == StageFault::OpenBusy)
      assert(!io.cleaned && !io.allocated && !io.writes);
    else assert(io.cleaned && !io.prepared && !io.allocated);
    if (test.fault == StageFault::BoardMismatch) assert(io.opens == 0);
    if (test.fault == StageFault::DMATimeoutFirst) {
      assert(out.dmaPolls == 202 && out.imemCompleted == 0 && !io.pending);
      assert(r.quiescent && r.targetsCleared && r.cleanupVerified);
    }
    if (test.fault == StageFault::DMATimeoutLast) assert(out.imemCompleted == 225 && out.dmemCompleted == 7);
    if (test.fault == StageFault::TimeoutResetFailure) assert(io.pending && !r.quiescent);
    if (test.fault == StageFault::DropDmemLast) assert(out.dmemMatched == 448 && out.mismatchWord == 448);
    if (test.fault == StageFault::CorruptDmemLast) assert(out.dmemMatched == 511 && out.mismatchWord == 511);
    if (test.fault == StageFault::StaleLastWindow) assert(!out.dmemMatched && out.mismatchWord == 0);
    ++cases;
  }

  {
    StageIO io; io.lowAddress = true; io.addresses = {{0x1000, 0x9000, 0x5000, 0xd000}};
    const auto out = FWSECStage::run(io); bounded(io, out);
    assert(!out.lifecycle.passed && !std::strcmp(out.lifecycle.status, "falcon-DMA-address-failed"));
    assert(io.submissions == 64 && io.cleaned && !out.lifecycle.resourcesRetained); ++cases;
  }
  {
    // An idle completion is the available IMEM evidence, not a byte readback.
    // This explicit observability test must not be counted as an IMEM proof.
    StageIO io; io.fault = StageFault::SilentImemDrop;
    const auto out = FWSECStage::run(io); bounded(io, out);
    assert(out.lifecycle.passed && out.dmemMatched == 512 && out.imemCompleted == 225);
    assert(io.checkedBeforeReset && !io.intactBeforeReset && !io.imemCopies && io.dmemCopies == 8); ++cases;
  }
  const unsigned addressChecks = addressCases();
  std::printf("FWSEC staging: %u lifecycle/transfer/fault cases and %u address cases passed; no firmware execution\n",
    cases, addressChecks);
  return 0;
}
