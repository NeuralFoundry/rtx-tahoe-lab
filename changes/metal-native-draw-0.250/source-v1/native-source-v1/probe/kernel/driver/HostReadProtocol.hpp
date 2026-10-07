#pragma once
#include "PreparationProtocol.hpp"
namespace HostRead {
constexpr unsigned WindowOffset = 0x1700, ApertureOffset = 0x700000;
// NVIDIA 570.144 dev_bus.h: coherent target is TWO (not one).
constexpr unsigned CoherentTarget = 0x02000000, Words = 1024;
inline unsigned pattern(unsigned i, unsigned phase) {
  return 0x52745834U ^ (i * 2654435761U) ^ (phase ? 0xd7c39a65U : 0U);
}
inline unsigned hashWord(unsigned hash, unsigned word) { return (hash ^ word) * 16777619U; }
struct Phase {
  unsigned reads = 0, matched = 0, hash = 2166136261U, first = 0, last = 0;
};
struct Result {
  const char *status = "not-run";
  Preparation::Segment segments[Preparation::DmaPages];
  unsigned count = 0, commandBefore = 0xffff, commandEnabled = 0xffff, commandAfter = 0xffff;
  Boot0::U64 end = 0;
  unsigned windowBefore = 0xffffffffU, windowSecond = 0xffffffffU;
  unsigned windowProgrammed = 0, windowObserved = 0xffffffffU, windowAfter = 0xffffffffU;
  unsigned mismatchPhase = 0xffffffffU, mismatchWord = 0xffffffffU, mismatchValue = 0;
  Phase phase[2];
  bool memoryAttempted = false, masterAttempted = false, windowAttempted = false;
  bool prepared = false, contentsIntact = false, windowRestored = false;
  bool cleanupVerified = false, resourcesRetained = false, passed = false;
};
template <class IO> Result run(IO &io) {
  Result r;
  if (!io.open()) { r.status = "host-device-busy"; return r; }
  const Boot0::Facts f = io.facts(); r.commandBefore = f.command;
  bool testOK = false;
  do {
    if (const char *error = Boot0::preflight(f)) { r.status = error; break; }
    if (!io.selectMapper()) { r.status = "host-mapper-unresolved"; break; }
    if (!io.allocate()) { r.status = "host-allocation-failed"; break; }
    if (io.command() != 0) { r.status = "host-command-changed"; break; }
    if (!io.prepare()) { r.status = "host-prepare-failed"; break; }
    r.prepared = true;
    if (!io.segments(r.segments, r.count, r.end) || !Preparation::validSegments(r.segments, r.count, r.end)) {
      r.status = "host-segments-invalid"; break;
    }
    if (!io.mapHostPage(r.segments[0].address)) { r.status = "host-map-failed"; break; }
    if (io.command() != 0) { r.status = "host-command-changed"; break; }
    r.memoryAttempted = true; io.setMemory(true);
    if (io.command() != 2) { r.status = "host-memory-enable-failed"; break; }
    r.windowBefore = io.readWindow(); r.windowSecond = io.readWindow();
    // Require a stable, ordinary VIDMEM window; reject reserved/high bits.
    if (r.windowBefore != r.windowSecond || (r.windowBefore & 0xff000000U)) {
      r.status = "host-window-not-idle"; break;
    }
    r.windowProgrammed = CoherentTarget | unsigned(r.segments[0].address >> 16);
    r.windowAttempted = true; io.writeWindow(r.windowProgrammed);
    r.windowObserved = io.readWindow();
    if (r.windowObserved != r.windowProgrammed) { r.status = "host-window-write-failed"; break; }
    if (io.command() != 2) { r.status = "host-command-changed"; break; }
    r.masterAttempted = true; io.setMaster(true); r.commandEnabled = io.command();
    if (r.commandEnabled != 6) { r.status = "host-master-enable-failed"; break; }
    testOK = true;
    for (unsigned phase = 0; phase < 2 && testOK; ++phase) {
      if (!io.publish(phase)) { r.status = "host-publish-failed"; testOK = false; break; }
      // synchronize() must not have moved or replaced an IOVM segment.
      Preparation::Segment check[Preparation::DmaPages]; unsigned n = 0; Boot0::U64 end = 0;
      if (!io.segments(check, n, end) || !Preparation::validSegments(check, n, end)) {
        r.status = "host-segments-changed"; testOK = false; break;
      }
      for (unsigned i = 0; i < n; ++i) if (check[i].address != r.segments[i].address) testOK = false;
      if (!testOK) { r.status = "host-segments-changed"; break; }
      Phase &p = r.phase[phase];
      for (unsigned i = 0; i < Words; ++i) {
        if (!(i & 63) && (io.command() != 6 || io.readWindow() != r.windowProgrammed)) {
          r.status = "host-access-state-changed"; testOK = false; break;
        }
        const unsigned value = io.readHostWord(i); ++p.reads;
        if (!i) p.first = value;
        p.last = value; p.hash = hashWord(p.hash, value);
        if (value != pattern(i, phase)) {
          r.mismatchPhase = phase; r.mismatchWord = i; r.mismatchValue = value;
          r.status = "host-data-mismatch"; testOK = false; break;
        }
        ++p.matched;
      }
    }
    if (testOK) {
      r.contentsIntact = io.verifyPublished();
      testOK = r.contentsIntact;
      r.status = testOK ? "GPU-host-page-read-verified" : "host-CPU-buffer-changed";
    }
  } while (false);
  // PRAMIN reads are synchronous. No GPU memory writes or queued DMA commands.
  // Disable bus mastering and restore window before releasing the wired buffer.
  if (r.masterAttempted) io.setMaster(false);
  if (r.windowAttempted && io.command() == 2) {
    io.writeWindow(r.windowBefore); r.windowAfter = io.readWindow();
    r.windowRestored = r.windowAfter == r.windowBefore;
  }
  if (r.memoryAttempted) io.setMemory(false);
  r.commandAfter = io.command();
  io.unmapHostPage();
  if (r.masterAttempted && r.commandAfter != 0) r.resourcesRetained = true;
  if (r.windowAttempted && !r.windowRestored) r.resourcesRetained = true;
  if (!r.resourcesRetained) r.cleanupVerified = io.cleanup();
  if (r.resourcesRetained) r.status = "host-restore-failed-resources-retained";
  else if (!r.cleanupVerified) r.status = "host-cleanup-failed";
  else if (r.memoryAttempted && r.commandAfter != 0) r.status = "host-command-restore-failed";
  io.close();
  r.passed = testOK && r.windowRestored && r.cleanupVerified && r.commandBefore == 0 && r.commandAfter == 0;
  return r;
}
}
