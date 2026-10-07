#include "driver/SEC2StageProtocol.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

using namespace SEC2Stage;
static unsigned scenarios=0;
#define REQUIRE(x) do { if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::abort(); } } while(false)

struct Fake {
  unsigned regs[Count]={}, reads[Count]={}, writes[Count]={};
  U64 pages[15]={};
  std::vector<unsigned> image,dmemory,imemory,tags;
  unsigned pci=0,deviceReads=0,verifyCalls=0,submitted=0,resets=0,delayTotal=0,readAfterDMA=0;
  unsigned malformed=0,forbidden=0,absentReads=0,imemSeen=0,dmemSeen=0;
  bool riscv=false,board=true,environment=true,sync=true;
  bool memoryEnable=true,masterEnable=true,masterDisable=true,memoryDisable=true;
  bool initialPending=false,finalPending=false,finalStatusUnreadable=false;
  unsigned failVerify=0,failReset=0,scrubReset=0,dmaTimeout=0,completionUnreadable=0;
  int badRead=-1,badWrite=-1; unsigned badReadAt=1,badWriteAt=1;
  int canaryFault=-1,dmemFault=-1; bool ignoreDmemDMA=false,stickyDMA=false;
  int changedPage=-1; unsigned changeAtSubmitted=0;
  explicit Fake(bool rv=false): image(AllocationBytes/4),dmemory(DmemWords),imemory(ImemBytes/4),tags(ImemBlocks),riscv(rv) {
    regs[HWCFG2]=0x80000000U|(rv ? 0x400U : 0);
    regs[HWCFG]=ImemBlocks|(DmemBlocks<<9); regs[CPUCTL]=0x10; regs[DMACMD]=2;
    regs[BCR]=rv ? 0x11U : 0; regs[TRANSCFG]=0x20000004U; regs[FBIFCTL]=0x100U;
    for(unsigned i=0;i<15;++i) pages[i]=0x200000ULL+((i*7)%15)*8192;
    for(unsigned i=0;i<image.size();++i) image[i]=0x53454332U^(i*2654435761U);
  }
  unsigned command() { return pci; }
  void setMemory(bool enabled) {
    if(enabled ? memoryEnable : memoryDisable) pci=enabled ? pci|2U : pci&~2U;
  }
  void setMaster(bool enabled) {
    if(enabled ? masterEnable : masterDisable) pci=enabled ? pci|4U : pci&~4U;
  }
  unsigned deviceStatus() {
    ++deviceReads;
    if(deviceReads==1) return initialPending ? 0x20U : 0;
    if(finalStatusUnreadable) return 0xffff;
    return finalPending ? 0x20U : 0;
  }
  bool imageMatchesBoard() { return board; }
  bool environmentReady() { REQUIRE(pci==2); return environment; }
  bool verifyImage() { return ++verifyCalls!=failVerify; }
  bool synchronizeImage() { return sync; }
  unsigned imageWord(unsigned offset) { REQUIRE(!(offset&3U) && offset<ImageBytes); return image[offset/4]; }
  bool imageAddress(unsigned offset,U64 &out) {
    if(offset>=AllocationBytes) return false;
    out=pages[offset/4096]+(offset&4095U);
    if(int(offset/4096)==changedPage && submitted>=changeAtSubmitted) out+=4096;
    return true;
  }
  void delayUs(unsigned us) { REQUIRE(us<=100); delayTotal+=us; REQUIRE(delayTotal<1000000); }
  unsigned read(Reg reg) {
    REQUIRE(reg<Count); REQUIRE(pci&2U);
    ++reads[reg];
    if(!riscv && (reg==BCR || reg==RISCVCPU)) { ++absentReads; return 0xbadf0000U; }
    if(int(reg)==badRead && reads[reg]==badReadAt) return 0xbadf0000U;
    if(reg==DMACMD && submitted) {
      ++readAfterDMA;
      if(submitted==completionUnreadable && readAfterDMA==2) return 0xbadf0000U;
    }
    if(reg==DMEMD) {
      REQUIRE(regs[DMEMC]%4==0 && regs[DMEMC]/4<DmemWords);
      const unsigned word=regs[DMEMC]/4;
      return dmemory[word]^((submitted==0 && int(word)==canaryFault) ? 1U : 0U);
    }
    return regs[reg];
  }
  bool translate(U64 address,unsigned &offset) {
    for(unsigned i=0;i<15;++i) if(address>=pages[i] && address<pages[i]+4096) {
      offset=i*4096+unsigned(address-pages[i]); return true;
    }
    return false;
  }
  void write(Reg reg,unsigned value) {
    REQUIRE(reg<Count); REQUIRE(pci&2U); ++writes[reg];
    if(reg==CPUCTL || reg==RISCVCPU || reg==HWCFG || reg==HWCFG2 ||
       (reg==BCR && (!riscv || value!=0))) { ++forbidden; return; }
    if(int(reg)==badWrite && writes[reg]==badWriteAt) return;
    if(reg==Engine) {
      if(value&1U) { ++resets; if(resets==failReset) return; }
      else {
        regs[CPUCTL]=0x10;
        regs[HWCFG2]=0x80000000U|(riscv ? 0x400U : 0)|(resets==scrubReset ? 0x1000U : 0);
        regs[DMACMD]=stickyDMA && submitted ? 0U : 2U;
        regs[DMACTL]=0; regs[RISCVCPU]=0;
        for(auto &word:dmemory) word=0;
      }
    }
    if(reg==BCR) { regs[BCR]=1; return; }
    if(reg==DMEMD) { REQUIRE(regs[DMEMC]/4<DmemWords); dmemory[regs[DMEMC]/4]=value; return; }
    if(reg==DMACMD) {
      ++submitted; readAfterDMA=0;
      REQUIRE(pci==6); REQUIRE(regs[DMACTL]==0); REQUIRE(regs[FBIFCTL]&0x80U);
      REQUIRE((regs[TRANSCFG]&0x10007U)==5);
      const U64 addr=(((U64(regs[DMABASE1])<<32)|regs[DMABASE])<<8)+regs[FBOFFSET];
      unsigned offset=0;
      if(!translate(addr,offset) || (offset&255U) || offset+256>ImageBytes) { ++malformed; regs[DMACMD]=0; return; }
      const unsigned mem=regs[DMAOFFSET];
      if(value==0x614) {
        REQUIRE(imemSeen<ImemBlocks); REQUIRE(offset==ImemOffset+imemSeen*256);
        REQUIRE(mem==imemSeen*256); REQUIRE(regs[FBOFFSET]==ImemOffset+mem);
        tags[imemSeen]=regs[FBOFFSET]>>8;
        for(unsigned j=0;j<64;++j) imemory[mem/4+j]=image[offset/4+j];
        ++imemSeen;
      } else if(value==0x600) {
        REQUIRE(dmemSeen<DmemBlocks); REQUIRE(offset==DmemOffset+dmemSeen*256);
        REQUIRE(mem==dmemSeen*256); REQUIRE(regs[FBOFFSET]==mem);
        if(!ignoreDmemDMA) for(unsigned j=0;j<64;++j) {
          const unsigned word=mem/4+j;
          dmemory[word]=image[offset/4+j]^(int(word)==dmemFault ? 1U : 0U);
        }
        ++dmemSeen;
      } else { ++malformed; }
      regs[DMACMD]=submitted==dmaTimeout ? 0U : 2U;
      return;
    }
    regs[reg]=value;
  }
};

