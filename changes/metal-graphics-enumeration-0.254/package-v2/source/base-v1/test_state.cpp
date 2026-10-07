#include "driver/StateSnapshot.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>

struct StateTransport {
  Boot0::Facts f;
  bool opened = false, mapped = false, restoreFailure = false;
  unsigned cmd = 0, fixedReads = 0, bootReads = 0, reads[5] = {};
  unsigned values[5] = {0x176000a1, 0, 1, 0xff, 6144};
  int unstable = -1, changedAt = -1;
  StateTransport() {
    f.identity = 0x252010de; f.subsystem = 0x104c1043; f.targetBDF = f.barTypesValid = true;
    f.command = 0; f.pmcsr = 8; f.link = 0x1083;
    f.bar0 = f.descriptor0 = 0xfb000000; f.length0 = 16ULL << 20;
    f.bar1 = f.descriptor1 = 0x824000000; f.length1 = 64ULL << 20;
  }
  bool open() { opened = true; return true; }
  Boot0::Facts facts() { return f; }
  bool mapPage() { mapped = true; return true; }
  void unmapPage() { assert(mapped); mapped = false; }
  unsigned command() { if (changedAt == int(fixedReads)) cmd |= 4; return cmd; }
  void setMemory(bool enabled) { if (enabled) cmd |= 2; else if (!restoreFailure) cmd &= ~2U; }
  unsigned readBoot0() { assert(cmd == 2 && mapped); ++bootReads; return 0xb76000a1; }
  unsigned readFixed(unsigned index) {
    assert(opened && mapped && cmd == 2 && bootReads == 2 && index < 5);
    if (index == 3) assert(values[2] & 1);
    ++fixedReads;
    return values[index] ^ ((unstable == int(index) && reads[index]++) ? 1U : 0U);
  }
  void close() { assert(opened && !mapped); opened = false; }
};

int main() {
  unsigned tests = 0;
  {
    StateTransport t; GPUState::Snapshot s; auto r = Boot0::run(t, s);
    assert(r.passed && s.complete && t.fixedReads == 10 && t.cmd == 0 && !t.opened); ++tests;
  }
  {
    StateTransport t; t.values[2] = 0; GPUState::Snapshot s; auto r = Boot0::run(t, s);
    assert(r.passed && s.complete && s.reads[3] == 0 && t.fixedReads == 8); ++tests;
  }
  for (unsigned reg = 0; reg < 5; ++reg) {
    for (unsigned error : {0xffffffffU, 0xbadf5000U}) {
      StateTransport t; t.values[reg] = error; GPUState::Snapshot s; auto r = Boot0::run(t, s);
      assert(!r.passed && !s.complete && s.reads[reg] == 1 && r.restoreVerified && t.cmd == 0 && !t.opened); ++tests;
    }
    StateTransport t; t.unstable = int(reg); GPUState::Snapshot s; auto r = Boot0::run(t, s);
    assert(!r.passed && !s.complete && s.reads[reg] == 2 && r.restoreVerified && !t.opened); ++tests;
  }
  for (int n = 0; n < 10; ++n) {
    StateTransport t; t.changedAt = n; GPUState::Snapshot s; auto r = Boot0::run(t, s);
    assert(!r.passed && !s.complete && int(t.fixedReads) == n && !t.opened); ++tests;
  }
  for (unsigned invalid : {0U, 1023U, 65537U}) {
    StateTransport t; t.values[4] = invalid; GPUState::Snapshot s; auto r = Boot0::run(t, s);
    assert(!r.passed && !s.complete && r.restoreVerified); ++tests;
  }
  {
    StateTransport t; t.values[0] = 0x174000a1; GPUState::Snapshot s; auto r = Boot0::run(t, s);
    assert(!r.passed && t.fixedReads == 2 && r.restoreVerified); ++tests;
  }
  {
    StateTransport t; t.restoreFailure = true; GPUState::Snapshot s; auto r = Boot0::run(t, s);
    assert(s.complete && !r.passed && !r.restoreVerified && !t.opened); ++tests;
  }
  {
    StateTransport t; t.values[1] = 0x600000; t.values[3] = 0x12;
    GPUState::Snapshot s; auto r = Boot0::run(t, s);
    // A measured non-ready state is still a successful snapshot, not boot permission.
    assert(r.passed && s.complete && s.first[1] != 0 && s.first[3] != 0xff); ++tests;
  }
  std::printf("State protocol: %u fault/success cases passed\n", tests);
}
