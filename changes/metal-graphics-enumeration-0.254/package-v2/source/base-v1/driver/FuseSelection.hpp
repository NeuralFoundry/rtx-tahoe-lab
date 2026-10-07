#pragma once
namespace FWSECFuse {
constexpr unsigned Offset = 0x8241e0, Mask = 7, Count = 3;
// Nouveau ga102_gsp_fwsec_signature: BIT(fls(raw)), then rank in mask.
// Fixed to the already extracted board descriptor: engine 0x400, ucode 9.
inline int select(unsigned raw) {
  if (raw == 0xffffffffU || (raw & 0xffff0000U) == 0xbadf0000U) return -1;
  unsigned bit = 0;
  for (unsigned v = raw; v; v >>= 1) ++bit;
  if (bit >= 32 || !(Mask & (1U << bit))) return -1;
  unsigned index = 0;
  for (unsigned i = 0; i < bit; ++i) if (Mask & (1U << i)) ++index;
  return index < Count ? int(index) : -1;
}
struct Snapshot {
  unsigned first = 0, second = 0, reads = 0;
  int index = -1;
  const char *status = "not-run";
  template <class IO> const char *operator()(IO &io) {
    if (io.command() != 2) return status = "fuse-command-changed";
    first = io.readFuse(); ++reads;
    if (first == 0xffffffffU || (first & 0xffff0000U) == 0xbadf0000U)
      return status = "fuse-unreadable";
    second = io.readFuse(); ++reads;
    if (first != second) return status = "fuse-unstable";
    if (io.command() != 2) return status = "fuse-command-changed";
    index = select(first);
    if (index < 0) return status = "fuse-signature-unavailable";
    status = "fuse-signature-candidate-selected";
    return nullptr;
  }
};
}
