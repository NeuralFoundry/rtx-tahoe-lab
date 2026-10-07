#include "driver/GSPBooterPreflight.hpp"
#include "driver/Boot0Protocol.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <initializer_list>

static unsigned checks = 0;
static void check(bool value, const char *what) {
  ++checks;
  if (!value) { std::fprintf(stderr, "FAIL %s\n", what); std::abort(); }
}
struct FuseIO {
  unsigned values[2] = {0, 0}, reads = 0, calls = 0, failCommandCall = 0;
  unsigned command() { return ++calls == failCommandCall ? 6 : 2; }
  unsigned readBooterFuse() {
    check(reads < 2, "bounded fuse reads");
    return values[reads++];
  }
};
struct FullIO {
  unsigned cmd = 0, fuse = 0, fuseReads = 0, opened = 0, closed = 0, mapped = 0, unmapped = 0;
  bool restore = true;
  bool open() { ++opened; return true; }
  void close() { ++closed; }
  Boot0::Facts facts() {
    Boot0::Facts f;
    f.identity = 0x252010de; f.subsystem = 0x104c1043; f.command = cmd;
    f.pmcsr = 0; f.link = 0x83; f.targetBDF = f.barTypesValid = true;
    f.bar0 = f.descriptor0 = 0xfb000000; f.length0 = 16ULL << 20;
    f.bar1 = f.descriptor1 = 0x824000000ULL; f.length1 = 64ULL << 20;
    return f;
  }
  bool mapPage() { ++mapped; return true; }
  void unmapPage() { ++unmapped; }
  unsigned command() { return cmd; }
  void setMemory(bool enabled) { if (enabled) cmd = 2; else if (restore) cmd = 0; }
  unsigned readBoot0() { return 0xb76000a1; }
  unsigned readBooterFuse() { ++fuseReads; return fuse; }
};
int main() {
  static_assert(GSPBooterPreflight::Offset == 0x824148, "Fixed SEC2 fuse only");
  for (unsigned value = 0; value < 256; ++value) {
    check(GSPBooterPreflight::select(value) == (value == 0 ? 1 : value == 1 ? 0 : -1), "signature version boundary");
  }
  for (unsigned value : {0xffffffffU, 0xbad00000U, 0xbad0fb00U, 0xbadf0000U, 0x80000000U, 0xfffffffeU})
    check(GSPBooterPreflight::select(value) == -1, "unavailable/sentinel version");
  for (unsigned value : {0U, 1U}) {
    FuseIO io; io.values[0] = io.values[1] = value;
    GSPBooterPreflight::Snapshot s;
    check(s(io) == nullptr && s.reads == 2 && s.index == (value ? 0 : 1), "stable supported fuse");
    check(std::strcmp(s.status, "gsp-booter-signature-candidate-selected") == 0, "success status");
  }
  for (unsigned fail = 1; fail <= 3; ++fail) {
    FuseIO io; io.failCommandCall = fail;
    GSPBooterPreflight::Snapshot s;
    check(s(io) != nullptr && s.index == -1 && s.reads == fail - 1, "command change before/during/after");
    check(std::strcmp(s.status, "gsp-booter-fuse-command-changed") == 0, "command error status");
  }
  for (unsigned sample = 0; sample < 2; ++sample) {
    for (unsigned bad : {0xffffffffU, 0xbad01234U, 0xbadf1234U}) {
      FuseIO io; io.values[sample] = bad;
      GSPBooterPreflight::Snapshot s;
      check(s(io) != nullptr && s.index == -1 && s.reads == sample + 1, "unreadable sample preserves observation");
      check(std::strcmp(s.status, "gsp-booter-fuse-unreadable") == 0, "unreadable status");
    }
  }
  {
    FuseIO io; io.values[1] = 1;
    GSPBooterPreflight::Snapshot s;
    check(s(io) != nullptr && s.index == -1 && s.reads == 2, "unstable fuse cannot select");
    check(std::strcmp(s.status, "gsp-booter-fuse-unstable") == 0, "unstable status");
  }
  {
    FuseIO io; io.values[0] = io.values[1] = 2;
    GSPBooterPreflight::Snapshot s;
    check(s(io) != nullptr && s.index == -1 && s.reads == 2, "unsupported stable version");
    check(std::strcmp(s.status, "gsp-booter-fuse-signature-unavailable") == 0, "unsupported status");
  }
  {
    GSPBooterPreflight::Snapshot s; FuseIO first, failed;
    check(s(first) == nullptr, "first snapshot valid"); failed.failCommandCall = 1;
    check(s(failed) != nullptr && s.reads == 0 && s.index == -1 && s.first == 0 && s.second == 0, "reused snapshot cannot replay success");
  }
  for (unsigned fuse : {0U, 1U, 2U, 0xffffffffU}) {
    FullIO io; io.fuse = fuse; GSPBooterPreflight::Snapshot s;
    const auto r = Boot0::run(io, s);
    check(r.passed == (fuse < 2) && r.restoreVerified && io.cmd == 0, "BOOT0 restores PCI on every fuse result");
    check(io.opened == 1 && io.closed == 1 && io.mapped == 1 && io.unmapped == 1, "BOOT0 resource lifecycle");
    check(io.fuseReads == (fuse == 0xffffffffU ? 1U : 2U), "BOOT0 nested read bound");
  }
  {
    FullIO io; io.restore = false; GSPBooterPreflight::Snapshot s;
    const auto r = Boot0::run(io, s);
    check(!r.passed && !r.restoreVerified && s.index == 1, "valid candidate never hides PCI restoration failure");
  }
  std::printf("GSP booter fuse: %u checks passed\n", checks);
}