static void execute(Fake &io,Result &r) {
  run(io,r); ++scenarios;
  REQUIRE(io.forbidden==0 && io.absentReads==0 && io.malformed==0);
  REQUIRE(r.imemCompleted<=r.imemSubmitted && r.dmemCompleted<=r.dmemSubmitted);
  REQUIRE(r.imemSubmitted<=137 && r.dmemSubmitted<=98);
  REQUIRE(!r.passed || (r.staged && r.releaseSafe));
  REQUIRE(!r.releaseSafe || r.commandAfter==0);
  REQUIRE(!r.releaseSafe || !r.anyDMA || (r.quiescent && r.targetsCleared && r.finalVerified));
}
static void success(bool rv) {
  Fake io(rv); auto r=std::unique_ptr<Result>(new Result); execute(io,*r);
  REQUIRE(r->passed && r->releaseSafe && r->staged && r->cpuIntact);
  REQUIRE(r->canaryMatched==6272 && r->dmemReads==6272 && r->dmemMatched==6272);
  REQUIRE(r->imemCompleted==137 && r->dmemCompleted==98 && r->synchronizeCount==1);
  REQUIRE(r->resetCount==2 && io.pci==0);
  REQUIRE(r->initialMask==requiredMask(rv) && r->finalMask==requiredMask(rv));
  REQUIRE(r->initialReads==(rv ? 14U : 12U) && r->finalReads==(rv ? 14U : 12U));
  for(unsigned i=0;i<ImemBlocks;++i) REQUIRE(io.tags[i]==i+1);
  for(unsigned i=0;i<ImemBytes/4;++i) REQUIRE(io.imemory[i]==io.image[(ImemOffset/4)+i]);
  for(unsigned i=0;i<DmemWords;++i) REQUIRE(r->dmem[i]==io.image[DmemOffset/4+i]);
  for(unsigned i=0;i<Blocks;++i) REQUIRE(r->completions[i]==2);
}
template<class Configure> static void failed(Configure configure,bool retain=false) {
  Fake io; configure(io); auto r=std::unique_ptr<Result>(new Result); execute(io,*r);
  REQUIRE(!r->passed); REQUIRE(!retain || !r->releaseSafe);
}

