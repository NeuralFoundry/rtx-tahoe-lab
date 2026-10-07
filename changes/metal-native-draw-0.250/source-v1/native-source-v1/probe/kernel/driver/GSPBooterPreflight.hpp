#pragma once

// Fixed GA10x 570.144 booter: metadata engine=1, ucode=3, fuseVersion=1,
// two 384-byte production signatures. No GSP/FWSEC fuse is interchangeable.
// https://github.com/torvalds/linux/blob/v6.15/drivers/gpu/drm/nouveau/nvkm/falcon/ga100.c
// ga100_flcn_fw_signature: SEC2 base 0x824140 + (ucode - 1) * 4.
namespace GSPBooterPreflight {
constexpr unsigned Offset = 0x824148, SignatureCount = 2, FuseVersion = 1;
inline bool readable(unsigned raw) {
  return raw != 0xffffffffU && (raw >> 16) != 0xbad0U && (raw >> 16) != 0xbadfU;
}
inline int select(unsigned raw) {
  // raw == 0 selects the last signature. Otherwise the highest set bit's
  // one-based index is subtracted from metadata fuseVersion. With version=1,
  // only raw==1 is supported; every other value requires unavailable material.
  if (!readable(raw)) return -1;
  if (raw == 0) return 1;
  return raw == 1 ? 0 : -1;
}
struct Snapshot {
  unsigned first = 0, second = 0, reads = 0;
  int index = -1;
  const char *status = "not-run";
  template<class IO> const char *operator()(IO &io) {
    *this = Snapshot{};
    for (unsigned sample = 0; sample < 2; ++sample) {
      if (io.command() != 2) return status = "gsp-booter-fuse-command-changed";
      unsigned &value = sample ? second : first;
      value = io.readBooterFuse(); ++reads;
      if (!readable(value)) return status = "gsp-booter-fuse-unreadable";
    }
    if (first != second) return status = "gsp-booter-fuse-unstable";
    if (io.command() != 2) return status = "gsp-booter-fuse-command-changed";
    index = select(first);
    if (index < 0) return status = "gsp-booter-fuse-signature-unavailable";
    status = "gsp-booter-signature-candidate-selected";
    return nullptr;
  }
};
}
