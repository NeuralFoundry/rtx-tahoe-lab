#pragma once
#if defined(KERNEL)
#include <stdint.h>
#else
#include <cstdint>
#endif

// Offline-tested protocol. No Mac adapter exists yet. See pinned NVIDIA
// kern_gmmu_tu102.c: CheckPendingInvalidates, SetPdbToInvalidate, CommitTlbInvalidate.
namespace GMMUInvalidate {
constexpr uint32_t PDBRegister=0x30a0,UpperRegister=0x30a4,CommandRegister=0x30b0;
constexpr uint32_t PDBValue=0x10020,UpperValue=0,CommandValue=0x80000041;
constexpr uint64_t MaxNanoseconds=2000000000ULL;
constexpr uint32_t MaxOperations=4096;
enum class Failure:uint32_t {None,Precondition,Clock,Deadline,Budget,Read,Unreadable,Write,PDBReadback};
struct Preconditions {bool pageTablesAccepted=false,childrenVerified=false,parentVerified=false,exclusiveOwner=false;};
struct Result {
  bool passed=false,attempted=false,commandAttempted=false,completed=false;
  Failure failure=Failure::None;
  uint32_t operations=0,reads=0,writes=0,lastAddress=0,lastValue=0;
  uint64_t start=0,lastTime=0,elapsed=0;
};
inline bool unreadable(uint32_t value) {return value==0xffffffff||(value&0xffff0000)==0xbad00000||(value&0xffff0000)==0xbadf0000;}
template<class IO> bool step(IO &io,Result &r) {
  const auto now=io.now();
  if(now<r.lastTime){r.failure=Failure::Clock;return false;}
  r.lastTime=now;r.elapsed=now-r.start;
  if(r.elapsed>=MaxNanoseconds){r.failure=Failure::Deadline;return false;}
  if(r.operations>=MaxOperations){r.failure=Failure::Budget;return false;}
  ++r.operations;return true;
}
template<class IO> bool read(IO &io,Result &r,uint32_t address,uint32_t &value) {
  if(!step(io,r))return false;
  r.lastAddress=address;++r.reads;
  if(!io.read(address,value)){r.failure=Failure::Read;return false;}
  r.lastValue=value;
  if(unreadable(value)){r.failure=Failure::Unreadable;return false;}
  return true;
}
template<class IO> bool write(IO &io,Result &r,uint32_t address,uint32_t value) {
  if(!step(io,r))return false;
  r.lastAddress=address;r.lastValue=value;++r.writes;
  if(address==CommandRegister)r.commandAttempted=true;
  if(!io.write(address,value)){r.failure=Failure::Write;return false;}
  return true;
}
template<class IO> bool idle(IO &io,Result &r) {
  uint32_t value=0;
  do {if(!read(io,r,CommandRegister,value))return false;}while(value&0x80000000);
  return true;
}
template<class IO> bool run(IO &io,const Preconditions &p,Result &r) {
  r={};
  if(!p.pageTablesAccepted||!p.childrenVerified||!p.parentVerified||!p.exclusiveOwner){r.failure=Failure::Precondition;return false;}
  r.start=r.lastTime=io.now();r.attempted=true;
  if(!idle(io,r))return false;
  // Invalidate only the known VRAM root (0x01002000), not all PDBs.
  if(!write(io,r,PDBRegister,PDBValue)||!write(io,r,UpperRegister,UpperValue))return false;
  uint32_t pdb=0,upper=0;
  if(!read(io,r,PDBRegister,pdb)||!read(io,r,UpperRegister,upper))return false;
  if(pdb!=PDBValue||upper!=UpperValue){r.failure=Failure::PDBReadback;return false;}
  if(!write(io,r,CommandRegister,CommandValue)||!idle(io,r))return false;
  // Account for time spent in the final MMIO read as well.
  const auto now=io.now();
  if(now<r.lastTime){r.failure=Failure::Clock;return false;}
  r.lastTime=now;r.elapsed=now-r.start;
  if(r.elapsed>=MaxNanoseconds){r.failure=Failure::Deadline;return false;}
  r.completed=r.passed=true;return true;
}
}
