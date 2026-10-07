#include "driver/FWSECRegion.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>

static FWSECPreflight::Snapshot board() {
  FWSECPreflight::Snapshot s;
  const unsigned raw[] = {0,1,0x1ffffe00,0,0x10,0x10,0,0x80420100,1,0};
  for (unsigned i=0;i<FWSECPreflight::Count;++i) { s.first[i]=s.second[i]=raw[i]; s.reads[i]=2; }
  s.complete=s.layoutValid=s.engineIdle=s.wprClear=s.displaySupported=true;
  s.vramBytes=0x180000000ULL; s.workspaceBoundary=s.frtsEnd=0x17ff00000ULL;
  s.frtsOffset=0x17fe00000ULL; s.wprLo=0x1ffffe0000ULL;
  return s;
}
static FWSECDisplay::Snapshot display() {
  FWSECDisplay::Snapshot d;
  d.first[0]=d.second[0]=d.mask=15; d.first[1]=d.second[1]=d.count=4;
  for (unsigned i=0;i<FWSECDisplay::Count;++i) d.reads[i]=2;
  d.complete=d.idle=true;
  return d;
}
int main() {
  unsigned cases=0;
  const auto good=board(); const auto sleeping=display();
  {
    FWSECRegion::Ledger l;
    assert(!l.owned() && !l.persistent() && !l.matches(good,sleeping,true));
    assert(!l.releaseBeforeStart()); l.markStartAttempted(); assert(!l.persistent());
    assert(!l.claim(good,sleeping,false));
    assert(l.claim(good,sleeping,true) && l.owned() && !l.persistent());
    assert(!l.claim(good,sleeping,true)); assert(l.matches(good,sleeping,true));
    assert(!l.matches(good,sleeping,false));
    assert(l.releaseBeforeStart() && !l.owned()); assert(!l.releaseBeforeStart());
    assert(!l.claim(good,sleeping,true)); // one-shot instance cannot claim again
    ++cases;
  }
  {
    FWSECRegion::Ledger l; assert(l.claim(good,sleeping,true));
    l.markStartAttempted(); l.markStartAttempted();
    assert(l.owned() && l.persistent() && !l.releaseBeforeStart());
    assert(l.matches(good,sleeping,true)); assert(!l.claim(good,sleeping,true)); ++cases;
  }
  auto reject=[&](const FWSECPreflight::Snapshot &s,const FWSECDisplay::Snapshot &d) {
    FWSECRegion::Ledger fresh; assert(!fresh.claim(s,d,true) && !fresh.owned());
    FWSECRegion::Ledger owned; assert(owned.claim(good,sleeping,true));
    assert(!owned.matches(s,d,true)); assert(owned.owned() && !owned.persistent()); ++cases;
  };
  for (unsigned index=0;index<FWSECPreflight::Count;++index) {
    auto s=good; s.reads[index]=1; reject(s,sleeping);
    s=good; s.second[index]^=1; reject(s,sleeping);
    s=good; s.first[index]=s.second[index]=0xbadf0000; reject(s,sleeping);
    s=good; s.first[index]^=1; s.second[index]=s.first[index]; reject(s,sleeping);
  }
  for (unsigned which=0;which<8;++which) {
    auto s=good;
    switch(which) {
      case 0:s.complete=false;break; case 1:s.layoutValid=false;break;
      case 2:s.engineIdle=false;break; case 3:s.wprClear=false;break;
      case 4:s.displaySupported=false;break; case 5:s.workspaceValid=true;break;
      case 6:s.requiresRelocation=true;break; case 7:s.workspaceAddress=0x10000;break;
    }
    reject(s,sleeping);
  }
  for (unsigned which=0;which<6;++which) {
    auto s=good;
    switch(which) {
      case 0:s.vramBytes-=0x100000;break;
      case 1:s.workspaceBoundary-=0x100000;break;
      case 2:s.frtsOffset+=0x100000;break; // overlap BIOS exclusion
      case 3:s.frtsEnd+=0x1000;break; // extend into BIOS exclusion
      case 4:s.wprLo=0;break;
      case 5:s.wprHi=0x17ff00000ULL;break;
    }
    reject(s,sleeping);
  }
  for (unsigned index=0;index<FWSECDisplay::Count;++index) {
    auto d=sleeping; d.reads[index]=0; reject(good,d);
    d=sleeping; d.second[index]^=1; reject(good,d);
    d=sleeping; d.first[index]=d.second[index]=0xffffffff; reject(good,d);
  }
  for (unsigned mode=1;mode<4;++mode) for(unsigned head=0;head<4;++head) {
    auto d=sleeping; d.first[head+2]=d.second[head+2]=mode<<8;
    // Forged idle=true cannot hide SNOOZE, AWAKE, or reserved mode3.
    reject(good,d);
  }
  for (unsigned which=0;which<6;++which) {
    auto d=sleeping;
    switch(which) {
      case 0:d.complete=false;break;case 1:d.idle=false;break;
      case 2:d.count=3;break;case 3:d.mask=7;break;
      case 4:d.count=d.first[1]=d.second[1]=5;break;
      case 5:d.mask=d.first[0]=d.second[0]=0xff;break;
    }
    reject(good,d);
  }
  {
    auto d=sleeping; d.mask=d.first[0]=d.second[0]=5;
    for(unsigned head:{1U,3U}) d.reads[head+2]=0;
    FWSECRegion::Ledger l; assert(l.claim(good,d,true));
    assert(l.matches(good,d,true)); assert(!l.matches(good,sleeping,true));
    d.first[3]=d.second[3]=1; assert(!l.matches(good,d,true)); ++cases;
  }
  std::printf("FWSEC region: %u cases passed\n",cases);
}
