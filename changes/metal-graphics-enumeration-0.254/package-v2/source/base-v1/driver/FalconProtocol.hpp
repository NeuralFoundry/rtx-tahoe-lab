#pragma once
#include "PreparationProtocol.hpp"
namespace FalconDMA {
constexpr unsigned Base = 0x110000, Bytes = 256, Words = Bytes / 4;
enum Reg : unsigned { Engine, HWCFG2, HWCFG, CPUCTL, DMACTL, DMACMD, BCR, RISCVCPU,
  TRANSCFG, FBIFCTL, DMABASE, DMABASE1, DMAOFFSET, FBOFFSET, DMEMC, DMEMD, Count };
constexpr unsigned Offsets[Count] = {0x3c0,0xf4,0x108,0x100,0x10c,0x118,0x1668,0x1388,
  0x600,0x624,0x110,0x128,0x114,0x11c,0x1c0,0x1c4};
constexpr unsigned SnapshotCount = 14;
inline bool readable(unsigned v) {
  return v != 0xffffffffU && (v & 0xffff0000U) != 0xbadf0000U && (v & 0xffff0000U) != 0xbad00000U;
}
inline unsigned pattern(unsigned i, unsigned phase) {
  return 0x4636444dU ^ (i * 2654435761U) ^ (phase ? 0xe3a1957bU : 0U);
}
inline unsigned canary(unsigned i, unsigned phase) { return ~pattern(i, phase); }
struct Phase {
  unsigned pioMatched = 0, submitted = 0, commandBefore = 0xffffffffU, commandAfter = 0xffffffffU;
  unsigned polls = 0, reads = 0, matched = 0, data[Words] = {};
};
struct Result {
  const char *status = "not-run";
  Preparation::Segment segments[Preparation::DmaPages];
  unsigned count = 0; Boot0::U64 end = 0;
  unsigned commandBefore = 0xffff, commandEnabled = 0xffff, commandAfter = 0xffff;
  unsigned deviceStatusBefore = 0xffff, deviceStatusAfter = 0xffff;
  unsigned initial[SnapshotCount] = {}, final[SnapshotCount] = {};
  unsigned environment[3] = {};
  unsigned initialReads = 0, finalReads = 0, resetCount = 0, resetPolls = 0, drainPolls = 0;
  unsigned mismatchPhase = 0xffffffffU, mismatchWord = 0xffffffffU, mismatchValue = 0;
  Phase phase[2];
  bool prepared = false, memoryAttempted = false, masterAttempted = false, resetAttempted = false, targetsAttempted = false;
  bool quiescent = false, targetsCleared = false, cleanupVerified = false, resourcesRetained = false;
  bool cpuIntact = false, passed = false;
};
template <class IO> bool waitBits(IO &io, Reg reg, unsigned mask, unsigned value,
                                 unsigned limit, unsigned delay, unsigned &polls) {
  for (unsigned i = 0; i < limit; ++i) {
    ++polls;
    if (!(io.command() & 2)) return false;
    const unsigned v = io.read(reg);
    if (!readable(v)) return false;
    if ((v & mask) == value) return true;
    io.delayUs(delay);
  }
  return false;
}
template <class IO> bool writeCheck(IO &io, Reg reg, unsigned value, unsigned mask = 0xffffffffU) {
  io.write(reg, value);
  const unsigned observed = io.read(reg);
  return readable(observed) && (observed & mask) == (value & mask);
}
template <class IO> bool reset(IO &io, Result &r) {
  r.resetAttempted = true; ++r.resetCount;
  // GA102 RESET_READY is advisory in Nouveau (documented HW issue). Bound
  // the preparation wait, but fail only unreadable accesses, not bit timeout.
  for (unsigned i = 0; i < 15; ++i) {
    const unsigned ready = io.read(HWCFG2); ++r.resetPolls;
    if (!readable(ready)) return false;
    if (ready & 0x80000000U) break;
    io.delayUs(10);
  }
  unsigned value = io.read(Engine);
  if (!readable(value)) return false;
  if (!writeCheck(io, Engine, value | 1U, 1)) return false;
  io.delayUs(10);
  if (!writeCheck(io, Engine, value & ~1U, 1)) return false;
  if (!waitBits(io, HWCFG2, 0x1000, 0, 200, 100, r.resetPolls)) return false;
  value = io.read(BCR);
  if (!readable(value)) return false;
  if (value & 0x10) {
    io.write(BCR, 0);
    if (!waitBits(io, BCR, 0x11, 1, 100, 100, r.resetPolls)) return false;
  }
  const unsigned cpu = io.read(CPUCTL), riscv = io.read(RISCVCPU);
  return readable(cpu) && readable(riscv) && !(cpu & 2) && !(riscv & 0x80);
}
template <class IO> bool snapshot(IO &io, unsigned *values, unsigned &reads) {
  bool valid = true;
  for (unsigned i = 0; i < SnapshotCount; ++i) {
    values[i] = io.read(Reg(i)); ++reads;
    if (!readable(values[i])) valid = false;
  }
  return valid;
}
template <class IO> bool sameSegments(IO &io, const Result &r) {
  Preparation::Segment s[Preparation::DmaPages]; unsigned count = 0; Boot0::U64 end = 0;
  if (!io.segments(s, count, end) || !Preparation::validSegments(s, count, end)) return false;
  for (unsigned i = 0; i < count; ++i) if (s[i].address != r.segments[i].address) return false;
  return true;
}
// Shared teardown for the measured DMA probe and the load-only FWSEC stage.
// The caller has opened the device; no firmware CPU may have been started.
template <class IO> void finish(IO &io, Result &r, bool good, bool anyDMA) {
  bool finalValid = false;
  if (r.resetAttempted && (io.command() & 2)) {
    bool stopped = reset(io, r);
    if (stopped) stopped = waitBits(io, DMACMD, 3, 2, 200, 100, r.drainPolls);
    if (stopped) {
      for (unsigned i = 0; i < 200; ++i) {
        r.deviceStatusAfter = io.deviceStatus(); ++r.drainPolls;
        if (r.deviceStatusAfter == 0xffff) break;
        if (!(r.deviceStatusAfter & 0x20)) { r.quiescent = true; break; }
        io.delayUs(100);
      }
    }
    if (r.quiescent) {
      const unsigned ctl = io.read(FBIFCTL);
      r.targetsCleared = readable(ctl) && writeCheck(io, DMACTL, 1, 1) &&
        writeCheck(io, FBIFCTL, ctl & ~0x80U) && writeCheck(io, DMABASE, 0) &&
        writeCheck(io, DMABASE1, 0) && writeCheck(io, DMAOFFSET, 0) && writeCheck(io, FBOFFSET, 0);
    }
    finalValid = snapshot(io, r.final, r.finalReads);
    if (finalValid) finalValid = !(r.final[Engine] & 1) && !(r.final[HWCFG2] & 0x1000) &&
      !(r.final[CPUCTL] & 2) && !(r.final[RISCVCPU] & 0x80) && (r.final[DMACMD] & 3) == 2 &&
      (r.final[DMACTL] & 1) && !(r.final[FBIFCTL] & 0x80) && !r.final[DMABASE] && !r.final[DMABASE1] &&
      !r.final[DMAOFFSET] && !r.final[FBOFFSET];
    if (!finalValid) { r.quiescent = false; r.targetsCleared = false; }
  }
  if (r.masterAttempted) io.setMaster(false);
  if (r.memoryAttempted) io.setMemory(false);
  r.commandAfter = io.command();
  io.unmapFalcon();
  r.resourcesRetained = (r.masterAttempted && r.commandAfter != 0) ||
    (anyDMA && !r.quiescent) || (r.targetsAttempted && !r.targetsCleared) ||
    ((anyDMA || r.targetsAttempted) && !finalValid);
  if (!r.resourcesRetained) r.cleanupVerified = io.cleanup();
  if (r.resourcesRetained) r.status = "falcon-quiescence-failed-resources-retained";
  else if (!r.cleanupVerified) r.status = "falcon-cleanup-failed";
  else if (r.memoryAttempted && r.commandAfter != 0) r.status = "falcon-command-restore-failed";
  else if (good && !finalValid) r.status = "falcon-final-state-invalid";
  io.close();
  r.passed = good && r.cpuIntact && r.quiescent && r.targetsCleared && r.cleanupVerified &&
    r.commandBefore == 0 && r.commandAfter == 0 && finalValid && r.finalReads == SnapshotCount;
}

template <class IO> Result run(IO &io) {
  Result r;
  if (!io.open()) { r.status = "falcon-device-busy"; return r; }
  const Boot0::Facts f = io.facts(); r.commandBefore = f.command;
  bool good = false, anyDMA = false;
  do {
    if (const char *error = Boot0::preflight(f)) { r.status = error; break; }
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
        ((r.initial[CPUCTL] & 2) && !(r.initial[CPUCTL] & 0x10))) {
      r.status = "falcon-engine-not-idle"; break;
    }
    if (!reset(io, r)) { r.status = "falcon-reset-failed"; break; }
    const unsigned hw = io.read(HWCFG);
    if (!readable(hw) || ((hw & 0x3fe00) >> 1) < Bytes) { r.status = "falcon-DMEM-size-invalid"; break; }
    good = true;
    for (unsigned phase = 0; phase < 2 && good; ++phase) {
      Phase &p = r.phase[phase];
      if (io.command() != (r.masterAttempted ? 6U : 2U)) { r.status = "falcon-command-changed"; good = false; break; }
      // Explicit byte offsets avoid depending on undocumented PIO auto-increment.
      // Canary is the complement of DMA payload; PIO echo cannot pass DMA test.
      for (unsigned i = 0; i < Words; ++i) {
        if (!writeCheck(io, DMEMC, i * 4, 0x00ffffff)) { r.status = "falcon-PIO-address-failed"; good = false; break; }
        io.write(DMEMD, canary(i, phase));
      }
      for (unsigned i = 0; i < Words && good; ++i) {
        if (!writeCheck(io, DMEMC, i * 4, 0x00ffffff)) { r.status = "falcon-PIO-address-failed"; good = false; break; }
        const unsigned v = io.read(DMEMD);
        if (v != canary(i, phase)) {
          r.mismatchPhase = phase; r.mismatchWord = i; r.mismatchValue = v;
          r.status = "falcon-PIO-canary-failed"; good = false; break;
        }
        ++p.pioMatched;
      }
      if (!good) break;
      if (!io.publishFalcon(phase) || !sameSegments(io, r)) { r.status = "falcon-publish-failed"; good = false; break; }
      if (!waitBits(io, DMACMD, 3, 2, 200, 100, p.polls)) { r.status = "falcon-DMA-not-idle"; good = false; break; }
      const unsigned fbif = io.read(FBIFCTL), trans = io.read(TRANSCFG);
      r.targetsAttempted = true;
      if (!readable(fbif) || !readable(trans) || !writeCheck(io, FBIFCTL, fbif | 0x80) ||
          !writeCheck(io, DMACTL, 0, 1) || !writeCheck(io, TRANSCFG, (trans & ~0x10007U) | 5)) {
        r.status = "falcon-FBIF-setup-failed"; good = false; break;
      }
      const Boot0::U64 address = r.segments[0].address;
      if (!writeCheck(io, DMABASE, unsigned(address >> 8)) || !writeCheck(io, DMABASE1, 0) ||
          !writeCheck(io, DMAOFFSET, 0) || !writeCheck(io, FBOFFSET, 0)) {
        r.status = "falcon-DMA-address-failed"; good = false; break;
      }
      if (!r.masterAttempted) { r.masterAttempted = true; io.setMaster(true); }
      r.commandEnabled = io.command();
      if (r.commandEnabled != 6) { r.status = "falcon-master-enable-failed"; good = false; break; }
      if (!waitBits(io, DMACMD, 3, 2, 200, 100, p.polls)) { r.status = "falcon-DMA-not-idle"; good = false; break; }
      p.commandBefore = io.read(DMACMD);
      anyDMA = true; p.submitted = 1;
      io.write(DMACMD, 0x600); // ctx0, nonsecure, system memory -> DMEM, 256 bytes.
      if (!waitBits(io, DMACMD, 3, 2, 200, 100, p.polls)) { r.status = "falcon-DMA-timeout"; good = false; break; }
      p.commandAfter = io.read(DMACMD);
      for (unsigned i = 0; i < Words; ++i) {
        if (!writeCheck(io, DMEMC, i * 4, 0x00ffffff)) { r.status = "falcon-PIO-address-failed"; good = false; break; }
        const unsigned v = io.read(DMEMD); p.data[i] = v; ++p.reads;
        if (v != pattern(i, phase)) {
          r.mismatchPhase = phase; r.mismatchWord = i; r.mismatchValue = v;
          r.status = "falcon-DMA-data-mismatch"; good = false; break;
        }
        ++p.matched;
      }
    }
    if (good) {
      r.cpuIntact = io.verifyFalcon(); good = r.cpuIntact;
      r.status = good ? "Falcon-DMA-host-to-DMEM-verified" : "falcon-CPU-buffer-changed";
    }
  } while (false);
  // Reset wipes test DMEM and DMA setup. Unlike PRAMIN, bus-master=0 alone
  // is NOT used as proof that asynchronous DMA has completed.
  finish(io, r, good, anyDMA);
  return r;
}
}
