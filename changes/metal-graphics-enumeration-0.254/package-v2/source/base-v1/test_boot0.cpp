#include "driver/Boot0Protocol.hpp"
#include <cassert>
#include <cstdio>
#include <string>

struct Fake {
  Boot0::Facts f;
  bool busy = false, mapFailure = false, enableFailure = false, restoreFailure = false, concurrentChange = false;
  bool opened = false, mapped = false;
  unsigned enables = 0, disables = 0, reads = 0, cmd = 0, first = 0x176000a1, second = 0x176000a1;
  std::string events;
  Fake() {
    f.identity = 0x252010de; f.subsystem = 0x104c1043; f.targetBDF = true; f.barTypesValid = true;
    f.command = 0; f.pmcsr = 8; f.link = 0x1083;
    f.bar0 = f.descriptor0 = 0xfb000000; f.length0 = 16ULL << 20;
    f.bar1 = f.descriptor1 = 0x824000000; f.length1 = 64ULL << 20;
  }
  bool open() { events += 'O'; opened = !busy; return opened; }
  Boot0::Facts facts() { assert(opened); cmd = f.command; return f; }
  bool mapPage() { assert(opened && cmd == 0); events += 'M'; mapped = !mapFailure; return mapped; }
  void unmapPage() { assert(mapped); mapped = false; events += 'U'; }
  unsigned command() { if (concurrentChange && mapped && !enables) cmd = 4; return cmd; }
  void setMemory(bool enable) {
    assert(opened);
    if (enable) { assert(mapped); events += 'E'; ++enables; if (!enableFailure) cmd |= 2; }
    else { assert(!mapped); events += 'D'; ++disables; if (!restoreFailure) cmd &= ~2U; }
  }
  unsigned readBoot0() { assert(opened && mapped && cmd == 2); events += 'R'; return reads++ == 0 ? first : second; }
  void close() { assert(opened && !mapped); opened = false; events += 'C'; }
};

int main() {
  unsigned tests = 0;
  {
    Fake f; auto r = Boot0::run(f);
    assert(r.passed && r.reads == 2 && r.commandBefore == 0 && r.commandDuring == 2 && r.commandAfter == 0);
    assert(f.events == "OMERRUDC" && !f.opened && !f.mapped); ++tests;
  }
  for (unsigned i = 0; i < 11; ++i) {
    Fake f;
    switch (i) {
      case 0: f.f.identity = 0xffffffff; break;
      case 1: f.f.targetBDF = false; break;
      case 2: f.f.command = 4; break;
      case 3: f.f.pmcsr = 3; break;
      case 4: f.f.link |= 0x800; break;
      case 5: f.f.descriptor0 += 0x1000; break;
      case 6: f.f.length0 = 2048; break;
      case 7: f.f.length1 = 8ULL << 30; break;
      case 8: f.f.bar1 = f.f.descriptor1 = 0xfc08400000000000ULL; break;
      case 9: f.f.barTypesValid = false; break;
      case 10: f.f.bar0 = f.f.descriptor0 = 0xfb000001; break;
    }
    auto r = Boot0::run(f);
    assert(!r.passed && f.enables == 0 && f.reads == 0 && !f.opened); ++tests;
  }
  {
    Fake f; f.busy = true; auto r = Boot0::run(f);
    assert(!r.passed && f.events == "O" && !f.enables); ++tests;
  }
  {
    Fake f; f.mapFailure = true; auto r = Boot0::run(f);
    assert(!r.passed && f.events == "OMC" && !f.enables); ++tests;
  }
  {
    Fake f; f.concurrentChange = true; auto r = Boot0::run(f);
    assert(!r.passed && !r.restoreVerified && f.enables == 0 && f.disables == 0 && f.cmd == 4); ++tests;
  }
  {
    Fake f; f.enableFailure = true; auto r = Boot0::run(f);
    assert(!r.passed && f.reads == 0 && f.enables == 1 && f.disables == 1 && f.cmd == 0); ++tests;
  }
  for (unsigned val : {0U, 0xffffffffU}) {
    Fake f; f.first = val; auto r = Boot0::run(f);
    assert(!r.passed && r.restoreVerified && f.reads == 1 && f.disables == 1 && f.cmd == 0); ++tests;
  }
  {
    Fake f; f.second ^= 1; auto r = Boot0::run(f);
    assert(!r.passed && r.restoreVerified && f.reads == 2 && f.cmd == 0); ++tests;
  }
  {
    Fake f; f.first = f.second = 0x174000a1; auto r = Boot0::run(f);
    assert(!r.passed && r.restoreVerified && f.cmd == 0); ++tests;
  }
  {
    Fake f; f.restoreFailure = true; auto r = Boot0::run(f);
    assert(!r.passed && !r.restoreVerified && f.disables == 1 && f.cmd == 2 && !f.opened); ++tests;
  }
  std::printf("Boot0 protocol: %u fault/success cases passed\n", tests);
}
