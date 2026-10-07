#pragma once
#include "FalconProtocol.hpp"

// Exact measured board geometry. The normal run() stages and then resets.
// stageOwned() transfers ownership of the still-live staging transaction to
// its caller, which MUST finish it even on failure. Neither entry starts a CPU.
namespace FWSECStage {
constexpr unsigned ImemBytes = 57600, DmemBytes = 2048, ImageBytes = ImemBytes + DmemBytes;
constexpr unsigned BlockBytes = 256, ImemBlocks = ImemBytes / BlockBytes;
constexpr unsigned DmemBlocks = DmemBytes / BlockBytes, Blocks = ImemBlocks + DmemBlocks;
constexpr unsigned DmemWords = DmemBytes / 4;
struct Result {
  FalconDMA::Result lifecycle;
  unsigned hwcfg = 0xffffffffU, canaryMatched = 0, publishCount = 0, dmaPolls = 0;
  unsigned imemSubmitted = 0, imemCompleted = 0, dmemSubmitted = 0, dmemCompleted = 0;
  unsigned dmemReads = 0, dmemMatched = 0, mismatchWord = 0xffffffffU, mismatchValue = 0;
  unsigned completions[Blocks] = {}, dmem[DmemWords] = {};
  bool boardMatched = false;
};

// FBOFFS is also the IMEM virtual tag. Rebase every 256-byte block against
// its actual IOVM segment, rather than assuming contiguous physical pages.
inline bool blockAddress(const Preparation::Segment *segments, unsigned imageOffset,
                         unsigned &base, unsigned &fbOffset, Boot0::U64 &source) {
  if (imageOffset >= ImageBytes || (imageOffset & 255)) return false;
  const unsigned local = imageOffset % Preparation::DmaSize;
  const auto &segment = segments[local / Preparation::PageSize];
  const unsigned inPage = local % Preparation::PageSize;
  if (segment.length != Preparation::PageSize || (segment.address & 4095) || !segment.address ||
      segment.address >= (Boot0::U64(1) << 40) || segment.address > (Boot0::U64(1) << 40) - segment.length) return false;
  source = segment.address + inPage;
  fbOffset = imageOffset < ImemBytes ? imageOffset : imageOffset - ImemBytes;
  if (source < fbOffset || ((source - fbOffset) & 255)) return false;
  base = unsigned((source - fbOffset) >> 8);
  return (Boot0::U64(base) << 8) + fbOffset == source;
}

struct Transaction { bool opened = false, verified = false, anyDMA = false; };

template <class IO> Transaction stageOwned(IO &io, Result &out) {
  using namespace FalconDMA;
  Transaction transaction;
  auto &r = out.lifecycle;
  out.boardMatched = io.imageMatchesBoard();
  if (!out.boardMatched) { r.status = "fwsec-board-image-mismatch"; return transaction; }
  if (!io.open()) { r.status = "falcon-device-busy"; return transaction; }
  transaction.opened = true;
  const Boot0::Facts facts = io.facts(); r.commandBefore = facts.command;
  bool good = false, anyDMA = false;
  do {
    if (const char *error = Boot0::preflight(facts)) { r.status = error; break; }
    r.deviceStatusBefore = io.deviceStatus();
    if (r.deviceStatusBefore == 0xffff || (r.deviceStatusBefore & 0x20)) { r.status = "falcon-PCIe-not-idle"; break; }
    if (!io.selectMapper() || !io.allocate()) { r.status = "falcon-allocation-failed"; break; }
    if (!io.prepare()) { r.status = "falcon-prepare-failed"; break; }
    r.prepared = true;
    if (!io.segments(r.segments, r.count, r.end) || !Preparation::validSegments(r.segments, r.count, r.end)) {
      r.status = "falcon-segments-invalid"; break;
    }
    if (!io.mapFalcon()) { r.status = "falcon-map-failed"; break; }
    if (io.command() != 0) { r.status = "falcon-command-changed"; break; }
    r.memoryAttempted = true; io.setMemory(true);
    if (io.command() != 2) { r.status = "falcon-memory-enable-failed"; break; }
    snapshot(io, r.initial, r.initialReads);
    if (!readable(r.initial[Engine]) || !readable(r.initial[HWCFG2]) || !readable(r.initial[CPUCTL]) ||
        !readable(r.initial[BCR]) || !readable(r.initial[RISCVCPU])) { r.status = "falcon-register-unreadable"; break; }
    if (!io.environmentReady(r.environment)) { r.status = "falcon-environment-not-ready"; break; }
    if ((r.initial[Engine] & 1) || (r.initial[RISCVCPU] & 0x80) ||
        ((r.initial[CPUCTL] & 2) && !(r.initial[CPUCTL] & 0x10))) { r.status = "falcon-engine-not-idle"; break; }
    if (!reset(io, r)) { r.status = "falcon-reset-failed"; break; }
    out.hwcfg = io.read(HWCFG);
    if (!readable(out.hwcfg) || ((out.hwcfg & 0x1ff) << 8) < ImemBytes ||
        ((out.hwcfg & 0x3fe00) >> 1) < DmemBytes) { r.status = "fwsec-memory-size-invalid"; break; }
    good = true;
    for (unsigned word = 0; word < DmemWords; ++word) {
      if (!writeCheck(io, DMEMC, word * 4, 0xffffff)) { good = false; r.status = "falcon-PIO-address-failed"; break; }
      io.write(DMEMD, ~io.imageWord(ImemBytes + word * 4));
    }
    for (unsigned word = 0; word < DmemWords && good; ++word) {
      if (!writeCheck(io, DMEMC, word * 4, 0xffffff)) { good = false; r.status = "falcon-PIO-address-failed"; break; }
      const unsigned value = io.read(DMEMD);
      if (value != ~io.imageWord(ImemBytes + word * 4)) {
        good = false; r.status = "fwsec-PIO-canary-failed";
        out.mismatchWord = word; out.mismatchValue = value; break;
      }
      ++out.canaryMatched;
    }
    for (unsigned block = 0; block < Blocks && good; ++block) {
      const unsigned offset = block * BlockBytes;
      if (io.command() != (r.masterAttempted ? 6U : 2U)) { good = false; r.status = "falcon-command-changed"; break; }
      if (!waitBits(io, DMACMD, 3, 2, 200, 100, out.dmaPolls)) { good = false; r.status = "falcon-DMA-not-idle"; break; }
      if (offset % Preparation::DmaSize == 0) {
        if (offset && !io.verifyFWSEC()) { good = false; r.status = "falcon-CPU-buffer-changed"; break; }
        if (!io.publishFWSEC(offset) || !sameSegments(io, r)) { good = false; r.status = "fwsec-publish-failed"; break; }
        ++out.publishCount;
      }
      const unsigned fbif = io.read(FBIFCTL), trans = io.read(TRANSCFG);
      r.targetsAttempted = true;
      if (!readable(fbif) || !readable(trans) || !writeCheck(io, FBIFCTL, fbif | 0x80) ||
          !writeCheck(io, DMACTL, 0, 1) || !writeCheck(io, TRANSCFG, (trans & ~0x10007U) | 5)) {
        good = false; r.status = "falcon-FBIF-setup-failed"; break;
      }
      unsigned base = 0, fbOffset = 0; Boot0::U64 source = 0;
      if (!blockAddress(r.segments, offset, base, fbOffset, source) ||
          !writeCheck(io, DMABASE, base) || !writeCheck(io, DMABASE1, 0) ||
          !writeCheck(io, DMAOFFSET, fbOffset) || !writeCheck(io, FBOFFSET, fbOffset)) {
        good = false; r.status = "falcon-DMA-address-failed"; break;
      }
      if (!r.masterAttempted) { r.masterAttempted = true; io.setMaster(true); }
      r.commandEnabled = io.command();
      if (r.commandEnabled != 6) { good = false; r.status = "falcon-master-enable-failed"; break; }
      if (!waitBits(io, DMACMD, 3, 2, 200, 100, out.dmaPolls)) { good = false; r.status = "falcon-DMA-not-idle"; break; }
      anyDMA = true;
      if (block < ImemBlocks) ++out.imemSubmitted; else ++out.dmemSubmitted;
      io.write(DMACMD, block < ImemBlocks ? 0x614 : 0x600);
      if (!waitBits(io, DMACMD, 3, 2, 200, 100, out.dmaPolls)) { good = false; r.status = "falcon-DMA-timeout"; break; }
      const unsigned done = io.read(DMACMD); out.completions[block] = done;
      if (!readable(done) || (done & 3) != 2) { good = false; r.status = "fwsec-DMA-completion-invalid"; break; }
      if (block < ImemBlocks) ++out.imemCompleted; else ++out.dmemCompleted;
    }
    for (unsigned word = 0; word < DmemWords && good; ++word) {
      if (!writeCheck(io, DMEMC, word * 4, 0xffffff)) { good = false; r.status = "falcon-PIO-address-failed"; break; }
      const unsigned value = io.read(DMEMD); out.dmem[word] = value; ++out.dmemReads;
      if (value != io.imageWord(ImemBytes + word * 4)) {
        good = false; r.status = "fwsec-DMEM-data-mismatch";
        out.mismatchWord = word; out.mismatchValue = value; break;
      }
      ++out.dmemMatched;
    }
    if (good) {
      r.cpuIntact = io.verifyFWSEC(); good = r.cpuIntact;
      r.status = good ? "FWSEC-staged-DMEM-verified-not-executed" : "falcon-CPU-buffer-changed";
    }
  } while (false);
  transaction.verified = good;
  transaction.anyDMA = anyDMA;
  return transaction;
}
template <class IO> void run(IO &io, Result &out) {
  const Transaction transaction = stageOwned(io, out);
  if (transaction.opened) FalconDMA::finish(io, out.lifecycle, transaction.verified, transaction.anyDMA);
}
template <class IO> Result run(IO &io) {
  Result out;
  run(io, out);
  return out;
}
}
