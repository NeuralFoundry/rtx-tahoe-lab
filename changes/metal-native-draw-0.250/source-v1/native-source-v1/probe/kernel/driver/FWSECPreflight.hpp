#pragma once

// Read-only register measurements and layout arithmetic. This deliberately
// does not reserve VRAM, relocate the VGA workspace, or start firmware.
namespace FWSECPreflight {
using U32 = unsigned;
using U64 = unsigned long long;
enum Register : unsigned {
  DisplayFuse, Workspace, WprLo, WprHi, CpuCtl, RiscvCpu, Engine,
  Hwcfg, Bcr, FrtsStatus, Count
};
static constexpr U32 Offsets[Count] = {
  0x820c04, 0x625f04, 0x1fa824, 0x1fa828, 0x110100,
  0x111388, 0x1103c0, 0x110108, 0x111668, 0x1438
};

inline bool readable(U32 value) {
  return value != 0xffffffffU && (value >> 16) != 0xbadfU &&
         (value >> 16) != 0xbad0U;
}

struct Snapshot {
  U32 first[Count] = {}, second[Count] = {}, reads[Count] = {};
  const char *status = "not-run";
  bool complete = false, displaySupported = false, workspaceValid = false;
  bool requiresRelocation = false, layoutValid = false;
  bool engineIdle = false, wprClear = false;
  U64 vramBytes = 0, workspaceAddress = 0, workspaceBoundary = 0;
  U64 frtsOffset = 0, frtsEnd = 0, wprLo = 0, wprHi = 0;

  template <class IO> const char *capture(IO &io, U32 vramMiB) {
    *this = Snapshot{};
    vramBytes = U64(vramMiB) << 20;
    for (unsigned index = 0; index < Count; ++index) {
      // NVIDIA's display fuse is inverted: DATA=0 means supported.
      // An absent display block is never probed through its workspace port.
      if (index == Workspace && !displaySupported) continue;
      for (unsigned sample = 0; sample < 2; ++sample) {
        if (io.command() != 2) {
          status = "fwsec-preflight-command-changed";
          return status;
        }
        U32 &value = sample ? second[index] : first[index];
        value = io.readPreflight(index);
        ++reads[index];
        if (!readable(value)) {
          status = "fwsec-preflight-register-unreadable";
          return status;
        }
      }
      if (first[index] != second[index]) {
        status = "fwsec-preflight-register-unstable";
        return status;
      }
      if (index == DisplayFuse) displaySupported = !(first[index] & 1U);
    }
    complete = true;

    // WPR registers expose a page number in bits 31:4, in 4 KiB units.
    wprLo = U64(first[WprLo] >> 4) << 12;
    wprHi = U64(first[WprHi] >> 4) << 12;
    wprClear = (wprHi == 0);
    engineIdle = (first[CpuCtl] & 0x10U) && !(first[CpuCtl] & 2U) &&
                 !(first[RiscvCpu] & 0x80U) && !(first[Engine] & 1U) &&
                 !(first[Bcr] & 0x10U);
    workspaceValid = displaySupported && (first[Workspace] & 8U);
    if (workspaceValid) workspaceAddress = U64(first[Workspace] >> 8) << 16;

    // This protocol supports only the board already measured at 6144 MiB.
    // Convert before shifting so unexpected large inputs cannot wrap at 32 bits.
    if (vramMiB != 6144U) {
      status = "fwsec-preflight-vram-invalid";
      return status;
    }
    constexpr U64 MiB = 0x100000ULL, Alignment = 0x20000ULL;
    const U64 fallback = vramBytes - MiB;
    workspaceBoundary = fallback;
    if (workspaceValid) {
      if (workspaceAddress >= vramBytes) {
        status = "fwsec-preflight-layout-invalid";
        return status;
      }
      if (workspaceAddress < fallback) {
        // Reference drivers PLAN to move a low VGA workspace to this boundary.
        // The measured workspaceAddress remains the actual register address.
        workspaceBoundary = vramBytes - Alignment;
        requiresRelocation = true;
      } else {
        workspaceBoundary = workspaceAddress;
      }
    }
    frtsEnd = workspaceBoundary & ~(Alignment - 1);
    if (frtsEnd < MiB || frtsEnd > vramBytes) {
      status = "fwsec-preflight-layout-invalid";
      return status;
    }
    frtsOffset = frtsEnd - MiB;
    layoutValid = true;
    status = "fwsec-preflight-measured";
    return nullptr;
  }
};
} // namespace FWSECPreflight
