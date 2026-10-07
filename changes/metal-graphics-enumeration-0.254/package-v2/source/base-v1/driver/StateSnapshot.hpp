#pragma once
#include "Boot0Protocol.hpp"

// Fixed GA106 early-init reads. References and interpretation: STATE-EXPERIMENT.md.
// No arbitrary addresses, polling, writes, reset or firmware execution.
namespace GPUState {
constexpr unsigned Count = 5;
constexpr unsigned Offsets[Count] = {0x00000a00, 0x001fa828, 0x00118128, 0x00118234, 0x001183a4};
constexpr unsigned Pages[3] = {0x00000000, 0x00118000, 0x001fa000};
enum Register { Boot42 = 0, Wpr2Hi = 1, Scratch05Protection = 2, Scratch05 = 3, VramMiB = 4 };

inline bool rejected(unsigned value) {
  return value == 0xffffffffU || (value & 0xffff0000U) == 0xbadf0000U;
}

struct Snapshot {
  unsigned first[Count] = {}, second[Count] = {}, reads[Count] = {};
  bool complete = false;
  template <class Transport> const char *operator()(Transport &io) {
    for (unsigned i = 0; i < Count; ++i) {
      // The upstream reset check short-circuits this protected read as well.
      if (i == Scratch05 && !(first[Scratch05Protection] & 1)) continue;
      if (io.command() != 2) return "state-command-changed";
      first[i] = io.readFixed(i); reads[i] = 1;
      if (rejected(first[i])) return "state-register-unavailable";
      if (io.command() != 2) return "state-command-changed";
      second[i] = io.readFixed(i); reads[i] = 2;
      if (first[i] != second[i]) return "state-register-unstable";
      if (i == Boot42 && ((first[i] >> 20) & 0x3ff) != 0x176) return "state-BOOT42-mismatch";
      if (i == VramMiB && (first[i] < 1024 || first[i] > 65536)) return "state-VRAM-out-of-range";
    }
    complete = true;
    return nullptr;
  }
};
}
