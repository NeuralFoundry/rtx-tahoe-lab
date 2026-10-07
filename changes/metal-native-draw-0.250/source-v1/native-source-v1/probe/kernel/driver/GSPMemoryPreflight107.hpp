#pragma once
#include "Boot0Protocol.hpp"
#include "GSPBooterPreflight.hpp"
#include "../RTXMemoryEvidence107.hpp"
namespace GSPMemory107 {
struct Snapshot {
 unsigned first=0,second=0,reads=0;
 const char *status="not-run";
 template<class IO>const char *operator()(IO &io){
  *this=Snapshot{};
  for(unsigned i=0;i<2;++i){
   if(io.command()!=2)return status="memory-preflight-command-changed";
   unsigned &v=i?second:first;v=io.readVramMiB();++reads;
   if(v!=RTXMemory107::ExpectedMiB)return status="memory-preflight-capacity-mismatch";
  }
  if(first!=second||io.command()!=2)return status="memory-preflight-unstable";
  status="memory-preflight-measured";return nullptr;
 }
};
struct Capture {
 GSPBooterPreflight::Snapshot &fuse;Snapshot &memory;
 template<class IO>const char *operator()(IO &io){
  memory=Snapshot{};
  if(const char *error=fuse(io))return error;
  return memory(io);
 }
};
inline bool evidence(unsigned long long generation,const Boot0::Facts &facts,
 const Boot0::Result &boot,const GSPBooterPreflight::Snapshot &fuse,const Snapshot &memory,
 unsigned char *out,unsigned bytes){
 if(!boot.passed||!boot.restoreVerified||!boot.enableAttempted||Boot0::preflight(facts)||fuse.index!=0)return false;
 RTXMemory107::Evidence e;e.generation=generation;e.identity=facts.identity;e.subsystem=facts.subsystem;
 e.first=memory.first;e.second=memory.second;e.reads=memory.reads;e.reportedBytes=RTXMemory107::U64(memory.first)<<20;e.complete=1;
 e.fuseFirst=fuse.first;e.fuseSecond=fuse.second;e.fuseReads=fuse.reads;
 e.bootFirst=boot.first;e.bootSecond=boot.second;e.bootReads=boot.reads;
 e.commandBefore=boot.commandBefore;e.commandDuring=boot.commandDuring;e.commandAfter=boot.commandAfter;e.restored=1;
 e.bdf=facts.targetBDF?0x100:0;return RTXMemory107::encode(e,out,bytes);
}
}
