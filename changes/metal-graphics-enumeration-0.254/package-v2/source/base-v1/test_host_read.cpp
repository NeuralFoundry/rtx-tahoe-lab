#include "driver/HostReadProtocol.hpp"
#include "driver/FuseSelection.hpp"
#include <cassert>
#include <cstdio>
#include <vector>

struct HostIO {
  Boot0::Facts f;
  int fail = -1;
  unsigned cmd = 0, window = 0x1234, windowReads = 0, phase = 0, gpuReads = 0, generations = 0;
  bool opened = false, prepared = false, mapped = false, cleaned = false;
  std::vector<unsigned> memory;
  static constexpr Boot0::U64 Address = 0x437af2000ULL;
  HostIO() {
    f.identity = 0x252010de; f.subsystem = 0x104c1043; f.targetBDF = f.barTypesValid = true;
    f.command = 0; f.pmcsr = 8; f.link = 0x1083;
    f.bar0 = f.descriptor0 = 0xfb000000; f.length0 = 16ULL << 20;
    f.bar1 = f.descriptor1 = 0x824000000; f.length1 = 64ULL << 20;
  }
  bool open() { return opened = fail != 25; }
  void close() { assert(opened && !mapped); opened = false; }
  Boot0::Facts facts() {
    if (fail == 23) f.command = 4;
    if (fail == 24) f.identity = 0xffffffff;
    return f;
  }
  unsigned command() { return cmd; }
  bool selectMapper() { return fail != 0; }
  bool allocate() { memory.resize(4096); return fail != 1; }
  bool prepare() { return prepared = fail != 2; }
  bool segments(Preparation::Segment *s, unsigned &count, Boot0::U64 &end) {
    assert(prepared); ++generations; count = 4; end = 16384;
    for (unsigned i = 0; i < count; ++i) { s[i].address = Address + i * 8192; s[i].length = 4096; }
    if (fail == 4) s[0].address = 1ULL << 40;
    if (fail == 13 && generations > 1) s[0].address += 65536;
    return fail != 3;
  }
  bool mapHostPage(Boot0::U64 addr) { assert(addr == Address && cmd == 0); return mapped = fail != 5; }
  void unmapHostPage() { mapped = false; }
  void setMemory(bool value) { if (value && fail == 6) return; cmd = value ? cmd | 2 : cmd & ~2U; }
  void setMaster(bool value) {
    if ((value && fail == 10) || (!value && fail == 18)) return;
    cmd = value ? cmd | 4 : cmd & ~4U;
  }
  unsigned readWindow() {
    assert(mapped && (cmd & 2)); ++windowReads;
    if (fail == 7 && windowReads == 2) return window ^ 1;
    if (fail == 8 && windowReads <= 2) return 0x02001234;
    if (fail == 21 && gpuReads == 64 && window != 0x1234) return window ^ 1;
    return window;
  }
  void writeWindow(unsigned value) {
    assert(mapped && (cmd & 2));
    if ((fail == 9 && value != 0x1234) || (fail == 19 && value == 0x1234)) return;
    window = value;
  }
  bool publish(unsigned p) {
    assert(prepared && cmd == 6); phase = p;
    for (unsigned i = 0; i < memory.size(); ++i) memory[i] = HostRead::pattern(i, i < 1024 ? p : 0);
    return !(fail == 11 && p == 0) && !(fail == 12 && p == 1);
  }
  unsigned readHostWord(unsigned i) {
    assert(mapped && prepared && cmd == 6 && i < 1024);
    // Model GPU target decoding independently; wrong target/address cannot pass.
    assert((window >> 24) == 2);
    const Boot0::U64 physical = (Boot0::U64(window & 0xffffff) << 16) + (Address & 0xffff) + i * 4;
    assert(physical == Address + i * 4);
    ++gpuReads;
    if (fail == 22 && gpuReads == 64) cmd = 2;
    return memory.at(i) ^ (((fail == 14 && i == 0) || (fail == 15 && i == 1023) || (fail == 16 && phase == 1)) ? 1U : 0U);
  }
  bool verifyPublished() { return fail != 17; }
  bool cleanup() {
    // A failed restore must retain the DMA mapping instead of freeing it.
    assert(!(cmd & 4)); assert(window == 0x1234);
    prepared = false; memory.clear(); cleaned = true; return fail != 20;
  }
};
struct FuseIO {
  unsigned raw = 0, calls = 0; bool unstable = false;
  unsigned command() { return 2; }
  unsigned readFuse() { return raw ^ ((unstable && calls++) ? 1U : 0U); }
};
int main() {
  unsigned cases = 0;
  { HostIO io; auto r = HostRead::run(io);
    assert(r.passed && r.phase[0].matched == 1024 && r.phase[1].matched == 1024);
    assert(io.cleaned && !io.opened && !io.prepared && io.gpuReads == 2048 && io.cmd == 0);
    assert(r.windowProgrammed == 0x020437af && r.windowRestored && r.cleanupVerified); ++cases; }
  for (int fail = 0; fail <= 25; ++fail) {
    HostIO io; io.fail = fail; auto r = HostRead::run(io);
    assert(!r.passed && !io.opened && !io.mapped && io.gpuReads <= 2048);
    if (fail == 18 || fail == 19) assert(r.resourcesRetained && io.prepared && !io.cleaned);
    else if (fail != 25) assert(io.cleaned && !io.prepared);
    if (fail <= 13 || fail >= 23) assert(io.gpuReads == (fail == 12 ? 1024U : 0U));
    ++cases;
  }
  for (unsigned raw : {0U, 1U, 2U, 3U, 4U, 7U, 0x80000000U, 0xffffffffU, 0xbadf0000U}) {
    FuseIO io; io.raw = raw; FWSECFuse::Snapshot fuse; const char *error = fuse(io);
    if (raw <= 3) { assert(!error && fuse.index == (raw == 0 ? 0 : raw == 1 ? 1 : 2)); }
    else assert(error && fuse.index == -1);
    assert(fuse.reads <= 2); ++cases;
  }
  { FuseIO io; io.unstable = true; FWSECFuse::Snapshot fuse;
    assert(fuse(io) && fuse.index == -1 && fuse.reads == 2); ++cases; }
  std::printf("Host read/fuse: %u success, fault and restoration cases passed\n", cases);
}
