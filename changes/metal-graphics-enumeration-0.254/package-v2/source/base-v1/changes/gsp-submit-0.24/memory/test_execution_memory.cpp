#include "ExecutionMemory.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
namespace E=ExecutionMemory;namespace P=ExecutionPlan;namespace C=ChannelCodec;namespace L=GMMULeaves;namespace M=ChannelMemory;namespace R=GSPComputePrep;
using Bytes=std::vector<unsigned char>;
static unsigned scenarios=0,checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"scenario%u line%u: %s\n",scenarios,unsigned(__LINE__),#x);std::exit(1);}}while(0)
static Bytes load(const char *path){std::ifstream f(path,std::ios::binary);CHECK(bool(f));return Bytes(std::istreambuf_iterator<char>(f),{});}
static void save(const std::string &path,const Bytes &b){std::ofstream f(path,std::ios::binary);CHECK(bool(f));f.write(reinterpret_cast<const char*>(b.data()),b.size());f.close();CHECK(bool(f));}
struct Sim {
  Bytes root=Bytes(12288),children=Bytes(L::MaxChildBytes),rootScratch=Bytes(12288),expected=Bytes(L::MaxChildBytes),fixedImage=Bytes(L::MaxChildBytes),fullImage=Bytes(L::MaxChildBytes),scratch=Bytes(4096);
  Bytes vRoot,vTables,backing=Bytes(0x4000000-unsigned(P::Base),0xa5);
  C::Plan golden;P::Plan plan;E::Storage storage;E::Result fixed,contexts;
  unsigned window=0x80173d90,pdb=0,upper=0,pending=0,ioCalls=0,failAt=0,loseAt=0,reads=0,writes=0,fixedInspected=0,contextsInspected=0;
  unsigned clocks=0,clockFault=0,timeoutAt=0,firstFixedWrite=0,firstContextWrite=0;
  bool owned=true,fixedClaimed=false,contextsClaimed=false,stickyInvalidate=false;unsigned long long time=100;
  explicit Sim(const Bytes &gr){
    ++scenarios;CHECK(gr.size()==4096);CHECK(C::plan(gr.data()+104,1664,golden));CHECK(P::make(gr.data()+104,1664,golden,plan));
    L::write64(root.data(),0x100322);L::write64(root.data()+4096,0x100422);L::write64(root.data()+8192+128*8,0x1122334455667788ULL);
    L::Range ranges[10];L::Result result;CHECK(C::mappingRanges(golden,ranges));CHECK(L::build(root.data(),root.size(),ranges,10,children.data(),children.size(),result));
    L::write64(root.data()+L::ParentOffset,L::ParentValue);
    storage={root.data(),children.data(),unsigned(result.childBytes),rootScratch.data(),expected.data(),fixedImage.data(),fullImage.data(),scratch.data()};
    vRoot=root;vTables=children;std::fill(vTables.begin()+storage.goldenBytes,vTables.end(),static_cast<unsigned char>(0x5a));
  }
  bool ready(){return owned;}
  unsigned long long nowNs(){++clocks;if(clocks==clockFault)return 0;time+=100;if(clocks==timeoutAt)time+=M::BudgetNs;return time;}
  bool claimFixed(){if(fixedClaimed)return false;fixedClaimed=true;return true;}
  bool claimContexts(const P::Plan &p){if(contextsClaimed||!E::fixedReady(fixed,storage.goldenBytes)||!P::valid(p,golden))return false;contextsClaimed=true;return true;}
  bool operation(){++ioCalls;if(ioCalls==loseAt)owned=false;return ioCalls!=failAt;}
  unsigned char *address(unsigned addr,unsigned n){
    CHECK(n&&n<=4096&&!(addr&3)&&!(n&3));
    if(addr>=L::OldBase&&addr-L::OldBase+n<=vRoot.size())return vRoot.data()+addr-unsigned(L::OldBase);
    if(addr>=L::NewBase&&addr-L::NewBase+n<=vTables.size())return vTables.data()+addr-unsigned(L::NewBase);
    if(addr>=P::Base&&addr-P::Base+n<=backing.size())return backing.data()+addr-unsigned(P::Base);
    CHECK(false);return nullptr;
  }
  bool readMemory(unsigned addr,unsigned char *out,unsigned n){
    CHECK(window==0);++reads;if(!operation())return false;std::memcpy(out,address(addr,n),n);
    if(!contextsClaimed&&!firstFixedWrite&&addr>=P::Base&&addr+n<=P::FixedEnd)fixedInspected+=n;
    if(contextsClaimed&&!firstContextWrite&&addr>=P::ContextBase)contextsInspected+=n;
    return true;
  }
  bool writeMemory(unsigned addr,const unsigned char *data,unsigned n){
    CHECK(window==0);++writes;const bool good=operation();
    if(!contextsClaimed){
      if(addr>=P::Base&&addr+n<=P::FixedEnd){
        if(!firstFixedWrite){firstFixedWrite=ioCalls;CHECK(fixedInspected==E::FixedBytes);}
        CHECK(std::all_of(data,data+n,[](unsigned char v){return v==0;}));
      }else{
        CHECK(n==4&&addr>=L::NewBase+4096+8&&addr<L::NewBase+4096+32);
        CHECK(std::all_of(backing.begin(),backing.begin()+E::FixedBytes,[](unsigned char v){return v==0;}));
        CHECK(std::memcmp(data,fixedImage.data()+addr-unsigned(L::NewBase),n)==0);
        if((addr&7)==0)CHECK(R::get32(address(addr+4,4))==R::get32(fixedImage.data()+addr+4-unsigned(L::NewBase)));
      }
    }else{
      CHECK(!(addr>=P::Base&&addr<P::FixedEnd)); // Never reset the live ring/USERD.
      bool privateRange=false;for(const auto &b:plan.buffers)if(addr>=b.physical&&addr+n<=b.physical+b.allocated)privateRange=true;
      if(privateRange){
        if(!firstContextWrite){firstContextWrite=ioCalls;CHECK(contextsInspected==plan.backingBytes);}
        CHECK(std::all_of(data,data+n,[](unsigned char v){return v==0;}));
      }else{
        CHECK(addr>=L::NewBase&&addr+n<=L::NewBase+contexts.childBytes);
        CHECK(std::memcmp(data,fullImage.data()+addr-unsigned(L::NewBase),n)==0);
        if(addr<L::NewBase+4096){
          CHECK(n==4);CHECK(std::memcmp(vTables.data()+storage.goldenBytes,fullImage.data()+storage.goldenBytes,contexts.childBytes-storage.goldenBytes)==0);
          const unsigned group=(addr-unsigned(L::NewBase))/16;
          CHECK(L::read64(fixedImage.data()+group*16)==0&&L::read64(fixedImage.data()+group*16+8)==0);
          if((addr&15)==8){CHECK(L::read64(address(addr-8,8))==0x20);CHECK(R::get32(address(addr+4,4))==0);}
        }else CHECK(addr>=L::NewBase+storage.goldenBytes&&n==4096);
      }
    }
    std::memcpy(address(addr,n),data,n);return good; // Partial/visible failure model.
  }
  bool readRegister(unsigned addr,unsigned &value){
    ++reads;if(!operation())return false;
    if(addr==M::Window)value=window;
    else if(addr==GMMUInvalidate::PDBRegister)value=pdb;
    else if(addr==GMMUInvalidate::UpperRegister)value=upper;
    else if(addr==GMMUInvalidate::CommandRegister){value=pending?0x80000041:0;if(pending&&!stickyInvalidate)--pending;}
    else{CHECK(false);return false;}return true;
  }
  bool writeRegister(unsigned addr,unsigned value){
    ++writes;const bool good=operation();
    if(addr==M::Window){CHECK(value==0||value==0x80173d90);window=value;}
    else if(addr==GMMUInvalidate::PDBRegister){CHECK(value==0x10020);pdb=value;}
    else if(addr==GMMUInvalidate::UpperRegister){CHECK(value==0);upper=value;}
    else if(addr==GMMUInvalidate::CommandRegister){CHECK(value==0x80000041&&pdb==0x10020&&!upper);pending=2;}
    else{CHECK(false);return false;}return good;
  }
  bool runFixed(){return E::fixed(*this,golden,3,storage,fixed);}
  bool runContexts(){return E::contexts(*this,golden,plan,storage,fixed,contexts);}
  void channelBecameLive(){
    CHECK(E::fixedReady(fixed,storage.goldenBytes));backing[0x800+0x8c]=0x73;backing[0x1000+16]=0x29;
    ioCalls=reads=writes=0;clocks=0;
  }
};
static void walk(const Sim &io,const L::Range *ranges,unsigned count,unsigned childBytes){
  for(unsigned i=0;i<count;++i)for(unsigned long long off=0;off<ranges[i].bytes;off+=4096)for(unsigned edge:{0U,4095U}){
    bool mapped=false;uint64_t pa=0;CHECK(L::walk(io.vRoot.data(),io.vRoot.size(),io.vTables.data(),childBytes,ranges[i].va+off+edge,mapped,pa));CHECK(mapped&&pa==ranges[i].pa+off+edge);
  }
}
int main(int argc,char **argv){
  CHECK(argc==3);const auto gr=load(argv[1]);Sim good(gr);CHECK(good.runFixed());CHECK(E::fixedReady(good.fixed,good.storage.goldenBytes));
  const unsigned fixedCalls=good.ioCalls;CHECK(good.fixed.zeroedBytes==36864&&good.fixed.linksPublished==3);
  CHECK(good.vRoot==good.root);CHECK(std::memcmp(good.vTables.data(),good.fixedImage.data(),good.storage.goldenBytes)==0);
  save(std::string(argv[2])+"/fixed-children.bin",Bytes(good.fixedImage.begin(),good.fixedImage.begin()+good.storage.goldenBytes));
  good.channelBecameLive();const Bytes liveFixed(good.backing.begin(),good.backing.begin()+E::FixedBytes);
  CHECK(good.runContexts());const unsigned contextCalls=good.ioCalls;
  CHECK(good.contexts.passed&&good.contexts.childBytes==40960&&good.contexts.linksPublished==1&&good.contexts.zeroedBytes==1003520&&good.contexts.invalidation.passed);
  CHECK(std::equal(liveFixed.begin(),liveFixed.end(),good.backing.begin()));CHECK(good.vRoot==good.root);
  CHECK(std::memcmp(good.vTables.data(),good.fullImage.data(),40960)==0);
  L::Range oldRanges[10],newRanges[6];CHECK(C::mappingRanges(good.golden,oldRanges)&&P::mappings(good.plan,good.golden,newRanges));walk(good,oldRanges,10,40960);walk(good,newRanges,6,40960);
  for(const auto &b:good.plan.buffers)CHECK(std::all_of(good.backing.begin()+b.physical-P::Base,good.backing.begin()+b.physical-P::Base+b.allocated,[](unsigned char v){return v==0;}));
  save(std::string(argv[2])+"/full-children.bin",Bytes(good.fullImage.begin(),good.fullImage.begin()+40960));
  M::restoreWindow(good,good.fixed);CHECK(good.fixed.windowRestored&&good.window==0x80173d90);
  // Interrupt every real read/write/register operation, including writes which
  // may already be visible. No failed stage can report passed.
  for(unsigned at=1;at<=fixedCalls;++at){Sim io(gr);io.failAt=at;CHECK(!io.runFixed());CHECK(!io.fixed.passed);CHECK(io.vRoot==io.root);}
  for(unsigned at=1;at<=contextCalls;++at){Sim io(gr);CHECK(io.runFixed());io.channelBecameLive();const Bytes before(io.backing.begin(),io.backing.begin()+E::FixedBytes);
    io.failAt=at;CHECK(!io.runContexts());CHECK(!io.contexts.passed);CHECK(std::equal(before.begin(),before.end(),io.backing.begin()));CHECK(io.vRoot==io.root);
  }
  for(unsigned phase=0;phase<2;++phase)for(unsigned mode=0;mode<8;++mode){Sim io(gr);
    if(phase){CHECK(io.runFixed());io.channelBecameLive();}
    if(mode==0)io.owned=false;
    if(mode==1)io.clockFault=3;
    if(mode==2)io.timeoutAt=3;
    if(mode==3)io.loseAt=7;
    if(mode==4)io.stickyInvalidate=true;
    if(mode==5)io.vRoot[8]^=1;
    if(mode==6)io.vTables[16]^=1;
    if(mode==7)R::put32(io.backing.data()+(phase?unsigned(P::ContextBase-P::Base):0),0xbad0acff);
    CHECK(!(phase?io.runContexts():io.runFixed()));CHECK(!(phase?io.contexts.passed:io.fixed.passed));
  }
  {Sim io(gr);io.storage.scratch=io.root.data();CHECK(!io.runFixed());CHECK(io.fixed.failure==M::Failure::Storage&&io.ioCalls==0);}
  {Sim io(gr);io.children[48]^=1;CHECK(!io.runFixed());CHECK(!io.fixedClaimed&&io.ioCalls==0);}
  {Sim io(gr);CHECK(io.runFixed());io.channelBecameLive();io.fixedImage[4104]^=1;CHECK(!io.runContexts());CHECK(!io.contextsClaimed&&io.ioCalls==0);}
  {Sim io(gr);CHECK(io.runFixed());io.channelBecameLive();io.fixed.passed=false;CHECK(!io.runContexts());CHECK(!io.contextsClaimed&&io.ioCalls==0);}
  {Sim io(gr);CHECK(io.runFixed());const auto before=io.backing;CHECK(!io.runFixed());CHECK(io.backing==before&&io.fixed.failure==M::Failure::Replay);}
  {Sim io(gr);CHECK(io.runFixed());io.channelBecameLive();CHECK(io.runContexts());const auto before=io.backing;CHECK(!io.runContexts());CHECK(io.backing==before&&io.contexts.failure==M::Failure::Replay);}
  {Sim io(gr);CHECK(io.runFixed());io.channelBecameLive();Bytes fresh(gr.begin()+104,gr.begin()+104+1664);R::put32(fresh.data(),0x401000-2*16384-0x40000);
    CHECK(P::make(fresh.data(),1664,io.golden,io.plan));CHECK(!io.runContexts());CHECK(io.contexts.failure==M::Failure::Plan&&!io.contextsClaimed&&io.ioCalls==0);
  }
  std::printf("{\"passed\":true,\"scenarios\":%u,\"checks\":%u,\"fixed_io_operations\":%u,\"context_io_operations\":%u,\"fixed_zeroed_bytes\":36864,\"private_zeroed_bytes\":1003520,\"child_bytes\":40960,\"live_ring_preserved\":true,\"hardware_accessed\":false,\"gpu_translation_verified\":false,\"compute_verified\":false,\"metal_verified\":false}\n",scenarios,checks,fixedCalls,contextCalls);
}
