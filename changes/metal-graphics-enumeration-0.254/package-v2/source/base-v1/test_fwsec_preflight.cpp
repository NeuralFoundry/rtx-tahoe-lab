#include "driver/FWSECPreflight.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace FWSECPreflight;

struct PreflightIO {
  std::array<U32, Count> values{{0, 0, 0, 0, 0x10, 0x10, 0, 0x80420100, 1, 0}};
  std::array<unsigned, Count> seen{};
  std::vector<unsigned> order;
  unsigned commandReads = 0, commandFaultAt = 0;
  unsigned changedIndex = Count, changedSample = 0;
  U32 changedValue = 0;
  U32 command() { return ++commandReads == commandFaultAt ? 6U : 2U; }
  U32 readPreflight(unsigned index) {
    assert(index < Count);
    order.push_back(index);
    const unsigned sample = seen[index]++;
    return index == changedIndex && sample == changedSample ? changedValue : values[index];
  }
};

static unsigned cases = 0;
static void success(PreflightIO &io, Snapshot &s) {
  assert(s.capture(io, 6144) == nullptr);
  assert(std::strcmp(s.status, "fwsec-preflight-measured") == 0);
  assert(s.complete && s.layoutValid);
  assert(s.vramBytes == 0x180000000ULL);
  assert(s.frtsEnd - s.frtsOffset == 0x100000ULL);
  assert((s.frtsOffset & 0x1ffffULL) == 0);
  assert(s.frtsEnd <= s.workspaceBoundary && s.workspaceBoundary < s.vramBytes);
  assert(io.commandReads == io.order.size());
}

