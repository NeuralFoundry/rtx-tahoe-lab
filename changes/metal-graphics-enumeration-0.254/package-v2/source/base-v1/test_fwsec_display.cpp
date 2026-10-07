#include "driver/FWSECDisplay.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace FWSECDisplay;

struct DisplayIO {
  std::array<unsigned, Count> values{{0xf, 4, 0, 0, 0, 0}}, seen{};
  std::vector<unsigned> order;
  unsigned commands = 0, badCommandAt = 0, commandValue = 2;
  unsigned alteredIndex = Count, alteredSample = 0, alteredValue = 0;
  unsigned command() { return ++commands == badCommandAt ? 0U : commandValue; }
  unsigned readDisplay(unsigned index) {
    assert(index < Count); order.push_back(index);
    const unsigned sample = seen[index]++;
    return index == alteredIndex && sample == alteredSample ? alteredValue : values[index];
  }
};

int main() {
  unsigned cases = 0;
  const unsigned expected[] = {0x610060,0x610074,0x612078,0x612878,0x613078,0x613878};
  for (unsigned i=0;i<Count;++i) assert(Offsets[i]==expected[i]);
  for (unsigned count = 1; count <= 4; ++count) {
    for (unsigned mask = 0; mask < (1U << count); ++mask) {
      DisplayIO io; Snapshot s; io.values[Mask] = 0x10000 | mask; io.values[HeadCount] = 0x1000 | count;
      for (unsigned head = 0; head < 4; ++head) if (!(mask & (1U << head))) io.values[Head0 + head] = 0xffffffffU;
      assert(s.capture(io) == nullptr && s.complete && s.idle && validIdleEvidence(s));
      assert(s.count == count && s.mask == mask);
      for (unsigned head = 0; head < 4; ++head) assert(s.reads[Head0 + head] == ((mask & (1U << head)) ? 2U : 0U));
      ++cases;
    }
  }
  for (unsigned count : {0U, 5U, 15U}) {
    DisplayIO io; Snapshot s; io.values[HeadCount] = count;
    assert(s.capture(io) != nullptr && !s.complete && !s.idle && io.order.size() == 4);
    assert(std::strcmp(s.status, "fwsec-display-topology-invalid") == 0); ++cases;
  }
  for (unsigned mask : {0x10U, 0x80U, 0xffU}) {
    DisplayIO io; Snapshot s; io.values[Mask] = mask;
    assert(s.capture(io) != nullptr && io.order.size() == 4 && !s.complete); ++cases;
  }
  {
    DisplayIO io; Snapshot s; io.values[Mask] = 4; io.values[HeadCount] = 2;
    assert(s.capture(io) != nullptr && io.order.size() == 4); ++cases;
  }
  for (unsigned head=0;head<4;++head) for (unsigned mode=1;mode<4;++mode) {
    DisplayIO io; Snapshot s; io.values[Head0+head] = mode<<8;
    assert(s.capture(io)==nullptr && s.complete && !s.idle && !validIdleEvidence(s));
    assert(io.order.size()==12); ++cases;
  }
  for (unsigned command : {0U, 1U, 4U, 7U, 0xffffU}) {
    DisplayIO io; Snapshot s;
    assert(s.capture(io, command)!=nullptr && io.order.empty() && io.commands==0); ++cases;
  }
  {
    DisplayIO io; Snapshot s; io.commandValue=6;
    assert(s.capture(io,6)==nullptr && validIdleEvidence(s)); ++cases;
  }
  for (unsigned call=1;call<=12;++call) {
    DisplayIO io; Snapshot s; io.badCommandAt=call;
    assert(s.capture(io)!=nullptr && !s.complete && io.order.size()==call-1 && io.commands==call);
    assert(std::strcmp(s.status,"fwsec-display-command-changed")==0); ++cases;
  }
  for (unsigned index=0;index<Count;++index) {
    for (unsigned sample=0;sample<2;++sample) for (unsigned value : {0xffffffffU,0xbadf0000U,0xbad01234U}) {
      DisplayIO io; Snapshot s; io.alteredIndex=index; io.alteredSample=sample; io.alteredValue=value;
      assert(s.capture(io)!=nullptr && !s.complete && io.order.size()==index*2+sample+1);
      assert(std::strcmp(s.status,"fwsec-display-register-unreadable")==0); ++cases;
    }
    DisplayIO io; Snapshot s; io.alteredIndex=index; io.alteredSample=1; io.alteredValue=io.values[index]^1;
    assert(s.capture(io)!=nullptr && !s.complete && io.order.size()==index*2+2);
    assert(std::strcmp(s.status,"fwsec-display-register-unstable")==0); ++cases;
  }
  {
    DisplayIO io; Snapshot s; assert(s.capture(io)==nullptr);
    DisplayIO failed; failed.badCommandAt=1; assert(s.capture(failed)!=nullptr);
    assert(!s.complete && !s.idle && s.mask==0 && s.count==0 && s.reads[Mask]==0); ++cases;
  }
  std::printf("FWSEC display: %u cases passed\n",cases);
}
