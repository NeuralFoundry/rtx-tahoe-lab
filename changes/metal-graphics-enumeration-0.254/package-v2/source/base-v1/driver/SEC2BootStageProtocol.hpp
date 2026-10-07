#pragma once

// Revision 0.12.1: explicit measured halted-RISC-V -> Falcon initialization.
// Independent portable staging-only protocol. The caller owns already prepared
// host mappings and the provider. This code never frees/closes those resources.
namespace SEC2BootStage {
using U64 = unsigned long long;
constexpr unsigned Base=0x840000, ImageBytes=60416, AllocationBytes=61440;
constexpr unsigned ImemOffset=0x100, ImemBytes=0x8900, DmemOffset=0x8a00, DmemBytes=0x6200;
constexpr unsigned BlockBytes=256, ImemBlocks=137, DmemBlocks=98, Blocks=235, DmemWords=6272;
enum Reg : unsigned { Engine, HWCFG2, HWCFG, CPUCTL, DMACTL, DMACMD, BCR, RISCVCPU,
  TRANSCFG, FBIFCTL, DMABASE, DMABASE1, DMAOFFSET, FBOFFSET, DMEMC, DMEMD, Count };
constexpr unsigned Offsets[Count]={0x3c0,0xf4,0x108,0x100,0x10c,0x118,0x1668,0x1388,
  0x600,0x624,0x110,0x128,0x114,0x11c,0x1c0,0x1c4};
constexpr unsigned SnapshotCount=14;
struct Result {
  const char *status="not-run";
  unsigned commandBefore=0xffff, commandEnabled=0xffff, commandAfter=0xffff;
  unsigned deviceStatusBefore=0xffff, deviceStatusAfter=0xffff;
  unsigned initial[SnapshotCount]={}, final[SnapshotCount]={};
  unsigned initialMask=0, finalMask=0, initialReads=0, finalReads=0;
  unsigned baseline[SnapshotCount]={}, baselineMask=0, baselineReads=0;
  unsigned modeCheck[5]={}, modeCheckReads=0;
  unsigned hwcfg=0xffffffffU, resetCount=0, resetPolls=0, drainPolls=0, dmaPolls=0;
  unsigned synchronizeCount=0, canaryMatched=0, imemSubmitted=0, imemCompleted=0;
  unsigned dmemSubmitted=0, dmemCompleted=0, dmemReads=0, dmemMatched=0;
  unsigned mismatchWord=0xffffffffU, mismatchValue=0;
  unsigned completions[Blocks]={}, dmem[DmemWords]={};
  bool imageMatched=false, environmentMatched=false, hasRiscv=false;
  bool memoryAttempted=false, masterAttempted=false, resetAttempted=false, targetsAttempted=false, anyDMA=false;
  bool cpuIntact=false, staged=false, quiescent=false, targetsCleared=false, finalVerified=false;
  bool baselineVerified=false, modeInitAttempted=false, modeInitVerified=false;
  bool releaseSafe=false, passed=false;
};

// IO: command/setMemory/setMaster/deviceStatus/read(Reg)/write(Reg,value)/
// delayUs/imageWord/imageAddress(offset,U64&)/verifyImage/synchronizeImage/
// imageMatchesBoard/environmentReady. All MMIO is limited to Reg above;
// CPUCTL and RISCVCPU are read only, BCR may only select Falcon after reset.
inline bool readable(unsigned v) {
  return v!=0xffffffffU && (v&0xffff0000U)!=0xbadf0000U && (v&0xffff0000U)!=0xbad00000U;
}
inline unsigned requiredMask(bool riscv) {
  return ((1U<<SnapshotCount)-1U) & (riscv ? ~0U : ~((1U<<BCR)|(1U<<RISCVCPU)));
}
constexpr unsigned ObservedBootMask=(1U<<Engine)|(1U<<HWCFG2)|(1U<<HWCFG)|(1U<<BCR)|(1U<<RISCVCPU);
constexpr Reg ModeCheckRegs[5]={Engine,HWCFG2,HWCFG,BCR,RISCVCPU};
// Narrow observed-board exception to the full initial Falcon snapshot rule.
// 0xbadf5620 remains unreadable; it is never interpreted as a register value.
// Only this measured halted, inactive RISC-V mode authorizes local reset and
// selection of Falcon using NVIDIA's documented post-reset sequence.
inline bool observedRiscvBoot(const Result &r) {
  if(!r.hasRiscv || r.initialMask!=ObservedBootMask || r.initial[Engine]!=0 ||
     r.initial[HWCFG2]!=0x67f7U || r.initial[HWCFG]!=0x80420100U ||
     r.initial[BCR]!=0x110U || r.initial[RISCVCPU]!=0x10U) return false;
  for(unsigned i=0;i<SnapshotCount;++i)
    if(!(ObservedBootMask&(1U<<i)) && r.initial[i]!=0xbadf5620U) return false;
  return true;
}
template<class IO> bool stableObservedBoot(IO &io,Result &r) {
  if(!observedRiscvBoot(r) || io.command()!=2) return false;
  io.delayUs(10);
  for(unsigned i=0;i<5;++i) {
    const unsigned value=io.read(ModeCheckRegs[i]);
    r.modeCheck[i]=value; ++r.modeCheckReads;
    if(!readable(value) || value!=r.initial[ModeCheckRegs[i]]) return false;
  }
  return io.command()==2;
}
template<class IO> bool writeCheck(IO &io, Reg reg, unsigned v, unsigned mask=~0U) {
  io.write(reg,v);
  const unsigned observed=io.read(reg);
  return readable(observed) && (observed&mask)==(v&mask);
}
template<class IO> bool waitBits(IO &io, Reg reg, unsigned mask, unsigned want,
                                unsigned limit, unsigned delay, unsigned &polls) {
  for(unsigned i=0;i<limit;++i) {
    ++polls;
    if(!(io.command()&2U)) return false;
    const unsigned v=io.read(reg);
    if(!readable(v)) return false;
    if((v&mask)==want) return true;
    io.delayUs(delay);
  }
  return false;
}
template<class IO> bool snapshot(IO &io, unsigned *out, unsigned &mask, unsigned &reads, bool riscv) {
  mask=0;
  for(unsigned i=0;i<SnapshotCount;++i) {
    if(!riscv && (i==BCR || i==RISCVCPU)) continue;
    out[i]=io.read(Reg(i)); ++reads;
    if(readable(out[i])) mask|=1U<<i;
  }
  return mask==requiredMask(riscv) && bool(out[HWCFG2]&0x400U)==riscv;
}
template<class IO> bool reset(IO &io, Result &r) {
  r.resetAttempted=true; ++r.resetCount;
  for(unsigned i=0;i<15;++i) {
    const unsigned v=io.read(HWCFG2); ++r.resetPolls;
    if(!readable(v) || bool(v&0x400U)!=r.hasRiscv) return false;
    if(v&0x80000000U) break; // Advisory; NVIDIA tolerates a 150us timeout.
    io.delayUs(10);
  }
  const unsigned engine=io.read(Engine);
  if(!readable(engine) || !writeCheck(io,Engine,engine|1U,1U)) return false;
  io.delayUs(10);
  if(!writeCheck(io,Engine,engine&~1U,1U) ||
     !waitBits(io,HWCFG2,0x1000U,0,200,100,r.resetPolls)) return false;
  const unsigned cfg=io.read(HWCFG2);
  if(!readable(cfg) || bool(cfg&0x400U)!=r.hasRiscv) return false;
  if(r.hasRiscv) {
    const unsigned bcr=io.read(BCR);
    if(!readable(bcr)) return false;
    if(bcr&0x10U) {
      io.write(BCR,0);
      if(!waitBits(io,BCR,0x11U,1U,100,100,r.resetPolls)) return false;
    }
    const unsigned cpu=io.read(RISCVCPU);
    if(!readable(cpu) || (cpu&0x80U)) return false;
  }
  const unsigned cpu=io.read(CPUCTL);
  return readable(cpu) && !(cpu&2U);
}
template<class IO> bool sourcePages(IO &io, U64 *pages) {
  for(unsigned i=0;i<15;++i) {
    if(!io.imageAddress(i*4096,pages[i]) || !pages[i] || (pages[i]&4095U) ||
       pages[i]>=(U64(1)<<40) || pages[i]>(U64(1)<<40)-4096) return false;
    for(unsigned j=0;j<i;++j) if(pages[j]==pages[i]) return false;
  }
  return true;
}
// The source image uses discontinuous pages; FBOFFS must nevertheless preserve
// the SEC2 secure IMEM virtual tag. No block crosses a page boundary.
inline bool blockAddress(const U64 *pages,unsigned block,U64 &source,unsigned &base,
                         unsigned &base1,unsigned &memOffset,unsigned &fbOffset,unsigned &imageOffset) {
  if(block>=Blocks) return false;
  const bool imem=block<ImemBlocks;
  memOffset=(imem ? block : block-ImemBlocks)*BlockBytes;
  imageOffset=(imem ? ImemOffset : DmemOffset)+memOffset;
  fbOffset=imem ? ImemOffset+memOffset : memOffset;
  const U64 page=pages[imageOffset/4096];
  if(!page || (page&4095U) || page>(U64(1)<<40)-4096) return false;
  source=page+(imageOffset&4095U);
  if(source<fbOffset || (source&255U) || (source-fbOffset)&255U) return false;
  const U64 shifted=(source-fbOffset)>>8;
  base=unsigned(shifted); base1=unsigned(shifted>>32);
  return base1<=0x1ffU && (((U64(base1)<<32)|base)<<8)+fbOffset==source;
}
template<class IO> void finish(IO &io, Result &r) {
  if(r.resetAttempted && (io.command()&2U)) {
    bool stopped=reset(io,r);
    if(stopped) stopped=waitBits(io,DMACMD,3,2,200,100,r.drainPolls);
    if(stopped) {
      for(unsigned i=0;i<200;++i) {
        r.deviceStatusAfter=io.deviceStatus(); ++r.drainPolls;
        if(r.deviceStatusAfter==0xffffU) break;
        if(!(r.deviceStatusAfter&0x20U)) { r.quiescent=true; break; }
        io.delayUs(100);
      }
    }
    if(r.quiescent && r.baselineVerified) {
      const unsigned ctl=io.read(FBIFCTL);
      r.targetsCleared=readable(ctl) && writeCheck(io,DMACTL,1,1) &&
        writeCheck(io,FBIFCTL,ctl&~0x80U) && writeCheck(io,DMABASE,0) &&
        writeCheck(io,DMABASE1,0) && writeCheck(io,DMAOFFSET,0) && writeCheck(io,FBOFFSET,0) &&
        writeCheck(io,TRANSCFG,r.baseline[TRANSCFG]);
    }
    r.finalVerified=snapshot(io,r.final,r.finalMask,r.finalReads,r.hasRiscv);
    if(r.finalVerified) {
      const unsigned *v=r.final;
      r.finalVerified=r.baselineVerified && !(v[Engine]&1U) && !(v[HWCFG2]&0x1000U) && !(v[CPUCTL]&2U) &&
        (!r.hasRiscv || (!(v[RISCVCPU]&0x80U) && !(v[BCR]&0x10U))) &&
        (v[DMACMD]&3U)==2 && (v[DMACTL]&7U)==1 && !(v[FBIFCTL]&0x80U) &&
        !v[DMABASE] && !v[DMABASE1] && !v[DMAOFFSET] && !v[FBOFFSET] &&
        v[TRANSCFG]==r.baseline[TRANSCFG];
    }
  }
  // MMIO stays enabled through reset/drain/snapshot, then PCI is disabled before
  // the CPU image is checked. The caller alone decides when IODMA objects free.
  if(r.memoryAttempted) { io.setMaster(false); io.setMemory(false); }
  r.commandAfter=io.command();
  r.cpuIntact=io.verifyImage();
  const bool untouched=!r.resetAttempted && !r.targetsAttempted && !r.anyDMA;
  r.releaseSafe=r.commandBefore==0 && r.commandAfter==0 && r.cpuIntact &&
    (untouched || (r.quiescent && r.targetsCleared && r.finalVerified));
  r.passed=r.staged && r.releaseSafe;
  if(!r.releaseSafe) r.status="sec2-cleanup-unproven-resources-retained";
  else if(r.passed) r.status="SEC2-staged-DMEM-verified-not-executed";
}
template<class IO> void run(IO &io, Result &r) {
  U64 pages[15]={};
  r.commandBefore=io.command();
  do {
    if(r.commandBefore!=0) { r.status="sec2-command-not-zero"; break; }
    r.imageMatched=io.imageMatchesBoard();
    if(!r.imageMatched || !io.verifyImage()) { r.status="sec2-board-image-mismatch"; break; }
    if(!sourcePages(io,pages)) { r.status="sec2-source-pages-invalid"; break; }
    r.deviceStatusBefore=io.deviceStatus();
    if(r.deviceStatusBefore==0xffffU || (r.deviceStatusBefore&0x20U)) {
      r.status="sec2-PCIe-not-idle"; break;
    }
    r.memoryAttempted=true; io.setMemory(true);
    if(io.command()!=2) { r.status="sec2-memory-enable-failed"; break; }
    r.environmentMatched=io.environmentReady();
    if(!r.environmentMatched) { r.status="sec2-environment-not-ready"; break; }
    const unsigned cfg=io.read(HWCFG2);
    if(!readable(cfg)) { r.status="sec2-register-unreadable"; break; }
    r.hasRiscv=bool(cfg&0x400U);
    const bool fullInitial=snapshot(io,r.initial,r.initialMask,r.initialReads,r.hasRiscv);
    if(!fullInitial) {
      if(!observedRiscvBoot(r)) { r.status="sec2-register-unreadable"; break; }
      if(!stableObservedBoot(io,r)) { r.status="sec2-initial-RISC-V-mode-unstable"; break; }
      r.modeInitAttempted=true;
    } else {
      // Original Falcon-accessible branch retains its strict initial HALTED gate.
      if(!(r.initial[CPUCTL]&0x10U)) { r.status="sec2-initial-CPU-not-halted"; break; }
      if((r.initial[Engine]&1U) || (r.initial[HWCFG2]&0x1000U) || (r.initial[DMACTL]&6U) ||
         (r.initial[DMACMD]&3U)!=2 || ((r.initial[CPUCTL]&2U) && !(r.initial[CPUCTL]&0x10U)) ||
         (r.hasRiscv && (r.initial[RISCVCPU]&0x80U))) {
        r.status="sec2-engine-not-idle"; break;
      }
    }
    if(!reset(io,r)) { r.status="sec2-reset-failed"; break; }
    r.baselineVerified=snapshot(io,r.baseline,r.baselineMask,r.baselineReads,r.hasRiscv);
    if(!r.baselineVerified) { r.status="sec2-baseline-register-unreadable"; break; }
    const unsigned *baseline=r.baseline;
    r.baselineVerified=!(baseline[Engine]&1U) && !(baseline[HWCFG2]&0x1000U) &&
      !(baseline[CPUCTL]&2U) && !(baseline[DMACTL]&6U) && (baseline[DMACMD]&3U)==2 &&
      (!r.hasRiscv || (!(baseline[BCR]&0x10U) && !(baseline[RISCVCPU]&0x80U)));
    if(!r.baselineVerified) { r.status="sec2-baseline-engine-not-idle"; break; }
    r.modeInitVerified=r.modeInitAttempted;
    r.hwcfg=io.read(HWCFG);
    if(!readable(r.hwcfg) || ((r.hwcfg&0x1ffU)<<8)<ImemBytes ||
       ((r.hwcfg&0x3fe00U)>>1)<DmemBytes) { r.status="sec2-memory-size-invalid"; break; }
    bool good=true;
    for(unsigned word=0;word<DmemWords;++word) {
      if(!writeCheck(io,DMEMC,word*4,0xffffffU)) { good=false; r.status="sec2-PIO-address-failed"; break; }
      io.write(DMEMD,~io.imageWord(DmemOffset+word*4));
    }
    for(unsigned word=0;word<DmemWords && good;++word) {
      if(!writeCheck(io,DMEMC,word*4,0xffffffU)) { good=false; r.status="sec2-PIO-address-failed"; break; }
      const unsigned v=io.read(DMEMD);
      if(v!=~io.imageWord(DmemOffset+word*4)) {
        good=false; r.status="sec2-PIO-canary-failed"; r.mismatchWord=word; r.mismatchValue=v; break;
      }
      ++r.canaryMatched;
    }
    if(!good) break;
    if(!io.synchronizeImage()) { r.status="sec2-synchronize-failed"; break; }
    ++r.synchronizeCount;
    if(!io.verifyImage()) { r.status="sec2-CPU-buffer-changed"; break; }
    const unsigned ctl=io.read(FBIFCTL),trans=io.read(TRANSCFG);
    r.targetsAttempted=true;
    if(!readable(ctl) || !readable(trans) || !writeCheck(io,FBIFCTL,ctl|0x80U) ||
       !writeCheck(io,DMACTL,0,7) || !writeCheck(io,TRANSCFG,(trans&~0x10007U)|5U)) {
      r.status="sec2-FBIF-setup-failed"; break;
    }
    for(unsigned block=0;block<Blocks && good;++block) {
      if(io.command()!=(r.masterAttempted ? 6U : 2U)) {
        good=false; r.status="sec2-command-changed"; break;
      }
      if(!waitBits(io,DMACMD,3,2,200,100,r.dmaPolls)) {
        good=false; r.status="sec2-DMA-not-idle"; break;
      }
      U64 source=0,current=0,end=0; unsigned base=0,base1=0,mem=0,fb=0,offset=0;
      if(!blockAddress(pages,block,source,base,base1,mem,fb,offset) ||
         !io.imageAddress(offset,current) || current!=source ||
         !io.imageAddress(offset+255,end) || end!=source+255 ||
         !writeCheck(io,DMABASE,base) || !writeCheck(io,DMABASE1,base1) ||
         !writeCheck(io,DMAOFFSET,mem) || !writeCheck(io,FBOFFSET,fb)) {
        good=false; r.status="sec2-DMA-address-failed"; break;
      }
      if(!r.masterAttempted) { r.masterAttempted=true; io.setMaster(true); }
      r.commandEnabled=io.command();
      if(r.commandEnabled!=6) { good=false; r.status="sec2-master-enable-failed"; break; }
      if(!waitBits(io,DMACMD,3,2,200,100,r.dmaPolls)) {
        good=false; r.status="sec2-DMA-not-idle"; break;
      }
      r.anyDMA=true;
      if(block<ImemBlocks) ++r.imemSubmitted; else ++r.dmemSubmitted;
      io.write(DMACMD,block<ImemBlocks ? 0x614U : 0x600U);
      if(!waitBits(io,DMACMD,3,2,200,100,r.dmaPolls)) {
        good=false; r.status="sec2-DMA-timeout"; break;
      }
      const unsigned done=io.read(DMACMD); r.completions[block]=done;
      if(!readable(done) || (done&3U)!=2) {
        good=false; r.status="sec2-DMA-completion-invalid"; break;
      }
      if(block<ImemBlocks) ++r.imemCompleted; else ++r.dmemCompleted;
    }
    for(unsigned word=0;word<DmemWords && good;++word) {
      if(!writeCheck(io,DMEMC,word*4,0xffffffU)) { good=false; r.status="sec2-PIO-address-failed"; break; }
      const unsigned v=io.read(DMEMD); r.dmem[word]=v; ++r.dmemReads;
      if(v!=io.imageWord(DmemOffset+word*4)) {
        good=false; r.status="sec2-DMEM-data-mismatch"; r.mismatchWord=word; r.mismatchValue=v; break;
      }
      ++r.dmemMatched;
    }
    if(!good) break;
    if(!io.verifyImage()) { r.status="sec2-CPU-buffer-changed"; break; }
    r.staged=true;
  } while(false);
  // The coordinator retains a successful stage for immediate BROM execution.
  // Failed stages still attempt the already-tested local teardown.
  if(!r.staged) finish(io,r);
}
}
