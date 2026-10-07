#pragma once
#include "FWSECPreflight.hpp"
#include "FWSECDisplay.hpp"

// A software exclusion ledger belonging to one persistent IOPCIDevice owner.
// There is no public/general VRAM allocator and no claim of a hardware lock.
namespace FWSECRegion {
using U32 = unsigned;
using U64 = unsigned long long;
static constexpr U64 Offset = 0x17fe00000ULL, Size = 0x100000ULL;
static constexpr U64 BiosStart = 0x17ff00000ULL, VramBytes = 0x180000000ULL;
static_assert(Offset + Size == BiosStart && BiosStart + Size == VramBytes,
              "FRTS and the excluded BIOS MiB must be adjacent and disjoint");

inline bool validBoardEvidence(const FWSECPreflight::Snapshot &s) {
  if (!s.complete || !s.layoutValid || !s.engineIdle || !s.wprClear ||
      !s.displaySupported || s.workspaceValid || s.requiresRelocation ||
      s.vramBytes != VramBytes || s.workspaceAddress != 0 ||
      s.workspaceBoundary != BiosStart || s.frtsOffset != Offset ||
      s.frtsEnd != BiosStart || s.wprLo != 0x1ffffe0000ULL || s.wprHi != 0) return false;
  // Strict current-board baseline. Disabled WPR2's large LO sentinel is valid
  // only while HI is zero; it is not a claimed VRAM range.
  static constexpr U32 baseline[FWSECPreflight::Count] = {
    0, 1, 0x1ffffe00, 0, 0x10, 0x10, 0, 0x80420100, 1, 0
  };
  for (unsigned index = 0; index < FWSECPreflight::Count; ++index) {
    if (s.reads[index] != 2 || s.first[index] != s.second[index] ||
        !FWSECPreflight::readable(s.first[index]) || s.first[index] != baseline[index]) return false;
  }
  return true;
}

class Ledger {
  bool active_ = false, started_ = false, used_ = false;
  U32 displayMask_ = 0, displayCount_ = 0;
public:
  bool claim(const FWSECPreflight::Snapshot &s, const FWSECDisplay::Snapshot &display,
             bool exclusiveOpen) {
    if (used_ || active_ || started_ || !exclusiveOpen || !validBoardEvidence(s) ||
        !FWSECDisplay::validIdleEvidence(display)) return false;
    active_ = used_ = true;
    displayMask_ = display.mask; displayCount_ = display.count;
    return true;
  }
  bool matches(const FWSECPreflight::Snapshot &s, const FWSECDisplay::Snapshot &display,
               bool exclusiveOpen) const {
    return active_ && exclusiveOpen && validBoardEvidence(s) &&
           FWSECDisplay::validIdleEvidence(display) &&
           display.mask == displayMask_ && display.count == displayCount_;
  }
  void markStartAttempted() { if (active_) started_ = true; }
  bool releaseBeforeStart() {
    if (!active_ || started_) return false;
    active_ = false;
    return true;
  }
  bool owned() const { return active_; }
  bool persistent() const { return started_; }
};
} // namespace FWSECRegion
