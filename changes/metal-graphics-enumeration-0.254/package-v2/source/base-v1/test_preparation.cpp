#include "driver/PreparationProtocol.hpp"
#include <cassert>
#include <cstdio>
#include <vector>

struct RomIO {
  unsigned cmd = 2, calls = 0, signature = 0xaa55;
  int commandAt = -1, corruptAt = -1;
  bool allocationFailure = false;
  std::vector<unsigned> data;
  unsigned command() { return int(calls) == commandAt ? 6 : cmd; }
  unsigned romWord(unsigned off) {
    assert(off < Preparation::RomSize && !(off & 3));
    const unsigned pass = calls++ > Preparation::RomSize / 4;
    return (off == 0 ? signature : off ^ 0x12340000) ^ (pass && int(off) == corruptAt ? 1U : 0U);
  }
  bool allocateRom() { if (allocationFailure) return false; data.resize(Preparation::RomSize / 4); return true; }
  void storeRom(unsigned off, unsigned value) { data.at(off / 4) = value; }
  unsigned savedRomWord(unsigned off) { return data.at(off / 4); }
};

struct DmaIO {
  Boot0::Facts f;
  int fail = -1;
  bool busy = false, opened = false, allocated = false, prepared = false, cleaned = false;
  unsigned cmd = 0;
  DmaIO() {
    f.identity = 0x252010de; f.subsystem = 0x104c1043; f.targetBDF = f.barTypesValid = true;
    f.command = 0; f.pmcsr = 8; f.link = 0x1083;
    f.bar0 = f.descriptor0 = 0xfb000000; f.length0 = 16ULL << 20;
    f.bar1 = f.descriptor1 = 0x824000000; f.length1 = 64ULL << 20;
  }
  bool open() { return opened = !busy; }
  void close() { assert(opened && cleaned && !allocated && !prepared); opened = false; }
  Boot0::Facts facts() { return f; }
  unsigned command() { return fail == 7 ? 4 : cmd; }
  bool selectMapper() { return fail != 0; }
  bool allocate() { allocated = true; return fail != 1; }
  bool prepare() { assert(allocated); return prepared = fail != 2; }
  bool segments(Preparation::Segment *s, unsigned &n, Boot0::U64 &end) {
    assert(prepared); n = 4; end = 16384;
    for (unsigned i = 0; i < 4; ++i) { s[i].address = 0x123000 + i * 4096; s[i].length = 4096; }
    if (fail == 4) s[1].address = s[0].address;
    return fail != 3;
  }
  bool verifyCPU() { return fail != 5; }
  bool cleanup() { cleaned = true; allocated = prepared = false; return fail != 6; }
};

int main() {
  unsigned cases = 0;
  {
    RomIO io; Preparation::RomResult r;
    assert(!Preparation::captureRom(io, r) && r.stable && r.bytesCaptured == Preparation::RomSize);
    assert(r.wordsRead == Preparation::RomSize / 2 + 1); ++cases;
  }
  for (unsigned signature : {0U, 0xffffffffU, 0xbadf0000U}) {
    RomIO io; io.signature = signature; Preparation::RomResult r;
    assert(Preparation::captureRom(io, r) && !r.stable && r.wordsRead == 1 && io.data.empty()); ++cases;
  }
  for (int fail : {0, 1, 2, 3, 4}) {
    RomIO io;
    if (fail == 0) io.cmd = 0;
    if (fail == 1) io.allocationFailure = true;
    if (fail == 2) io.commandAt = 1025;
    if (fail == 3) io.corruptAt = 0;
    if (fail == 4) io.corruptAt = Preparation::RomSize - 4;
    Preparation::RomResult r;
    assert(Preparation::captureRom(io, r) && !r.stable); ++cases;
  }
  {
    DmaIO io; auto r = Preparation::prepareDma(io);
    assert(r.passed && r.prepared && r.cleanupVerified && !io.opened && io.cmd == 0); ++cases;
  }
  for (int fail = 0; fail < 8; ++fail) {
    DmaIO io; io.fail = fail; auto r = Preparation::prepareDma(io);
    assert(!r.passed && io.cleaned && !io.opened && !io.prepared); ++cases;
  }
  {
    DmaIO io; io.busy = true; auto r = Preparation::prepareDma(io);
    assert(!r.passed && !io.cleaned && !io.allocated); ++cases;
  }
  {
    DmaIO io; io.f.identity = 0xffffffff; auto r = Preparation::prepareDma(io);
    assert(!r.passed && io.cleaned && !io.allocated); ++cases;
  }
  for (unsigned fail = 0; fail < 6; ++fail) {
    Preparation::Segment rows[4];
    for (unsigned i = 0; i < 4; ++i) { rows[i].address = 0x100000 + i * 4096; rows[i].length = 4096; }
    unsigned count = 4; Boot0::U64 end = 16384;
    if (fail == 0) rows[0].address = 0;
    if (fail == 1) rows[0].address++;
    if (fail == 2) rows[0].address = 1ULL << 40;
    if (fail == 3) rows[0].length = 4097;
    if (fail == 4) count = 5;
    if (fail == 5) end = 16383;
    assert(!Preparation::validSegments(rows, count, end)); ++cases;
  }
  std::printf("Preparation: %u capture/mapping fault and success cases passed\n", cases);
}
