#include "ClientFixtureHelpers.hpp"
int main(int argc,char **argv){
  CHECK(argc==4);RuntimeSim runtime(load(argv[1]),load(argv[2]));Bytes requests,records;
  const auto execution=run(runtime,&requests,&records);CHECK(execution.rpc.passed);
  L::Range ranges[6];CHECK(P::mappings(runtime.plan,runtime.golden,ranges));walk(runtime,ranges,6,runtime.contexts.childBytes);
  FenceSim fence;const auto result=fenceRun(fence,runtime.golden,execution,requests,records);CHECK(result.passed);
  Bytes raw(ExecutionCapture::Bytes);ExecutionCapture::Result capture;CaptureIO io{fence};
  CHECK(ExecutionCapture::capture(io,raw.data(),capture)&&capture.passed);
  const std::string out=argv[3];save(out+"/device-capture.bin",raw);save(out+"/requests.bin",requests);save(out+"/records.bin",records);
  ChannelABI::Owner owner;owner.generation=0x12345678;owner.phase=17;owner.pinned=owner.owned=owner.lease=owner.mapped=owner.queueClaimed=1;owner.command=6;
  owner.ringClaimed=owner.contextsClaimed=owner.windowObserved=1;owner.barBase=34963718144ULL;owner.physical=owner.barBase+0x1002000;owner.excludedNs=runtime.excluded;
  unsigned long long words[128];ExecutionABI::rm(execution,owner,EC::Steps,words);saveWords(out+"/rm-info.bin",words,80);
  ExecutionABI::external(execution.external,owner,XV::Steps,true,words);saveWords(out+"/external-info.bin",words,64);
  save(out+"/external-requests.bin",runtime.externalRequestCapture);save(out+"/external-records.bin",runtime.externalRecordCapture);
  Bytes externalIndex(execution.external.rpc.count*72);
  for(unsigned i=0;i<execution.external.rpc.count;++i){const auto &r=execution.external.rpc.records[i];const unsigned long long row[]={r.offset,r.bytes,r.function,r.result,r.sequence,r.payload,r.step,r.slot,r.elapsedUs};
    for(unsigned j=0;j<9;++j)L::write64(externalIndex.data()+i*72+j*8,row[j]);}
  save(out+"/external-index.bin",externalIndex);
  ExecutionABI::plan(execution.context,runtime.golden,owner.generation,words);saveWords(out+"/plan-info.bin",words,64);
  ExecutionABI::fence(result,owner,true,true,3,words);saveWords(out+"/fence-info.bin",words,64);
  ExecutionABI::capture(capture,owner,words);saveWords(out+"/device-info.bin",words,32);
  Bytes root(12288),children(L::MaxChildBytes);ChannelSnapshot::Result snapshot;
  ChannelSnapshot::capture(runtime,runtime.contexts.childBytes,root.data(),children.data(),snapshot);CHECK(snapshot.passed);
  children.resize(snapshot.childBytes);save(out+"/root-capture.bin",root);save(out+"/children-capture.bin",children);
  ExecutionABI::snapshot(snapshot,owner,words);saveWords(out+"/snapshot-info.bin",words,64);
  M::restoreWindow(runtime,runtime.fixed);CHECK(runtime.fixed.windowRestored);
  ExecutionABI::memory(runtime.fixed,0,owner,words);saveWords(out+"/fixed-info.bin",words,64);
  ExecutionABI::memory(runtime.contexts,1,owner,words);saveWords(out+"/contexts-info.bin",words,64);
  ChannelABI::plan(runtime.golden,owner.generation,words);saveWords(out+"/golden-plan.bin",words,128);
  // RuntimeSim supplies a validated prefix journal, not its historical I/O
  // counters. Complete this explicitly synthetic diagnostic fixture only.
  auto golden=runtime.goldenResult;golden.rpc.validated=golden.rpc.attempted=golden.rpc.prefixConsumed=true;
  golden.rpc.step=4;golden.rpc.doorbells=golden.rpc.consumerWrites=5;
  golden.rpc.lastFunction=103;golden.rpc.lastResult=golden.rpc.lastParamStatus=0;
  ChannelABI::rm(golden,owner,true,true,true,words);saveWords(out+"/golden-rm.bin",words,64);
  save(out+"/golden-root.bin",runtime.root);save(out+"/golden-children.bin",Bytes(runtime.children.begin(),runtime.children.begin()+runtime.storage.goldenBytes));
  Bytes index(execution.rpc.count*72);
  for(unsigned i=0;i<execution.rpc.count;++i){const auto &r=execution.rpc.records[i];const unsigned long long row[]={r.offset,r.bytes,r.function,r.result,r.sequence,r.payload,r.step,r.slot,r.elapsedUs};
    for(unsigned j=0;j<9;++j)L::write64(index.data()+i*72+j*8,row[j]);}
  save(out+"/index.bin",index);
  std::cout<<"{\"passed\":true,\"hardware_accessed\":false,\"checks\":"<<checks<<",\"compute_verified\":false,\"metal_verified\":false}\n";
}
