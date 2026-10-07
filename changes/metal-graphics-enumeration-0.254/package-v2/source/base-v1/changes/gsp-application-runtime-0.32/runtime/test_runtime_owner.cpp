#include "../../../driver/GSPExecutionOwner.hpp"
#include <cassert>
#include <cstdio>
#include <vector>

namespace L = GSPLaunchOwnership;
namespace D = GSPDmaProtocol;
static unsigned checks = 0;
#define CHECK(expression) do { ++checks; assert(expression); } while (0)

static Boot0::Facts identity() {
  Boot0::Facts f;
  f.identity = 0x252010de; f.subsystem = 0x104c1043;
  f.targetBDF = f.barTypesValid = true; f.command = 0; f.pmcsr = 8; f.link = 0x1083;
  f.bar0 = f.descriptor0 = 0xfb000000; f.length0 = 16ULL << 20;
  f.bar1 = f.descriptor1 = 0x824000000; f.length1 = 64ULL << 20;
  return f;
}
static FWSECPreflight::Snapshot board() {
  FWSECPreflight::Snapshot s;
  const unsigned raw[] = {0, 1, 0x1ffffe00, 0, 0x10, 0x10, 0, 0x80420100, 1, 0};
  for (unsigned i = 0; i < FWSECPreflight::Count; ++i) {
    s.first[i] = s.second[i] = raw[i]; s.reads[i] = 2;
  }
  s.complete = s.layoutValid = s.engineIdle = s.wprClear = s.displaySupported = true;
  s.vramBytes = L::VramBytes; s.workspaceBoundary = s.frtsEnd = L::BiosStart;
  s.frtsOffset = L::ExpectedLayout.regions[L::Frts].offset; s.wprLo = 0x1ffffe0000ULL;
  return s;
}
static FWSECDisplay::Snapshot display() {
  FWSECDisplay::Snapshot d;
  d.first[0] = d.second[0] = d.mask = 15; d.first[1] = d.second[1] = d.count = 4;
  for (unsigned i = 0; i < FWSECDisplay::Count; ++i) d.reads[i] = 2;
  d.complete = d.idle = true;
  return d;
}
struct IO {
  bool baseline=true,cleanup=true;
  unsigned releases=0,closes=0,pins=0;
  bool baselineHeld() const{return baseline;}
  bool releaseHost(){++releases;return cleanup;}
  void closeProvider(){++closes;}
  void pin(){++pins;}
};
static GSPExecutionOwner::Owner claimed(){
  GSPExecutionOwner::Owner o;CHECK(o.claim(identity(),board(),display(),17,true));return o;
}
static L::ContentSeal seal(){
  L::ContentSeal s;s.generation=17;s.structuralMask=s.AllStructures;s.firmwareMask=s.FirmwareResources;
  s.globalPagesValidated=s.uploadsSynchronized=s.readbacksVerified=true;return s;
}
static GSPExecutionOwner::Owner sealed(){
  auto o=claimed();CHECK(o.freeze(17));CHECK(o.accept(seal()));return o;
}
int main(){
  for(unsigned failAt=0;failAt<7;++failAt){
    auto o=sealed();IO io;
    CHECK(!o.beginSec2(17));CHECK(!o.beginBoot(17));
    CHECK(!o.beginFwsec(18));CHECK(o.sealed());CHECK(o.beginFwsec(17));
    CHECK(!o.beginFwsec(17));CHECK(o.executionAttempted());
    if(failAt>0)CHECK(o.startFwsec(17));
    if(failAt>1)CHECK(o.fwsecComplete(17,true));
    if(failAt>2)CHECK(o.beginSec2(17));
    if(failAt>3)CHECK(o.sec2Complete(17,true));
    if(failAt>4){CHECK(o.beginBoot(17));CHECK(o.startSec2(17));CHECK(o.startMask()==3);}
    if(failAt>5){CHECK(o.bootReturned(17,true));CHECK(o.firstStatus(17));}
    o.fail();CHECK(!o.cleanup(io));CHECK(!io.releases && !io.closes);CHECK(o.ledger().owned());
  }
  {
    auto o=sealed();CHECK(o.beginFwsec(17));CHECK(o.startFwsec(17));
    CHECK(!o.fwsecComplete(18,true));CHECK(!o.beginSec2(17));
  }
  {
    auto o=sealed();CHECK(o.beginFwsec(17));CHECK(o.startFwsec(17));
    CHECK(o.fwsecComplete(17,true));CHECK(o.beginSec2(17));CHECK(o.sec2Complete(17,true));
    CHECK(o.beginBoot(17));CHECK(o.startSec2(17));CHECK(!o.startSec2(17));IO io;
    CHECK(!o.cleanup(io));CHECK(!io.releases);
  }
  {
    auto o=claimed();IO io;CHECK(o.canUpload());CHECK(o.cleanup(io));
    CHECK(io.releases==1 && io.closes==1 && io.pins==0);CHECK(!o.ledger().owned());
    CHECK(o.cleanup(io));CHECK(io.releases==1 && io.closes==1);
    CHECK(!o.claim(identity(),board(),display(),18,true));
  }
  {
    auto o=claimed();CHECK(!o.freeze(18));CHECK(o.canUpload());CHECK(o.freeze(17));CHECK(!o.canUpload());
    auto s=seal();s.generation=18;CHECK(!o.accept(s));CHECK(!o.sealed());
    s=seal();s.readbacksVerified=false;CHECK(!o.accept(s));CHECK(!o.sealed());
    CHECK(o.accept(seal()));CHECK(o.sealed());CHECK(!o.freeze(17));CHECK(!o.accept(seal()));
    IO io;CHECK(o.cleanup(io));CHECK(io.releases==1 && io.closes==1);
  }
  // All fault/close callers share cleanup. Exposure, start, and a failed late
  // generation check must block complete/clear/close even at PCI command zero.
  for(unsigned route=0;route<6;++route){
    auto o=sealed();IO io;
    if(route==0)CHECK(o.expose(17));
    if(route==1)CHECK(o.start(17));
    if(route==2){CHECK(o.expose(17));CHECK(o.start(17));}
    if(route==3)CHECK(!o.expose(18));
    if(route==4)CHECK(!o.start(18));
    if(route==5){CHECK(o.start(17));CHECK(!o.start(17));}
    o.fail();CHECK(!o.canUpload());CHECK(!o.cleanup(io));CHECK(!o.cleanup(io));
    CHECK(io.releases==0 && io.closes==0 && io.pins==2);CHECK(o.ledger().owned());
  }
  for(bool owned:{false,true}){
    auto o=owned?claimed():GSPExecutionOwner::Owner{};IO io;io.cleanup=false;
    CHECK(!o.cleanup(io));CHECK(io.releases==1 && io.closes==0);io.cleanup=true;
    CHECK(!o.cleanup(io));CHECK(io.releases==1 && io.closes==0);
  }
  {
    auto o=sealed();IO io;io.baseline=false;CHECK(!o.cleanup(io));
    io.baseline=true;CHECK(!o.cleanup(io));CHECK(!io.releases && !io.closes);
  }
  {
    auto o=claimed();IO io;CHECK(o.freeze(17));o.fail();CHECK(o.cleanup(io));
    CHECK(io.releases==1 && io.closes==1);
  }
  {
    auto o=claimed();IO io;CHECK(!o.start(17));CHECK(!o.cleanup(io));CHECK(!io.releases);
  }
  {
    GSPExecutionOwner::Owner empty; CHECK(!empty.beginRuntime(17));
    auto o=sealed(); CHECK(!o.beginRuntime(17));
    CHECK(o.beginFwsec(17)); CHECK(!o.beginRuntime(17)); CHECK(o.startFwsec(17));
    CHECK(o.fwsecComplete(17,true)); CHECK(o.beginSec2(17)); CHECK(o.sec2Complete(17,true));
    CHECK(o.beginBoot(17)); CHECK(o.startSec2(17)); CHECK(!o.beginRuntime(17));
    CHECK(o.bootReturned(17,true)); CHECK(!o.beginRuntime(18)); CHECK(o.beginRuntime(17));
    CHECK(o.phase()==GSPExecutionOwner::Phase::RuntimeReady); CHECK(!o.beginRuntime(17)); CHECK(!o.firstStatus(17));
    CHECK(!o.beginFwsec(17)); CHECK(!o.beginSec2(17)); IO io;
    CHECK(!o.cleanup(io)); CHECK(io.pins==1 && !io.releases && !io.closes);
    CHECK(o.phase()==GSPExecutionOwner::Phase::Retained); CHECK(!o.beginRuntime(17));
  }
  std::printf("GSP execution owner: %u checks passed\n",checks);
}
