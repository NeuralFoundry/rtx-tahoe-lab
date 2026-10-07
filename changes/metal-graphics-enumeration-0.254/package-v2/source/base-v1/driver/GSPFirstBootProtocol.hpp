#pragma once
#include "SEC2FirstBootStageProtocol.hpp"

// One native-owned first boot, using NVIDIA 570.144 GA102 BCR/reset/START
// semantics and Nouveau v6.15 HS metadata/LibOS mailbox conventions.
// The caller retains all resources after ANY device operation. No cleanup,
// global GPU reset, sequencer execution, or client-supplied MMIO is provided.
namespace GSPFirstBoot {
using U64=unsigned long long;
enum Reg : unsigned { Boot0, GspEngine, GspHwcfg2, GspDmaCtl, GspCpu, GspRiscv,
  GspBcr, GspMailbox0, GspMailbox1, SecCpu, SecRiscv, SecBcr, SecDma, SecEngine,
  SecRm, SecMailbox0, SecMailbox1, SecBromPara, SecBromEngine, SecBromUcode,
  SecBromAlgorithm, SecBootVector, SecCpuAlias, WprLo, WprHi, Handoff, Count };
constexpr unsigned Offsets[Count]={0,0x1103c0,0x1100f4,0x11010c,0x110100,0x111388,
  0x111668,0x110040,0x110044,0x840100,0x841388,0x841668,0x840118,0x8403c0,
  0x840084,0x840040,0x840044,0x841210,0x84119c,0x841198,0x841180,0x840104,
  0x840130,0x1fa824,0x1fa828,0x1180f8};
struct Result {
  const char *status="not-run";
  unsigned writes=0,verifiedWrites=0,resetPolls=0,haltPolls=0;
  unsigned failedReg=Count,cpuBefore=0xffffffffU,cpuAfter=0xffffffffU;
  unsigned mailbox0=0xffffffffU,mailbox1=0xffffffffU,gspRiscv=0xffffffffU,gspBcr=0xffffffffU;
  unsigned wprLo=0xffffffffU,wprHi=0xffffffffU,handoff=0xffffffffU;
  bool gspResetAttempted=false,gspResetVerified=false,configurationVerified=false;
  bool startAttempted=false,startWriteAccepted=false,aliasUsed=false,sec2Halted=false,gspActive=false,passed=false;
};
inline bool readable(unsigned v){return SEC2FirstBootStage::readable(v);}
template<class IO> bool checked(IO &io,Result &r,Reg reg,unsigned value,unsigned mask=~0U){
  ++r.writes;
  if(!io.write(reg,value)){r.failedReg=reg;return false;}
  const unsigned v=io.read(reg);
  if(!readable(v) || (v&mask)!=(value&mask)){r.failedReg=reg;return false;}
  ++r.verifiedWrites;return true;
}
template<class IO> bool wait(IO &io,Reg reg,unsigned mask,unsigned want,unsigned count,unsigned &polls){
  for(unsigned i=0;i<count;++i){
    ++polls;if(!io.held() || io.command()!=6)return false;
    const unsigned v=io.read(reg);if(!readable(v))return false;
    if((v&mask)==want)return true;io.delayUs(100);
  }
  return false;
}
template<class IO> void run(IO &io,Result &r){
  r=Result{};
  if(!io.launchGate() || !io.held() || io.command()!=6){r.status="launch-gate-rejected";return;}
  U64 meta=0,args=0;
  if(!io.currentPointers(meta,args) || !meta || !args || (meta&4095) || (args&4095) ||
     meta>=(1ULL<<40) || args>=(1ULL<<40) || meta==args){r.status="launch-pointer-rejected";return;}
  const unsigned cpu=io.read(GspCpu),riscv=io.read(GspRiscv),bcr=io.read(GspBcr);
  const unsigned engine=io.read(GspEngine),scrub=io.read(GspHwcfg2),dma=io.read(GspDmaCtl);
  const unsigned scpu=io.read(SecCpu),sriscv=io.read(SecRiscv),sbcr=io.read(SecBcr),sdma=io.read(SecDma),sengine=io.read(SecEngine);
  if(io.read(Boot0)!=0xb76000a1U || !readable(cpu) || !readable(riscv) || !readable(bcr) ||
     !readable(engine) || !readable(scrub) || !readable(dma) ||
     (cpu&0x12)!=0x10 || (riscv&0x80) || (bcr&0x11)!=1 || (engine&1) || !(scrub&0x400) ||
     (scrub&0x1000) || (dma&6) || !readable(scpu) || !readable(sriscv) || !readable(sbcr) ||
     !readable(sdma) || !readable(sengine) || (scpu&0x12)!=0x10 || (sriscv&0x80) ||
     (sbcr&0x11)!=1 || (sdma&3)!=2 || (sengine&1) || !io.frtsMatches()){
    r.status="launch-engine-baseline-rejected";return;
  }
  // BROM/metadata writes while SEC2 remains stopped. Zero cannot be used as
  // a stale success mailbox because metadata is a validated nonzero pointer.
  const Reg regs[]={SecRm,SecMailbox0,SecMailbox1,SecBromPara,SecBromEngine,SecBromUcode,SecBromAlgorithm,SecBootVector};
  const unsigned values[]={0xb76000a1U,unsigned(meta),unsigned(meta>>32),0x10,1,3,1,0x100};
  for(unsigned i=0;i<8;++i)if(!checked(io,r,regs[i],values[i])){r.status="sec2-boot-configuration-failed";return;}
  // NVIDIA tolerates reset-ready not asserting after 150us, but not bad reads.
  for(unsigned i=0;i<15;++i){
    const unsigned v=io.read(GspHwcfg2);++r.resetPolls;
    if(!readable(v)){r.status="gsp-reset-ready-unreadable";return;}
    if(v&0x80000000U)break;io.delayUs(10);
  }
  r.gspResetAttempted=true;
  if(!checked(io,r,GspEngine,engine|1,1)){r.status="gsp-reset-assert-failed";return;}
  io.delayUs(10);
  if(!checked(io,r,GspEngine,engine&~1U,1) ||
     !wait(io,GspHwcfg2,0x1000,0,200,r.resetPolls) || !wait(io,GspDmaCtl,6,0,200,r.resetPolls) ||
     !checked(io,r,GspBcr,0x111,0x111)) {r.status="gsp-riscv-reset-failed";return;}
  r.gspResetVerified=true;
  if(!checked(io,r,GspMailbox0,unsigned(args)) || !checked(io,r,GspMailbox1,unsigned(args>>32))){
    r.status="gsp-libos-mailbox-failed";return;
  }
  U64 currentMeta=0,currentArgs=0;r.cpuBefore=io.read(SecCpu);
  const unsigned rcpu=io.read(GspRiscv),sc=io.read(SecBcr),sr=io.read(SecRiscv),dc=io.read(SecDma);
  if(!io.launchGate() || !io.currentPointers(currentMeta,currentArgs) || currentMeta!=meta || currentArgs!=args ||
     !io.frtsMatches() || !readable(r.cpuBefore) || (r.cpuBefore&0x12)!=0x10 ||
     !readable(rcpu) || (rcpu&0x80) || !readable(sc) || (sc&0x11)!=1 ||
     !readable(sr) || (sr&0x80) || !readable(dc) || (dc&3)!=2 ||
     io.read(SecMailbox0)!=unsigned(meta) || io.read(SecMailbox1)!=unsigned(meta>>32) ||
     io.read(GspMailbox0)!=unsigned(args) || io.read(GspMailbox1)!=unsigned(args>>32)){
    r.status="launch-prestart-state-changed";return;
  }
  r.configurationVerified=true;r.aliasUsed=(r.cpuBefore&0x40)!=0;
  r.startAttempted=true;++r.writes;
  if(!io.noteStart() || !io.write(r.aliasUsed?SecCpuAlias:SecCpu,2)){
    r.status="sec2-start-write-failed";return;
  }
  r.startWriteAccepted=true;
  // Give the start trigger a delayed observation; mailbox must change from
  // the nonzero metadata pointer to success, not merely show a stale halt bit.
  for(unsigned i=0;i<40000;++i){
    io.delayUs(100);++r.haltPolls;
    if(!io.held() || io.command()!=6){r.status="launch-owner-lost";return;}
    r.cpuAfter=io.read(SecCpu);
    if(!readable(r.cpuAfter)){r.status="sec2-cpu-unreadable";return;}
    if((r.cpuAfter&0x10) && io.read(SecMailbox0)==0){r.sec2Halted=true;break;}
  }
  r.mailbox0=io.read(SecMailbox0);r.mailbox1=io.read(SecMailbox1);
  r.gspRiscv=io.read(GspRiscv);r.gspBcr=io.read(GspBcr);
  r.wprLo=io.read(WprLo);r.wprHi=io.read(WprHi);r.handoff=io.read(Handoff);
  r.gspActive=readable(r.gspRiscv) && (r.gspRiscv&0x80);
  r.passed=r.sec2Halted && r.mailbox0==0 && r.gspActive;
  r.status=r.passed?"sec2-booter-returned-gsp-active":
    (!r.sec2Halted?"sec2-booter-halt-or-mailbox-failed":"gsp-not-observed-active");
}
}
