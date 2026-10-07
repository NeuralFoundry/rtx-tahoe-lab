#include "ChannelMemory.hpp"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
namespace M=ChannelMemory;namespace L=GMMULeaves;namespace C=ChannelCodec;namespace I=GMMUInvalidate;
using Bytes=std::vector<unsigned char>;
static unsigned long long scenarios=0,checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"scenario%llu line%d: %s\n",scenarios,__LINE__,#x);std::abort();}}while(0)
static Bytes oldImage(){Bytes out(12288);L::write64(out.data(),0x100322);L::write64(out.data()+4096,0x100422);L::write64(out.data()+9216,0x1122334455667788ULL);for(unsigned i=0;i<16;++i)out[12272+i]=static_cast<unsigned char>(i);return out;}
static C::Plan tinyPlan(){Bytes gr(1664);for(unsigned kind:{0U,16U,17U,18U,19U,20U,23U,24U}){C::R::put32(gr.data()+kind*8,4096);C::R::put32(gr.data()+kind*8+4,4096);}C::Plan p;CHECK(C::plan(gr.data(),1664,p));return p;}
struct Op {unsigned address,bytes;bool write,reg;};
struct Sim {
  Bytes memory=Bytes(0x2000000-unsigned(L::OldBase),0x44),old=oldImage(),ringImage=Bytes(8192),children=Bytes(L::MaxChildBytes),scratch=Bytes(4096);
  C::Plan active;M::Result ringResult;std::vector<Op> ops;
  bool owned=true,ringClaimed=false,contextClaimed=false,stuck=false,regMismatch=false,cleanupFail=false;
  unsigned window=0x80173d90,pdb=0,upper=0,commands=0,clockCalls=0,failAt=0,corruptAt=0,ownerAt=0,clockAt=0,deadlineAt=0,ioCalls=0;
  M::U64 time=100;bool uncertain=false;
  Sim(){std::memcpy(memory.data(),old.data(),old.size());}
  bool ready(){return owned;}
  bool claimRing(){if(ringClaimed)return false;ringClaimed=true;return true;}
  bool claimContexts(const C::Plan &p){if(contextClaimed||!C::planValid(p))return false;active=p;contextClaimed=true;return true;}
  M::U64 nowNs(){++clockCalls;if(clockAt==clockCalls)return 0;if(deadlineAt==clockCalls)time+=M::BudgetNs;time+=10;return time;}
  bool op(unsigned address,unsigned bytes,bool write,bool reg){
    ++ioCalls;ops.push_back({address,bytes,write,reg});if(ioCalls==ownerAt)owned=false;return ioCalls!=failAt;
  }
  bool readMemory(unsigned address,unsigned char *out,unsigned bytes){
    CHECK(window==0&&bytes>0&&bytes<=4096&&!(address&3)&&!(bytes&3)&&address>=L::OldBase&&address+bytes<=0x2000000);
    if(!op(address,bytes,false,false))return false;std::memcpy(out,memory.data()+address-L::OldBase,bytes);
    if(ioCalls==corruptAt)out[0]^=1;return true;
  }
  bool writeMemory(unsigned address,const unsigned char *data,unsigned bytes){
    CHECK(window==0&&bytes>0&&bytes<=4096&&!(address&3)&&!(bytes&3)&&address>=L::OldBase&&address+bytes<=0x2000000);
    const bool pass=op(address,bytes,true,false);
    if(contextClaimed){
      bool backing=false;for(const auto &b:active.buffers)if(address>=b.physical&&address+bytes<=b.physical+b.allocated)backing=true;
      const bool childrenWrite=address>=L::NewBase+8192&&address+bytes<=L::LeaseEnd;
      const bool link=bytes==4&&address>=L::NewBase+16&&address+4<=L::NewBase+4096;
      CHECK(backing||childrenWrite||link); // no root, ring-PTE or ring backing writes
    }else{
      CHECK((address>=M::RingStart&&address+bytes<=M::RingStart+M::RingBytes)||(address>=L::NewBase&&address+bytes<=L::NewBase+8192)||
            (bytes==4&&(address==L::OldBase+L::ParentOffset||address==L::OldBase+L::ParentOffset+4)));
    }
    if(pass||uncertain)std::memcpy(memory.data()+address-L::OldBase,data,bytes);return pass;
  }
  bool readRegister(unsigned address,unsigned &value){
    if(!op(address,4,false,true))return false;
    if(address==M::Window)value=regMismatch?1:window;else if(address==I::PDBRegister)value=pdb;else if(address==I::UpperRegister)value=upper;
    else if(address==I::CommandRegister)value=stuck?0x80000000:0;else {CHECK(false);return false;}
    if(ioCalls==corruptAt)value=0xbad0acff;return true;
  }
  bool writeRegister(unsigned address,unsigned value){
    if(cleanupFail&&address==M::Window&&value==0x80173d90)return false;
    const bool pass=op(address,4,true,true);if(!pass&&!uncertain)return false;
    if(address==M::Window){CHECK(value==0||value==0x80173d90);window=value;}
    else if(address==I::PDBRegister){CHECK(value==I::PDBValue);pdb=value;}
    else if(address==I::UpperRegister){CHECK(value==0);upper=value;}
    else if(address==I::CommandRegister){CHECK(value==I::CommandValue);++commands;}
    else CHECK(false);return pass;
  }
};
static M::Result ring(Sim &io){++scenarios;M::ring(io,io.old.data(),io.ringImage.data(),io.scratch.data(),io.ringResult);const auto &r=io.ringResult;
  CHECK(r.operations<=M::MaxOperations&&r.linksPublished<=1&&r.childBytes<=8192);if(r.passed){CHECK(r.backingVerified&&r.childrenVerified&&r.invalidation.passed&&r.zeroedBytes==M::RingBytes&&r.verifiedChildBytes==8192);}
  return r;
}
static M::Result contexts(Sim &io,const C::Plan &plan){++scenarios;M::Result r;M::contexts(io,plan,io.old.data(),io.ringImage.data(),io.children.data(),io.scratch.data(),io.ringResult,r);
  CHECK(r.operations<=M::MaxOperations&&r.childBytes<=L::MaxChildBytes);if(r.passed){CHECK(r.backingVerified&&r.childrenVerified&&r.invalidation.passed&&r.zeroedBytes==plan.backingBytes&&r.verifiedBackingBytes==plan.backingBytes);}
  return r;
}
static void resetCounters(Sim &io){io.ops.clear();io.ioCalls=io.clockCalls=0;io.failAt=io.corruptAt=io.ownerAt=io.clockAt=io.deadlineAt=0;}
int main(int argc,char **argv){
  CHECK(argc==2);std::ifstream f(argv[1],std::ios::binary);CHECK(bool(f));Bytes packet(std::istreambuf_iterator<char>(f),{});CHECK(packet.size()==4096);
  C::Plan full;CHECK(C::plan(packet.data()+104,1664,full));const auto small=tinyPlan();
  Sim baseline;CHECK(ring(baseline).passed);const auto ringOps=baseline.ops;const auto ringClocks=baseline.clockCalls;
  CHECK(L::read64(baseline.memory.data()+L::ParentOffset)==L::ParentValue);
  // Every context backing byte is zero; every hole and all existing ring backing remain unchanged.
  const auto before=baseline.memory;resetCounters(baseline);const auto result=contexts(baseline,full);CHECK(result.passed);
  for(size_t off=0;off<baseline.memory.size();++off){const auto address=L::OldBase+off;bool inBacking=false;
    for(const auto &b:full.buffers)if(address>=b.physical&&address<b.physical+b.allocated)inBacking=true;
    if(inBacking)CHECK(baseline.memory[off]==0);
    else if(address<L::NewBase||address>=L::NewBase+result.childBytes)CHECK(baseline.memory[off]==before[off]);
  }
  CHECK(std::memcmp(baseline.memory.data()+L::NewBase-L::OldBase,baseline.children.data(),result.childBytes)==0);
  CHECK(std::memcmp(baseline.memory.data()+L::NewBase-L::OldBase+4096,before.data()+L::NewBase-L::OldBase+4096,4096)==0);
  const auto fullCalls=baseline.ioCalls;M::restoreWindow(baseline,baseline.ringResult);CHECK(baseline.ringResult.windowRestored&&baseline.window==0x80173d90);
  // The aperture-valid word of each new dual entry must be its final write.
  for(unsigned group=1;group<256;++group){std::vector<unsigned> offsets;
    for(const auto &op:baseline.ops)if(op.write&&!op.reg&&op.address>=L::NewBase+group*16&&op.address<L::NewBase+(group+1)*16)offsets.push_back(op.address-unsigned(L::NewBase)-group*16);
    if(!offsets.empty())CHECK(offsets==std::vector<unsigned>({0,4,12,8}));
  }
  for(unsigned i=1;i<=ringOps.size();++i){Sim io;io.failAt=i;io.uncertain=true;CHECK(!ring(io).passed);CHECK(io.commands<=1);}
  for(unsigned i=1;i<=ringClocks;++i){Sim io;io.clockAt=i;const auto r=ring(io);if(i==1)CHECK(r.passed);else CHECK(!r.passed);}
  for(unsigned i=2;i<=ringClocks;++i){Sim io;io.deadlineAt=i;CHECK(!ring(io).passed);}
  for(unsigned i=1;i<=ringOps.size();++i)if(!ringOps[i-1].write){Sim io;io.corruptAt=i;const auto r=ring(io);if(ringOps[i-1].reg||ringOps[i-1].address<L::NewBase||i>20)CHECK(!r.passed);}
  Sim setup;CHECK(ring(setup).passed);resetCounters(setup);CHECK(contexts(setup,small).passed);const auto contextOps=setup.ops;const auto contextClocks=setup.clockCalls;
  for(unsigned i=1;i<=contextOps.size();++i){Sim io;CHECK(ring(io).passed);resetCounters(io);io.failAt=i;io.uncertain=true;CHECK(!contexts(io,small).passed);}
  for(unsigned i:{2U,5U,20U,contextClocks}){Sim io;CHECK(ring(io).passed);resetCounters(io);io.clockAt=i;CHECK(!contexts(io,small).passed);}
  for(unsigned i:{2U,5U,20U,contextClocks}){Sim io;CHECK(ring(io).passed);resetCounters(io);io.deadlineAt=i;CHECK(!contexts(io,small).passed);}
  {Sim io;io.old[0]^=1;CHECK(!ring(io).passed&&io.ioCalls==0);}
  {Sim io;io.memory[0]^=1;CHECK(!ring(io).passed&&!io.ringResult.modified);}
  {Sim io;C::R::put32(io.memory.data()+L::NewBase-L::OldBase,0xbad0acff);CHECK(!ring(io).passed&&!io.ringResult.modified);}
  {Sim io;io.ringClaimed=true;CHECK(!ring(io).passed&&io.ioCalls==0);}
  {Sim io;io.owned=false;CHECK(!ring(io).passed&&io.ioCalls==0);}
  {Sim io;CHECK(ring(io).passed);resetCounters(io);io.ringResult.windowRestored=true;CHECK(!contexts(io,small).passed&&io.ioCalls==0);}
  {Sim io;CHECK(ring(io).passed);resetCounters(io);io.memory[9216]^=1;const auto r=contexts(io,small);CHECK(!r.passed&&!r.modified);}
  {Sim io;CHECK(ring(io).passed);resetCounters(io);io.memory[L::NewBase-L::OldBase+4096]^=1;const auto r=contexts(io,small);CHECK(!r.passed&&!r.modified);}
  {Sim io;CHECK(ring(io).passed);resetCounters(io);C::R::put32(io.memory.data()+small.buffers[8].physical-L::OldBase,0xbadf0000);const auto r=contexts(io,small);CHECK(!r.passed&&!r.modified);}
  {Sim io;CHECK(ring(io).passed);io.cleanupFail=true;M::restoreWindow(io,io.ringResult);CHECK(!io.ringResult.passed&&!io.ringResult.windowRestored);}
  {Sim io;CHECK(ring(io).passed);resetCounters(io);io.stuck=true;CHECK(!contexts(io,small).passed);}
  std::printf("{\"passed\":true,\"scenarios\":%llu,\"checks\":%llu,\"ring_io_calls\":%zu,\"full_context_io_calls\":%u,\"fault_context_io_calls\":%zu,\"hardware_accessed\":false}\n",scenarios,checks,ringOps.size(),fullCalls,contextOps.size());
}
