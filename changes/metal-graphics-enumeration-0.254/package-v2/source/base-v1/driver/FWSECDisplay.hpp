#pragma once
#include "FWSECPreflight.hpp"

namespace FWSECDisplay {
using U32 = unsigned;
enum Register : unsigned { Mask, HeadCount, Head0, Head1, Head2, Head3, Count };
static constexpr U32 Offsets[Count] = {
  0x610060, 0x610074, 0x612078, 0x612878, 0x613078, 0x613878
};

struct Snapshot {
  U32 first[Count] = {}, second[Count] = {}, reads[Count] = {};
  bool complete = false, idle = false;
  U32 mask = 0, count = 0;
  const char *status = "not-run";

  template <class IO> const char *capture(IO &io, U32 expectedCommand = 2) {
    *this = Snapshot{};
    if (expectedCommand != 2 && expectedCommand != 6) {
      status = "fwsec-display-command-invalid"; return status;
    }
    bool sleeping = true;
    for (unsigned index = 0; index < Count; ++index) {
      if (index >= Head0 && !(mask & (1U << (index - Head0)))) continue;
      for (unsigned sample = 0; sample < 2; ++sample) {
        if (io.command() != expectedCommand) {
          status = "fwsec-display-command-changed"; return status;
        }
        U32 &value = sample ? second[index] : first[index];
        value = io.readDisplay(index); ++reads[index];
        if (!FWSECPreflight::readable(value)) {
          status = "fwsec-display-register-unreadable"; return status;
        }
      }
      if (first[index] != second[index]) {
        status = "fwsec-display-register-unstable"; return status;
      }
      if (index == Mask) mask = first[index] & 0xffU;
      if (index == HeadCount) {
        count = first[index] & 0xfU;
        // Count is the hardware head index limit; mask need not be contiguous
        // or have count set bits. This experiment supports at most four heads.
        if (!count || count > 4 || (mask & ~((1U << count) - 1U))) {
          status = "fwsec-display-topology-invalid"; return status;
        }
      }
      // NVIDIA treats both SNOOZE (1) and AWAKE (2) as active. Require SLEEP (0).
      if (index >= Head0 && (first[index] & 0x300U)) sleeping = false;
    }
    complete = true; idle = sleeping;
    status = "fwsec-display-measured";
    return nullptr;
  }
};

// Recheck actual read evidence at the reservation boundary, independently of
// exported convenience flags. This is not a claim of global hardware ownership.
inline bool validIdleEvidence(const Snapshot &s) {
  if (!s.complete || !s.idle || !s.count || s.count > 4 ||
      s.mask != (s.first[Mask] & 0xffU) || s.count != (s.first[HeadCount] & 0xfU) ||
      (s.mask & ~((1U << s.count) - 1U))) return false;
  for (unsigned index = 0; index < Count; ++index) {
    const bool eligible = index < Head0 || (s.mask & (1U << (index - Head0)));
    if (!eligible) {
      if (s.reads[index] || s.first[index] || s.second[index]) return false;
      continue;
    }
    if (s.reads[index] != 2 || s.first[index] != s.second[index] ||
        !FWSECPreflight::readable(s.first[index])) return false;
    if (index >= Head0 && (s.first[index] & 0x300U)) return false;
  }
  return true;
}
} // namespace FWSECDisplay
