#include "GMMUInvalidate.hpp"
#include "gmmu-reference/dev_vm.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace GMMUInvalidate;
static_assert(PDBRegister==NV_VIRTUAL_FUNCTION_PRIV_MMU_INVALIDATE_PDB,"PDB register");
static_assert(UpperRegister==NV_VIRTUAL_FUNCTION_PRIV_MMU_INVALIDATE_UPPER_PDB,"Upper register");
static_assert(CommandRegister==NV_VIRTUAL_FUNCTION_PRIV_MMU_INVALIDATE,"Command register");
static_assert(PDBValue==((0x1002000>>NV_VIRTUAL_FUNCTION_PRIV_MMU_INVALIDATE_PDB_ADDR_ALIGNMENT)<<4),"Fixed PDB");
static_assert(CommandValue==((NV_VIRTUAL_FUNCTION_PRIV_MMU_INVALIDATE_TRIGGER_TRUE<<31)|
 (NV_VIRTUAL_FUNCTION_PRIV_MMU_INVALIDATE_SYS_MEMBAR_TRUE<<6)|NV_VIRTUAL_FUNCTION_PRIV_MMU_INVALIDATE_ALL_VA_TRUE),"Scoped command");
struct Op {bool write;uint32_t addr,value;};
struct IO {
  uint64_t time=100,clockStep=1;uint32_t clockCalls=0,clockFault=0,deadlineAt=0,operations=0,failAt=0,badAt=0,badValue=0xffffffff;
  uint32_t pdb=0,upper=0,busyBefore=0,busyAfter=0;bool triggered=false,stuckBefore=false,stuckAfter=false,corruptPDB=false;
  std::vector<Op> log;
  uint64_t now() {++clockCalls;if(clockFault&&clockCalls==clockFault)return 0;if(deadlineAt&&clockCalls==deadlineAt)time+=MaxNanoseconds;time+=clockStep;return time;}
  bool read(uint32_t addr,uint32_t &value) {
    ++operations;
    if(operations==failAt)return false;
    if(addr==PDBRegister)value=corruptPDB?pdb^1:pdb;
    else if(addr==UpperRegister)value=upper;
    else if(addr==CommandRegister) {
      auto &busy=triggered?busyAfter:busyBefore;const bool stuck=triggered?stuckAfter:stuckBefore;
      value=(busy||stuck)?0x80000000:0;if(busy)--busy;
    } else std::abort();
    if(operations==badAt)value=badValue;
    log.push_back({false,addr,value});return true;
  }
  bool write(uint32_t addr,uint32_t value) {
    ++operations;log.push_back({true,addr,value});
    if(operations==failAt)return false;
    if(addr==PDBRegister){if(value!=PDBValue)std::abort();pdb=value;}
    else if(addr==UpperRegister){if(value!=UpperValue)std::abort();upper=value;}
    else if(addr==CommandRegister){if(value!=CommandValue||triggered)std::abort();triggered=true;}
    else std::abort();return true;
  }
};
static uint64_t checks=0,scenarios=0;
static void check(bool value){++checks;if(!value){std::fprintf(stderr,"failed scenario %llu check %llu\n",(unsigned long long)scenarios,(unsigned long long)checks);std::exit(1);}}
static Result execute(IO &io,const Preconditions &p={true,true,true,true}) {++scenarios;Result r;const bool ok=run(io,p,r);check(ok==r.passed);check(ok==r.completed);check(ok==(r.failure==Failure::None));check(r.operations<=MaxOperations);check(io.operations==r.operations);return r;}
int main() {
  IO io;auto result=execute(io);check(result.passed&&result.commandAttempted&&result.operations==7&&result.writes==3&&result.reads==4);
  const auto baseline=io.log;
  for(uint32_t mask=0;mask<15;++mask) {IO test;auto r=execute(test,{bool(mask&1),bool(mask&2),bool(mask&4),bool(mask&8)});check(r.failure==Failure::Precondition);check(test.operations==0&&test.clockCalls==0&&!r.attempted);}
  for(uint32_t n=1;n<=7;++n) {
    IO test;test.failAt=n;auto r=execute(test);check(!r.passed);check(test.operations==n);check(r.commandAttempted==(n>=6));
    if(!baseline[n-1].write)for(uint32_t value:{0xffffffffU,0xbad0acffU,0xbadf0000U}) {
      IO bad;bad.badAt=n;bad.badValue=value;auto b=execute(bad);check(b.failure==Failure::Unreadable);check(bad.operations==n);
    }
  }
  for(uint32_t call=2;call<=9;++call) {
    IO back;back.clockFault=call;auto b=execute(back);check(b.failure==Failure::Clock);
    IO slow;slow.deadlineAt=call;auto s=execute(slow);check(s.failure==Failure::Deadline);
  }
  for(uint32_t before:{0U,1U,5U,100U})for(uint32_t after:{0U,1U,5U,100U}) {
    IO test;test.busyBefore=before;test.busyAfter=after;auto r=execute(test);check(r.passed&&r.operations==7+before+after);check(r.writes==3);
  }
  IO wrong;wrong.corruptPDB=true;auto w=execute(wrong);check(w.failure==Failure::PDBReadback&&!w.commandAttempted&&w.writes==2);
  for(bool after:{false,true}) {
    IO stuck;stuck.stuckBefore=!after;stuck.stuckAfter=after;stuck.clockStep=0;auto s=execute(stuck);check(s.failure==Failure::Budget&&s.operations==MaxOperations);check(s.commandAttempted==after);
    IO slow;slow.stuckBefore=!after;slow.stuckAfter=after;slow.clockStep=10000000;auto t=execute(slow);check(t.failure==Failure::Deadline&&t.operations<MaxOperations);check(t.commandAttempted==after);
  }
  std::printf("{\"passed\":true,\"scenarios\":%llu,\"checks\":%llu,\"hardware_accessed\":false}\n",(unsigned long long)scenarios,(unsigned long long)checks);
}