int main() {
  static_assert(sizeof(U32) == 4 && sizeof(U64) == 8, "protocol widths");
  const U32 expectedOffsets[] = {
    0x820c04, 0x625f04, 0x1fa824, 0x1fa828, 0x110100,
    0x111388, 0x1103c0, 0x110108, 0x111668, 0x1438
  };
  for (unsigned i = 0; i < Count; ++i) assert(Offsets[i] == expectedOffsets[i]);
  {
    PreflightIO io; Snapshot s;
    success(io, s);
    assert(s.displaySupported && !s.workspaceValid && !s.requiresRelocation);
    assert(s.workspaceAddress == 0 && s.workspaceBoundary == 0x17ff00000ULL);
    assert(s.frtsOffset == 0x17fe00000ULL && s.engineIdle && s.wprClear);
    for (unsigned i = 0; i < Count; ++i) {
      assert(s.reads[i] == 2 && io.order[2*i] == i && io.order[2*i+1] == i);
    }
    ++cases;
  }
  {
    PreflightIO io; Snapshot s;
    io.values[DisplayFuse] = 1; io.values[Workspace] = 0xffffffffU;
    success(io, s);
    assert(!s.displaySupported && !s.workspaceValid && !s.requiresRelocation);
    assert(s.reads[Workspace] == 0 && io.seen[Workspace] == 0 && io.order.size() == 18);
    assert(s.frtsOffset == 0x17fe00000ULL);
    ++cases;
  }
  // An invalid workspace field is ignored when STATUS is clear, even if its
  // address bits would describe an out-of-range address.
  {
    PreflightIO io; Snapshot s; io.values[Workspace] = 0xffffff00U;
    success(io, s); assert(!s.workspaceValid && s.frtsOffset == 0x17fe00000ULL);
    ++cases;
  }
  // Above 4 GiB address bits must survive the 64-bit conversion. Sweep all
  // 64 KiB workspace positions in the final MiB, including unaligned bounds.
  for (U64 address = 0x17ff00000ULL; address < 0x180000000ULL; address += 0x10000) {
    PreflightIO io; Snapshot s; io.values[Workspace] = U32(address >> 8) | 8;
    success(io, s);
    assert(s.workspaceValid && s.workspaceAddress == address);
    assert(s.workspaceBoundary == address && !s.requiresRelocation);
    assert(s.frtsEnd == (address & ~0x1ffffULL));
    ++cases;
  }
  for (U64 address : {0ULL, 0x10000ULL, 0x100000000ULL, 0x17fef0000ULL}) {
    PreflightIO io; Snapshot s; io.values[Workspace] = U32(address >> 8) | 8;
    success(io, s);
    assert(s.requiresRelocation && s.workspaceAddress == address);
    assert(s.workspaceBoundary == 0x17ffe0000ULL && s.frtsOffset == 0x17fee0000ULL);
    ++cases;
  }
  for (U64 address : {0x180000000ULL, 0x180010000ULL, 0xffffff0000ULL}) {
    PreflightIO io; Snapshot s; io.values[Workspace] = U32(address >> 8) | 8;
    assert(s.capture(io, 6144) != nullptr);
    assert(std::strcmp(s.status, "fwsec-preflight-layout-invalid") == 0);
    assert(s.complete && !s.layoutValid && s.frtsOffset == 0);
    assert(s.workspaceAddress == address && io.order.size() == 20);
    ++cases;
  }
  for (U32 mib : {0U, 1U, 4096U, 8192U, 0xffffffffU}) {
    PreflightIO io; Snapshot s;
    assert(s.capture(io, mib) != nullptr);
    assert(std::strcmp(s.status, "fwsec-preflight-vram-invalid") == 0);
    assert(s.vramBytes == U64(mib) * 1048576ULL);
    assert(s.complete && !s.layoutValid && s.frtsOffset == 0 && io.order.size() == 20);
    ++cases;
  }
  {
    PreflightIO io; Snapshot s;
    io.values[WprLo] = 0x017fe00f; io.values[WprHi] = 0x017ff00a;
    success(io, s);
    assert(s.wprLo == 0x17fe00000ULL && s.wprHi == 0x17ff00000ULL && !s.wprClear);
    ++cases;
  }
  {
    PreflightIO io; Snapshot s;
    io.values[WprLo] = 0x017fe000; io.values[WprHi] = 0xf;
    success(io, s); assert(s.wprClear && s.wprLo != 0 && s.wprHi == 0);
    ++cases;
  }
  for (unsigned index : {unsigned(CpuCtl), unsigned(RiscvCpu), unsigned(Engine), unsigned(Bcr)}) {
    PreflightIO io; Snapshot s;
    io.values[index] |= index == CpuCtl ? 2U : index == RiscvCpu ? 0x80U : index == Engine ? 1U : 0x10U;
    success(io, s); assert(!s.engineIdle); ++cases;
  }
  {
    PreflightIO io; Snapshot s; io.values[CpuCtl] = 0;
    success(io, s); assert(!s.engineIdle); ++cases;
  }
  {
    PreflightIO io; Snapshot s; io.values[FrtsStatus] = 0x00010000;
    success(io, s); assert(s.first[FrtsStatus] == 0x10000); ++cases;
  }
  // Every MMIO call is preceded by a PCI command check. A changed command
  // stops the stream before the next MMIO access, including second samples.
  for (unsigned call = 1; call <= 20; ++call) {
    PreflightIO io; Snapshot s; io.commandFaultAt = call;
    assert(s.capture(io, 6144) != nullptr);
    assert(std::strcmp(s.status, "fwsec-preflight-command-changed") == 0);
    assert(!s.complete && !s.layoutValid && io.order.size() == call - 1);
    assert(io.commandReads == call); ++cases;
  }
  for (unsigned index = 0; index < Count; ++index) {
    for (unsigned sample = 0; sample < 2; ++sample) {
      for (U32 bad : {0xffffffffU, 0xbadf0000U, 0xbad01234U}) {
        PreflightIO io; Snapshot s;
        io.changedIndex = index; io.changedSample = sample; io.changedValue = bad;
        assert(s.capture(io, 6144) != nullptr);
        assert(std::strcmp(s.status, "fwsec-preflight-register-unreadable") == 0);
        assert(!s.complete && !s.layoutValid && io.order.size() == index * 2 + sample + 1);
        assert(s.reads[index] == sample + 1);
        assert((sample ? s.second[index] : s.first[index]) == bad);
        for (unsigned later = index + 1; later < Count; ++later) assert(io.seen[later] == 0);
        ++cases;
      }
    }
    PreflightIO io; Snapshot s;
    io.changedIndex = index; io.changedSample = 1; io.changedValue = io.values[index] ^ 1U;
    assert(s.capture(io, 6144) != nullptr);
    assert(std::strcmp(s.status, "fwsec-preflight-register-unstable") == 0);
    assert(!s.complete && io.order.size() == index * 2 + 2);
    for (unsigned later = index + 1; later < Count; ++later) assert(io.seen[later] == 0);
    ++cases;
  }
  // Reusing a Snapshot after a successful capture cannot preserve readiness
  // or old data when the next capture fails before its first hardware read.
  {
    PreflightIO first; Snapshot s; success(first, s);
    PreflightIO second; second.commandFaultAt = 1;
    assert(s.capture(second, 6144) != nullptr);
    assert(!s.complete && !s.layoutValid && !s.engineIdle && !s.wprClear);
    assert(s.frtsOffset == 0 && s.workspaceBoundary == 0 && s.reads[DisplayFuse] == 0);
    ++cases;
  }
  std::printf("FWSEC preflight: %u cases passed\n", cases);
}
