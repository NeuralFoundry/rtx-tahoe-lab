#include "driver/GSPVirtualRegisters.hpp"
#include "changes/gsp-channel-0.23/GMMUInvalidate.hpp"
#include "changes/gsp-channel-0.23/gmmu-reference/dev_vm.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

namespace V=GSPVirtualRegisters;
namespace I=GMMUInvalidate;
static_assert(V::Base==(0?NV_VIRTUAL_FUNCTION_FULL_PHYS_OFFSET),"PF virtual-register base");
static_assert(I::PDBRegister==NV_VIRTUAL_FUNCTION_PRIV_MMU_INVALIDATE_PDB,"PDB virtual offset");
static_assert(I::UpperRegister==NV_VIRTUAL_FUNCTION_PRIV_MMU_INVALIDATE_UPPER_PDB,"upper virtual offset");
static_assert(I::CommandRegister==NV_VIRTUAL_FUNCTION_PRIV_MMU_INVALIDATE,"command virtual offset");
unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"check failed at line %d\n",__LINE__);std::exit(1);}}while(0)
struct Operation{unsigned write,address,value;};
struct Raw {
  std::vector<Operation> ops;
  unsigned pdb=0,upper=0,command=0,window=0x80173d90,unreadable=0,failWrite=0,writes=0;
  bool corruptPdb=false;
  unsigned read(unsigned address){
    unsigned value=0xffffffff;
    if(address==0x30b0)value=0xbadf1100; // Exact first read captured on live0.24.
    if(address==0xb830b0)value=unreadable?unreadable:(command&0x7fffffff);
    if(address==0xb830a0)value=corruptPdb?pdb^16:pdb;
    if(address==0xb830a4)value=upper;
    if(address==0x1704)value=window;
    ops.push_back({0,address,value});return value;
  }
  bool write(unsigned address,unsigned value){
    ops.push_back({1,address,value});if(++writes==failWrite)return false;
    if(address==0xb830a0)pdb=value;
    else if(address==0xb830a4)upper=value;
    else if(address==0xb830b0)command=value;
    else if(address==0x1704)window=value;
    else return false;
    return true;
  }
};
struct IO {
  Raw raw;bool translated=true;uint64_t clock=0,stride=1000;
  uint64_t now(){return clock+=stride;}
  bool read(unsigned a,unsigned &v){if(translated)return V::read(raw,a,v);v=raw.read(a);return true;}
  bool write(unsigned a,unsigned v){return translated?V::write(raw,a,v):raw.write(a,v);}
};
int main(int argc,char **argv){
  CHECK(argc==3);
  std::array<unsigned char,512> captured{};std::ifstream input(argv[1],std::ios::binary);
  input.read(reinterpret_cast<char*>(captured.data()),512);CHECK(input.gcount()==512&&input.peek()==EOF);
  uint64_t capturedValue=0;for(unsigned i=0;i<8;++i)capturedValue|=uint64_t(captured[37*8+i])<<(i*8);
  CHECK(capturedValue==0xbadf1100);
  const std::array<unsigned,4> logical{{0x1704,0x30a0,0x30a4,0x30b0}};
  const std::array<unsigned,4> physical{{0x1704,0xb830a0,0xb830a4,0xb830b0}};
  for(unsigned i=0;i<4;++i){unsigned out=0;CHECK(V::physical(logical[i],out)&&out==physical[i]);}
  for(unsigned center:logical)for(unsigned a=center-16;a<center+17;++a){
    bool allowed=false;unsigned expected=0;
    for(unsigned i=0;i<4;++i)if(a==logical[i]){allowed=true;expected=physical[i];}
    unsigned out=0xdeadbeef;CHECK(V::physical(a,out)==allowed);CHECK(out==(allowed?expected:0xdeadbeef));
  }
  Raw rejected;
  for(unsigned a:std::array<unsigned,9>{{0,0x1700,0x30a8,0xb830a0,0xb830a4,0xb830b0,0xbb0090,0xfffffffc,0xffffffff}}){
    unsigned v=123;CHECK(!V::read(rejected,a,v)&&v==123);CHECK(!V::write(rejected,a,0));CHECK(rejected.ops.empty());
  }
  I::Preconditions pre;pre.pageTablesAccepted=pre.childrenVerified=pre.parentVerified=pre.exclusiveOwner=true;
  IO legacy;legacy.translated=false;I::Result failed;
  CHECK(!I::run(legacy,pre,failed));CHECK(failed.failure==I::Failure::Unreadable&&failed.lastValue==capturedValue);
  CHECK(failed.reads==1&&failed.writes==0&&!failed.commandAttempted&&legacy.raw.ops.size()==1);
  IO corrected;I::Result good;CHECK(I::run(corrected,pre,good));
  CHECK(good.passed&&good.completed&&good.commandAttempted&&good.reads==4&&good.writes==3);
  const std::array<Operation,7> expected{{{0,0xb830b0,0},{1,0xb830a0,0x10020},{1,0xb830a4,0},
    {0,0xb830a0,0x10020},{0,0xb830a4,0},{1,0xb830b0,0x80000041},{0,0xb830b0,0x41}}};
  CHECK(corrected.raw.ops.size()==expected.size());
  for(unsigned i=0;i<expected.size();++i){const auto &a=corrected.raw.ops[i],&b=expected[i];CHECK(a.write==b.write&&a.address==b.address&&a.value==b.value);}
  IO corrupt;corrupt.raw.corruptPdb=true;I::Result badPdb;CHECK(!I::run(corrupt,pre,badPdb));
  CHECK(badPdb.failure==I::Failure::PDBReadback&&!badPdb.commandAttempted&&badPdb.writes==2);
  IO inaccessible;inaccessible.raw.unreadable=0xbadf1100;I::Result badRead;CHECK(!I::run(inaccessible,pre,badRead));
  CHECK(badRead.failure==I::Failure::Unreadable&&badRead.writes==0&&!badRead.commandAttempted);
  for(unsigned n=1;n<=3;++n){IO broken;broken.raw.failWrite=n;I::Result r;CHECK(!I::run(broken,pre,r));
    CHECK(r.failure==I::Failure::Write&&r.writes==n&&r.commandAttempted==(n==3));}
  IO timeout;timeout.stride=I::MaxNanoseconds;I::Result expired;CHECK(!I::run(timeout,pre,expired));
  CHECK(expired.failure==I::Failure::Deadline&&timeout.raw.ops.empty());
  Raw window;unsigned original=0;CHECK(V::read(window,0x1704,original)&&original==0x80173d90);
  CHECK(V::write(window,0x1704,0)&&V::write(window,0x1704,original));
  CHECK(window.ops.size()==3&&window.window==original);for(const auto &o:window.ops)CHECK(o.address==0x1704);
  std::ofstream trace(argv[2],std::ios::binary);CHECK(bool(trace));
  for(const auto &o:corrected.raw.ops)for(unsigned v:std::array<unsigned,3>{{o.write,o.address,o.value}})
    for(unsigned i=0;i<4;++i)trace.put(static_cast<char>((v>>(i*8))&255));
  trace.close();CHECK(bool(trace));
  std::printf("{\"passed\":true,\"hardware_accessed\":false,\"scenarios\":9,\"checks\":%u,\"trace_operations\":7,\"physical_command_address\":%u,\"live_fault_reproduced\":true,\"compute_verified\":false,\"metal_verified\":false}\n",checks,0xb830b0);
}
