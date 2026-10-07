// All device data in this test is simulated. No hardware backend is used.
// Reuse the existing simulator; the renamed regression main is never called.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4715)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#endif
#define main transactionRegressionMain
#include "../transactions/test_channel_transactions.cpp"
#undef main
#ifdef _MSC_VER
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif
#include "ChannelABI.hpp"
namespace S=ChannelSnapshot;
struct SnapshotIO {
  Bytes root=Bytes(S::RootBytes),children=Bytes(S::MaxChildren);
  unsigned calls=0,failAt=0,clockCalls=0,clockFault=0;bool owned=true;
  unsigned long long time=100;
  bool ready(){return owned;}
  unsigned long long nowNs(){++clockCalls;if(clockFault==1&&clockCalls==3)return 0;
    time+=clockFault==2?S::BudgetNs:100;return time;}
  bool readMemory(unsigned address,unsigned char *out,unsigned bytes){
    CHECK(bytes==4096);++calls;if(calls==failAt)return false;
    if(address>=GMMULeaves::OldBase&&address+bytes<=GMMULeaves::OldBase+root.size())
      std::memcpy(out,root.data()+address-GMMULeaves::OldBase,bytes);
    else {CHECK(address>=GMMULeaves::NewBase&&address+bytes<=GMMULeaves::NewBase+children.size());
      std::memcpy(out,children.data()+address-GMMULeaves::NewBase,bytes);}
    return true;
  }
};
static void saveWords(const std::string &path,const ChannelABI::U64 *words,unsigned n){
  Bytes raw(n*8);for(unsigned i=0;i<n;++i)GMMULeaves::write64(raw.data()+i*8,words[i]);save(path,raw);
}
static void memoryFixture(ChannelMemory::Result &m,unsigned backing,unsigned child,unsigned links){
  m.attempted=m.passed=m.modified=m.parentAttempted=m.backingVerified=m.childrenVerified=true;
  m.operations=20;m.reads=12;m.writes=8;m.inspectedBytes=backing+child;
  m.zeroedBytes=m.verifiedBackingBytes=backing;m.childBytes=m.verifiedChildBytes=child;m.linksPublished=links;
  m.start=100;m.elapsed=10000;
  auto &v=m.invalidation;v.passed=v.completed=v.commandAttempted=true;v.operations=5;v.reads=2;v.writes=3;
  v.lastAddress=0x30b0;v.start=100;v.lastTime=200;v.elapsed=100;
}
int main(int argc,char **argv){
  CHECK(argc==3);const std::string out=argv[2];const auto packet=load(argv[1]);
  SnapshotIO baseline;Bytes root(S::RootBytes),children(S::MaxChildren);S::Result snapshot;
  ++scenarios;S::capture(baseline,S::MaxChildren,root.data(),children.data(),snapshot);
  CHECK(snapshot.passed&&snapshot.reads==14&&snapshot.rootBytes==S::RootBytes&&snapshot.childBytes==S::MaxChildren);
  for(unsigned i=1;i<=14;++i){++scenarios;SnapshotIO io;io.failAt=i;S::capture(io,S::MaxChildren,root.data(),children.data(),snapshot);
    CHECK(!snapshot.passed&&snapshot.failure==5&&snapshot.reads==i&&snapshot.rootBytes+snapshot.childBytes==(i-1)*4096);}
  for(unsigned part=0;part<2;++part){++scenarios;SnapshotIO io;R::put32((part?io.children:io.root).data()+4,0xbad0acff);
    S::capture(io,S::MaxChildren,root.data(),children.data(),snapshot);
    CHECK(!snapshot.passed&&snapshot.failure==6&&snapshot.reads==(part?4U:1U));
    CHECK(R::get32((part?children:root).data()+4)==0xbad0acff);}
  for(unsigned fault=1;fault<=2;++fault){++scenarios;SnapshotIO io;io.clockFault=fault;
    S::capture(io,S::MaxChildren,root.data(),children.data(),snapshot);CHECK(!snapshot.passed&&snapshot.failure==(fault==1?3U:4U));}
  for(unsigned n:{0U,4096U,8193U,S::MaxChildren+4096}){++scenarios;SnapshotIO io;
    S::capture(io,n,root.data(),children.data(),snapshot);CHECK(!snapshot.attempted&&snapshot.failure==1&&io.calls==0);}
  {++scenarios;SnapshotIO io;S::capture(io,8192,children.data(),children.data(),snapshot);CHECK(snapshot.failure==1&&io.calls==0);}
  {++scenarios;SnapshotIO io;io.owned=false;S::capture(io,8192,root.data(),children.data(),snapshot);CHECK(snapshot.failure==2&&io.calls==0);}
  Sim io(packet);Bytes records(R::MaxBytes),requests(5*4096),scratch(4096);T::Result result;
  ++scenarios;T::execute(io,io.prep,io.prepBytes.data(),io.pd,io.pdBytes.data(),records.data(),requests.data(),scratch.data(),result);
  CHECK(result.rpc.passed&&result.contextPrepared);records.resize(result.rpc.bytes);save(out+"/records.bin",records);save(out+"/requests.bin",requests);
  Bytes index(result.rpc.count*72);
  for(unsigned i=0;i<result.rpc.count;++i){const auto &r=result.rpc.records[i];const ChannelABI::U64 row[]={r.offset,r.bytes,r.function,r.result,r.sequence,r.payload,r.step,r.slot,r.elapsedUs};
    for(unsigned j=0;j<9;++j)GMMULeaves::write64(index.data()+i*72+j*8,row[j]);}
  save(out+"/index.bin",index);
  SnapshotIO snap;GMMULeaves::write64(snap.root.data(),0x100322);GMMULeaves::write64(snap.root.data()+4096,0x100422);
  GMMULeaves::write64(snap.root.data()+8192+128*8,0x1122334455667788ULL); // Explicitly synthetic GSP-owned entry.
  GMMULeaves::Range ranges[10];GMMULeaves::Result tree;CHECK(C::mappingRanges(result.context,ranges));
  CHECK(GMMULeaves::build(snap.root.data(),unsigned(snap.root.size()),ranges,10,snap.children.data(),unsigned(snap.children.size()),tree));
  GMMULeaves::write64(snap.root.data()+GMMULeaves::ParentOffset,GMMULeaves::ParentValue);
  S::capture(snap,unsigned(tree.childBytes),root.data(),children.data(),snapshot);CHECK(snapshot.passed);
  children.resize(tree.childBytes);save(out+"/root-capture.bin",root);save(out+"/children-capture.bin",children);
  ChannelABI::Owner owner;owner.generation=777;owner.phase=17;owner.pinned=owner.owned=owner.lease=owner.mapped=1;owner.command=6;
  owner.barBase=34963718144ULL;owner.physical=owner.barBase+0x1002000;
  owner.ringClaimed=owner.contextsClaimed=owner.windowObserved=owner.queueClaimed=1;owner.excludedNs=10000;
  ChannelABI::U64 words[128];ChannelMemory::Result ring,contexts;
  memoryFixture(ring,0x7000,8192,1);ring.windowSaved=ring.windowRestored=true;ring.windowBefore=ring.windowAfter=0x80173d90;
  memoryFixture(contexts,unsigned(result.context.backingBytes),unsigned(tree.childBytes),unsigned(tree.childBytes/4096-2));
  ChannelABI::memory(ring,0,owner,words);saveWords(out+"/ring-info.bin",words,64);
  ChannelABI::memory(contexts,1,owner,words);saveWords(out+"/contexts-info.bin",words,64);
  ChannelABI::rm(result,owner,true,true,true,words);saveWords(out+"/rm-info.bin",words,64);
  ChannelABI::plan(result.context,777,words);saveWords(out+"/plan-info.bin",words,128);
  ChannelABI::snapshot(snapshot,owner,words);saveWords(out+"/snapshot-info.bin",words,64);
  C::Plan empty;ChannelABI::plan(empty,777,words);CHECK(words[3]==0);for(unsigned i=4;i<128;++i)CHECK(words[i]==0);
  std::printf("{\"passed\":true,\"hardware_accessed\":false,\"synthetic_fixture\":true,\"scenarios\":%llu,\"checks\":%llu}\n",scenarios,checks);
}
