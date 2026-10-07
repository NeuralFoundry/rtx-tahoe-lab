#pragma once
#include "Boot0Protocol.hpp"
#include "FWSECRegion.hpp"
#include "GSPDmaProtocol.hpp"

// Portable ownership bookkeeping only. No MMIO, allocation, DMA or CPU START.
// Caller supplies fresh evidence under one exclusive IOPCIDevice owner. Flags
// in this model are not hardware measurements and cannot replace that evidence.
namespace GSPLaunchOwnership {
using U32 = unsigned;
using U64 = unsigned long long;
using Byte = unsigned char;
constexpr U64 MiB = 0x100000ULL, VramBytes = 0x180000000ULL;
constexpr U64 ReservedStart = 0x173e00000ULL, ReservedEnd = 0x17ff00000ULL;
constexpr U64 ReservedBytes = ReservedEnd - ReservedStart;
constexpr U64 BiosStart = ReservedEnd, BiosEnd = VramBytes;
constexpr U64 ImageBytes = 63541248ULL, BootloaderBytes = 24576ULL;
static_assert(ReservedBytes == 193 * MiB && BiosEnd - BiosStart == MiB,
              "Fixed board reservation must preserve the BIOS MiB");

enum Region : U32 { NonWprHeap, Metadata, GspHeap, Image, Bootloader, Frts, Bios, RegionCount };
struct Range { U64 offset = 0, size = 0; };
struct Layout { Range regions[RegionCount]; };
// These are the exact output of the pinned plan_vram profile, including its
// alignment gaps. The 129 MiB heap is a reference profile, not a tested minimum.
constexpr Layout ExpectedLayout = {{
  {6239027200ULL, MiB}, {6240075776ULL, 4096},
  {6241124352ULL, 135266304ULL}, {6376783872ULL, ImageBytes},
  {6440329216ULL, BootloaderBytes}, {6440353792ULL, MiB}, {BiosStart, MiB}
}};
inline bool validLayout(const Layout &layout) {
  for (U32 i = 0; i < RegionCount; ++i) {
    const Range &r = layout.regions[i];
    if (!r.size || r.offset >= VramBytes || r.size > VramBytes - r.offset ||
        r.offset != ExpectedLayout.regions[i].offset || r.size != ExpectedLayout.regions[i].size)
      return false;
    if (i && layout.regions[i - 1].offset + layout.regions[i - 1].size > r.offset) return false;
  }
  return true;
}
inline bool containsOwned(U64 offset, U64 length) {
  return length && offset >= ReservedStart && offset < ReservedEnd && length <= ReservedEnd - offset;
}

// Stage-only reset/drain proof. The caller must fill this from a new observation
// after the latest submit attempt, not from a historical successful Result.
struct Quiescence {
  U64 generation = 0, exposure = 0;
  U32 command = 0xffffffffU;
  bool engineReset = false, dmaIdle = false, pciTransactionsDrained = false;
  bool targetsCleared = false, cpuStopped = false, coreSelectionVerified = false;
  bool hostBuffersIntact = false, commandStable = false, providerHeld = false;
  bool complete() const {
    return command == 0 && engineReset && dmaIdle && pciTransactionsDrained &&
      targetsCleared && cpuStopped && coreSelectionVerified && hostBuffersIntact &&
      commandStable && providerHeld;
  }
};

class Ledger {
  bool used_ = false, owned_ = false, frozen_ = false, exposed_ = false;
  bool started_ = false, quiescent_ = false, cleanupFailed_ = false;
  U64 generation_ = 0, exposure_ = 0;
  U32 headMask_ = 0, headCount_ = 0;
public:
  bool claim(const Boot0::Facts &identity, const FWSECPreflight::Snapshot &board,
             const FWSECDisplay::Snapshot &display, U64 generation,
             bool exclusiveOpen, const Layout &layout = ExpectedLayout) {
    if (used_ || !generation || !exclusiveOpen || Boot0::preflight(identity) ||
        !FWSECRegion::validBoardEvidence(board) || !FWSECDisplay::validIdleEvidence(display) ||
        !validLayout(layout)) return false;
    used_ = owned_ = true; generation_ = generation;
    headMask_ = display.mask; headCount_ = display.count;
    return true;
  }
  bool matchesBeforeDeviceUse(const Boot0::Facts &identity, const FWSECPreflight::Snapshot &board,
                             const FWSECDisplay::Snapshot &display, U64 generation,
                             bool exclusiveOpen) const {
    return owned_ && !exposed_ && !started_ && !cleanupFailed_ && generation == generation_ &&
      exclusiveOpen && !Boot0::preflight(identity) && FWSECRegion::validBoardEvidence(board) &&
      FWSECDisplay::validIdleEvidence(display) && display.mask == headMask_ && display.count == headCount_;
  }
  bool freezeUploads(U64 generation) {
    if (!canMutateUploads() || generation != generation_) return false;
    frozen_ = true; return true;
  }
  // Must precede the first potentially effective SEC2 DMA submit/target change.
  // Even an invalid request while owned is conservatively treated as exposure;
  // a false return never grants permission to write a register.
  bool noteDeviceExposureAttempted(U64 generation) {
    if (!owned_) return false;
    const bool allowed = generation == generation_ && frozen_ && !started_ && !cleanupFailed_ &&
      exposure_ != ~U64(0);
    frozen_ = exposed_ = true; quiescent_ = false;
    if (exposure_ != ~U64(0)) ++exposure_;
    else cleanupFailed_ = true;
    return allowed;
  }
  // Irreversible in this implementation. Mark BEFORE any CPU START write,
  // including one that later returns failure or has an unreadable readback.
  bool noteStartAttempted(U64 generation) {
    if (!owned_) return false;
    const bool allowed = generation == generation_ && frozen_ && !started_ && !cleanupFailed_;
    frozen_ = exposed_ = started_ = true; quiescent_ = false;
    return allowed;
  }
  bool noteQuiescence(const Quiescence &proof) {
    if (!owned_ || !exposed_) return false;
    quiescent_ = false;
    if (started_ || cleanupFailed_ || proof.generation != generation_ ||
        proof.exposure != exposure_ || !proof.complete()) return false;
    quiescent_ = true; return true;
  }
  void noteCleanupFailure() { if (owned_) cleanupFailed_ = true; }
  bool canReleaseHost() const {
    return owned_ && !started_ && !cleanupFailed_ && (!exposed_ || quiescent_);
  }
  bool canReleaseRegion() const { return canReleaseHost(); }
  bool canMutateUploads() const { return owned_ && !frozen_ && !exposed_ && !started_ && !cleanupFailed_; }
  bool requiresPin() const { return owned_ && (started_ || cleanupFailed_ || (exposed_ && !quiescent_)); }
  bool releaseBeforeStart(U64 generation, bool nativeCleanupVerified) {
    if (!owned_ || generation != generation_ || !canReleaseRegion()) return false;
    if (!nativeCleanupVerified) { cleanupFailed_ = true; return false; }
    owned_ = false; return true;
  }
  bool owned() const { return owned_; }
  bool startAttempted() const { return started_; }
  bool exposed() const { return exposed_; }
  bool uploadsFrozen() const { return frozen_; }
  U64 generation() const { return generation_; }
  U64 exposure() const { return exposure_; }
};

// Bounded building blocks for the later native content validator. Caller must
// independently establish current IODMACommand ownership and global nonaliasing
// with GSPDmaProtocol::validatePages before using these local checks.
inline U32 firstPage(U32 resource) {
  U32 index = 0;
  for (U32 r = 0; r < resource && r < GSPDmaProtocol::ResourceCount; ++r)
    index += GSPDmaProtocol::Pages[r];
  return index;
}
inline bool validPage(U64 value) {
  return value >= GSPDmaProtocol::Page && value <= GSPDmaProtocol::AddressLimit - GSPDmaProtocol::Page &&
    !(value & (GSPDmaProtocol::Page - 1));
}
inline bool validateDirectPointer(const U64 *currentPages, U32 pageCount, U32 resource,
                                  U64 logicalOffset, U64 address, U64 length) {
  using namespace GSPDmaProtocol;
  if (!currentPages || pageCount != TotalPages || resource >= ResourceCount || !length ||
      logicalOffset >= Sizes[resource] || length > Sizes[resource] - logicalOffset ||
      !address || address >= AddressLimit || length > AddressLimit - address) return false;
  const U32 first = firstPage(resource) + U32(logicalOffset / Page);
  const U32 last = firstPage(resource) + U32((logicalOffset + length - 1) / Page);
  if (!validPage(currentPages[first]) || address != currentPages[first] + logicalOffset % Page) return false;
  for (U32 i = first + 1; i <= last; ++i)
    if (!validPage(currentPages[i]) || currentPages[i] != currentPages[i - 1] + Page) return false;
  return true;
}
inline U64 readLE64(const Byte *data) {
  U64 value = 0;
  for (U32 i = 0; i < 8; ++i) value |= U64(data[i]) << (i * 8);
  return value;
}
inline bool validatePageTable(const Byte *data, U64 dataBytes, U64 tableOffset,
                              const U64 *expectedPages, U32 pageCount) {
  if (!data || !expectedPages || !pageCount || pageCount > GSPDmaProtocol::TotalPages ||
      dataBytes > GSPDmaProtocol::TotalBytes || tableOffset > dataBytes ||
      U64(pageCount) * 8 > dataBytes - tableOffset) return false;
  for (U32 i = 0; i < pageCount; ++i)
    if (!validPage(expectedPages[i]) || readLE64(data + tableOffset + U64(i) * 8) != expectedPages[i]) return false;
  return true;
}

// Explicit contract for a future complete validator. This module does NOT
// fill this evidence or validate full firmware/buffer contents. In particular,
// the two pointer helpers above cannot produce a complete content seal.
struct ContentSeal {
  U64 generation = 0;
  U32 structuralMask = 0, firmwareMask = 0;
  bool globalPagesValidated = false, uploadsSynchronized = false, readbacksVerified = false;
  static constexpr U32 AllStructures = (1U << GSPDmaProtocol::ResourceCount) - 1;
  static constexpr U32 FirmwareResources = (1U << GSPDmaProtocol::Radix3) |
    (1U << GSPDmaProtocol::Bootloader) | (1U << GSPDmaProtocol::Signature) | (1U << GSPDmaProtocol::BooterLoad);
  bool completeFor(U64 currentGeneration) const {
    return currentGeneration && generation == currentGeneration && structuralMask == AllStructures &&
      firmwareMask == FirmwareResources && globalPagesValidated && uploadsSynchronized && readbacksVerified;
  }
};
} // namespace GSPLaunchOwnership
