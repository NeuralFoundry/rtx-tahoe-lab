#pragma once
#include "Boot0Protocol.hpp"

namespace Preparation {
constexpr unsigned RomOffset = 0x300000, RomSize = 1U << 20, DmaSize = 16384;
constexpr unsigned PageSize = 4096, DmaPages = DmaSize / PageSize;

struct RomResult {
  const char *status = "not-run";
  unsigned wordsRead = 0, bytesCaptured = 0;
  bool stable = false;
};

template <class IO> const char *captureRom(IO &io, RomResult &r) {
  r.status = "rom-command-changed";
  if (io.command() != 2) return r.status;
  const unsigned first = io.romWord(0); ++r.wordsRead;
  const unsigned signature = first & 0xffff;
  if (signature != 0xaa55 && signature != 0x4e56 && signature != 0xbb77)
    return r.status = "rom-signature-unavailable";
  if (!io.allocateRom()) return r.status = "rom-buffer-allocation-failed";
  // Fixed ROM aperture; two passes, never an address supplied by the ROM.
  for (unsigned pass = 0; pass < 2; ++pass) {
    for (unsigned off = 0; off < RomSize; off += 4) {
      if (!(off & (PageSize - 1)) && io.command() != 2) return r.status = "rom-command-changed";
      const unsigned value = io.romWord(off); ++r.wordsRead;
      if (pass == 0) { io.storeRom(off, value); r.bytesCaptured += 4; }
      else if (io.savedRomWord(off) != value) return r.status = "rom-unstable";
    }
  }
  if (io.command() != 2) return r.status = "rom-command-changed";
  r.stable = true;
  r.status = "rom-captured";
  return nullptr;
}

struct Segment { Boot0::U64 address = 0, length = 0; };
inline bool validSegments(const Segment *s, unsigned count, Boot0::U64 end) {
  if (count != DmaPages || end != DmaSize) return false;
  for (unsigned i = 0; i < count; ++i) {
    if (!s[i].address || (s[i].address & (PageSize - 1)) || s[i].length != PageSize ||
        s[i].address > (1ULL << 40) - PageSize) return false;
    for (unsigned j = 0; j < i; ++j) if (s[j].address == s[i].address) return false;
  }
  return true;
}

struct DmaResult {
  const char *status = "not-run";
  Segment segments[DmaPages];
  unsigned count = 0, commandBefore = 0xffff, commandAfter = 0xffff;
  Boot0::U64 end = 0;
  bool prepared = false, cpuContentsIntact = false, cleanupVerified = false, passed = false;
};

// OS mapping lifecycle only. The adapter deliberately has no PCI write or GPU DMA method.
template <class IO> DmaResult prepareDma(IO &io) {
  DmaResult r;
  if (!io.open()) { r.status = "dma-device-busy"; return r; }
  const Boot0::Facts f = io.facts(); r.commandBefore = f.command;
  do {
    if (const char *error = Boot0::preflight(f)) { r.status = error; break; }
    if (!io.selectMapper()) { r.status = "dma-mapper-unresolved"; break; }
    if (!io.allocate()) { r.status = "dma-allocation-failed"; break; }
    if (io.command() != 0) { r.status = "dma-command-changed"; break; }
    if (!io.prepare()) { r.status = "dma-prepare-failed"; break; }
    r.prepared = true;
    if (!io.segments(r.segments, r.count, r.end)) { r.status = "dma-segment-generation-failed"; break; }
    if (!validSegments(r.segments, r.count, r.end)) { r.status = "dma-segment-invalid"; break; }
    r.cpuContentsIntact = io.verifyCPU();
    if (!r.cpuContentsIntact) { r.status = "dma-CPU-buffer-changed"; break; }
    r.status = "dma-mapping-prepared";
  } while (false);
  r.cleanupVerified = io.cleanup();
  r.commandAfter = io.command();
  if (!r.cleanupVerified) r.status = "dma-cleanup-failed";
  else if (r.commandAfter != r.commandBefore || r.commandAfter != 0) r.status = "dma-command-changed";
  io.close();
  r.passed = r.prepared && r.cpuContentsIntact && validSegments(r.segments, r.count, r.end) &&
    r.cleanupVerified && r.commandBefore == 0 && r.commandAfter == 0;
  return r;
}
}