int main() {
  success(false); success(true);
  // Geometry independently checks every physical target, tag, boundary and page.
  {
    Fake io; for(unsigned b=0;b<Blocks;++b) {
      U64 source=0; unsigned base=0,hi=0,mem=0,fb=0,off=0;
      REQUIRE(blockAddress(io.pages,b,source,base,hi,mem,fb,off));
      REQUIRE(source==io.pages[off/4096]+(off%4096));
      REQUIRE((source&4095U)<=3840 && hi==0);
      REQUIRE(mem==(b<137 ? b : b-137)*256);
      REQUIRE(fb==mem+(b<137 ? 256U : 0U));
    }
    U64 source=0; unsigned a=0,b=0,c=0,d=0,e=0;
    REQUIRE(!blockAddress(io.pages,235,source,a,b,c,d,e));
    io.pages[8]=0x1000; REQUIRE(!blockAddress(io.pages,128,source,a,b,c,d,e));
    ++scenarios;
  }
  failed([](Fake &f){ f.board=false; });
  failed([](Fake &f){ f.environment=false; });
  failed([](Fake &f){ f.sync=false; });
  failed([](Fake &f){ f.memoryEnable=false; });
  failed([](Fake &f){ f.masterEnable=false; });
  failed([](Fake &f){ f.pci=6; },true);
  failed([](Fake &f){ f.initialPending=true; });
  failed([](Fake &f){ f.pages[1]=f.pages[0]; });
  failed([](Fake &f){ f.pages[0]=0; });
  failed([](Fake &f){ f.pages[0]=0x200001; });
  failed([](Fake &f){ f.pages[0]=1ULL<<40; });
  failed([](Fake &f){ f.regs[HWCFG]=136|(98<<9); });
  failed([](Fake &f){ f.regs[HWCFG]=137|(97<<9); });
  failed([](Fake &f){ f.regs[Engine]=1; });
  failed([](Fake &f){ f.regs[CPUCTL]=2; });
  {
    Fake io; io.regs[CPUCTL]=0; auto r=std::unique_ptr<Result>(new Result); execute(io,*r);
    REQUIRE(!r->passed && !r->resetAttempted && !r->anyDMA && r->releaseSafe);
    REQUIRE(r->initialMask==requiredMask(false) && r->initial[CPUCTL]==0);
    REQUIRE(r->initialReads==12 && r->resetCount==0 && r->imemSubmitted==0 && r->dmemSubmitted==0);
    REQUIRE(io.writes[Engine]==0 && io.writes[DMEMC]==0 && io.writes[DMEMD]==0 && io.writes[DMACMD]==0);
    REQUIRE(std::strcmp(r->status,"sec2-initial-CPU-not-halted")==0);
  }
  failed([](Fake &f){ f.regs[DMACMD]=0; });
  failed([](Fake &f){ f.regs[DMACTL]=6; });
  failed([](Fake &f){ f.regs[HWCFG2]|=0x1000; });
  for(unsigned i: {1U,137U,138U,235U}) {
    failed([i](Fake &f){ f.dmaTimeout=i; });
    failed([i](Fake &f){ f.completionUnreadable=i; });
  }
  for(int word: {0,4,95,96,1023,6271}) {
    failed([word](Fake &f){ f.canaryFault=word; });
    failed([word](Fake &f){ f.dmemFault=word; });
  }
  failed([](Fake &f){ f.ignoreDmemDMA=true; });
  failed([](Fake &f){ f.changedPage=1; f.changeAtSubmitted=1; });
  failed([](Fake &f){ f.failVerify=2; });
  failed([](Fake &f){ f.failVerify=3; });
  failed([](Fake &f){ f.failVerify=4; },true);
  failed([](Fake &f){ f.failReset=1; });
  failed([](Fake &f){ f.failReset=2; },true);
  failed([](Fake &f){ f.scrubReset=1; });
  failed([](Fake &f){ f.scrubReset=2; },true);
  failed([](Fake &f){ f.finalPending=true; },true);
  failed([](Fake &f){ f.finalStatusUnreadable=true; },true);
  failed([](Fake &f){ f.stickyDMA=true; },true);
  failed([](Fake &f){ f.masterDisable=false; },true);
  failed([](Fake &f){ f.memoryDisable=false; },true);
  // Reject every readable-register fault at the first relevant access.
  for(unsigned reg=0;reg<SnapshotCount;++reg) if(reg!=BCR && reg!=RISCVCPU) {
    failed([reg](Fake &f){ f.badRead=int(reg); });
  }
  // Setup/readback failures and cleanup write failures cannot claim success.
  for(Reg reg: {Engine,FBIFCTL,TRANSCFG,DMABASE,FBOFFSET}) {
    failed([reg](Fake &f){ f.badWrite=int(reg); });
  }
  failed([](Fake &f){ f.badWrite=DMAOFFSET; f.badWriteAt=2; });
  for(Reg reg: {FBIFCTL,DMACTL,TRANSCFG}) {
    failed([reg](Fake &f){ f.badWrite=int(reg); f.badWriteAt=2; },true);
  }
  for(Reg reg: {DMABASE,DMAOFFSET,FBOFFSET}) {
    failed([reg](Fake &f){ f.badWrite=int(reg); f.badWriteAt=Blocks+1; },true);
  }
  // An unreadable final snapshot must retain mappings despite otherwise
  // successful reset, PIO evidence, DMA idle and PCI restoration.
  {
    Fake baseline; auto known=std::unique_ptr<Result>(new Result); execute(baseline,*known);
    REQUIRE(known->passed);
    for(unsigned reg=0;reg<SnapshotCount;++reg) if(reg!=BCR && reg!=RISCVCPU) {
      failed([&baseline,reg](Fake &f){ f.badRead=int(reg); f.badReadAt=baseline.reads[reg]; },true);
    }
  }
  {
    Fake io(true); io.badWrite=BCR; auto r=std::unique_ptr<Result>(new Result); execute(io,*r);
    REQUIRE(!r->passed && r->imemSubmitted==0);
  }
  for(Reg reg: {BCR,RISCVCPU}) {
    Fake io(true); io.badRead=int(reg); auto r=std::unique_ptr<Result>(new Result); execute(io,*r);
    REQUIRE(!r->passed && !r->resetAttempted && r->releaseSafe);
  }
  {
    Fake io(true); io.regs[RISCVCPU]=0x80; auto r=std::unique_ptr<Result>(new Result); execute(io,*r);
    REQUIRE(!r->passed && !r->resetAttempted && r->releaseSafe);
  }
  {
    Fake io; io.regs[HWCFG2]&=~0x80000000U; auto r=std::unique_ptr<Result>(new Result); execute(io,*r);
    REQUIRE(r->passed); REQUIRE(io.delayTotal>=150);
  }
  std::printf("SEC2 staging: %u scenarios passed (no CPU start)\n",scenarios);
}
