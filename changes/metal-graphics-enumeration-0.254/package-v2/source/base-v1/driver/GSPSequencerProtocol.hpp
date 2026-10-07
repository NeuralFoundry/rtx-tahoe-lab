#pragma once
#include "GSPSequencerProfile.hpp"
#include "SEC2FirstBootStageProtocol.hpp"

namespace GSPSequencer {
constexpr U64 BudgetNs=15000000000ULL,PollNs=4000000000ULL;
constexpr unsigned MaxTicks=200000;
enum Failure : unsigned { None,InvalidProfile,OwnerLost,WorkspaceMissing,ClockRegressed,
  Deadline,Unreadable,WriteFailed,PollTimeout,CoreState,PointerLost,StartRejected,FirmwareError };
struct Result {
  const char *status="not-run";Failure failure=None;
  bool validated=false,attempted=false,passed=false,falconStart=false,falconHalted=false,sec2Start=false,resumed=false;
  unsigned completed=0,word=0,opcode=~0U,ticks=0,reads=0,writes=0,polls=0,resetCount=0;
  unsigned imemCommands=0,dmemCommands=0,lastAddress=0,lastValue=0xffffffffU;
  unsigned falconCpu=0xffffffffU,falconMailbox0=0xffffffffU,falconMailbox1=0xffffffffU;
  unsigned sec2Cpu=0xffffffffU,sec2Mailbox0=0xffffffffU,riscv=0xffffffffU,bcr=0xffffffffU,handoff=0xffffffffU;
  U64 startedNs=0,elapsedNs=0,libosArgs=0;
};
inline bool fail(Result &r,Failure f,const char *s){r.failure=f;r.status=s;r.passed=false;return false;}
template<class IO> struct Executor {
  IO &io;Result &r;
  bool tick(){
    const U64 now=io.nowNs();
    if(now<r.startedNs || now-r.startedNs<r.elapsedNs)return fail(r,ClockRegressed,"sequence-clock-regressed");
    r.elapsedNs=now-r.startedNs;
    if(!io.ready())return fail(r,OwnerLost,"sequence-owner-lost");
    if(!io.workspaceHeld(WorkspaceStart,WorkspaceEnd))return fail(r,WorkspaceMissing,"sequence-workspace-lost");
    if(++r.ticks>MaxTicks || r.elapsedNs>=BudgetNs)return fail(r,Deadline,"sequence-deadline");
    return true;
  }
  bool read(unsigned address,unsigned &value){
    if(!tick())return false;
    r.lastAddress=address;value=io.read(address);r.lastValue=value;++r.reads;
    return SEC2FirstBootStage::readable(value) || fail(r,Unreadable,"sequence-register-unreadable");
  }
  bool rawWrite(unsigned address,unsigned value){
    if(!tick())return false;
    r.lastAddress=address;r.lastValue=value;r.attempted=true;++r.writes;
    return io.write(address,value) || fail(r,WriteFailed,"sequence-register-write-failed");
  }
  bool wait(unsigned address,unsigned mask,unsigned expected,U64 timeout=PollNs){
    const U64 began=r.elapsedNs;
    for(unsigned i=0;i<40000;++i){
      unsigned value=0;++r.polls;
      if(!read(address,value))return false;
      if((value&mask)==expected)return true;
      if(r.elapsedNs-began>=timeout)return fail(r,PollTimeout,"sequence-register-poll-timeout");
      io.delayUs(100);
    }
    return fail(r,PollTimeout,"sequence-register-poll-count-exhausted");
  }
  bool checked(unsigned address,unsigned value,unsigned mask=~0U){
    unsigned actual=0;
    return rawWrite(address,value) && read(address,actual) &&
      ((actual&mask)==(value&mask) || fail(r,CoreState,"sequence-write-readback-mismatch"));
  }
  bool reset(bool riscv){
    unsigned v=0;
    for(unsigned i=0;i<15;++i){
      if(!read(0x1100f4,v))return false;
      if(v&0x80000000U)break;io.delayUs(10);
    }
    if(!read(0x1103c0,v) || !checked(0x1103c0,v|1,1))return false;
    io.delayUs(10);
    if(!checked(0x1103c0,v&~1U,1) || !wait(0x1100f4,0x1000,0))return false;
    if(riscv){
      if(!rawWrite(0x111668,0x111) || !wait(0x111668,0x111,0x111))return false;
    }else{
      if(!read(0x1100f4,v))return false;
      if(!(v&0x400))return fail(r,CoreState,"sequence-riscv-capability-missing");
      if(!read(0x111668,v))return false;
      if((v&0x10) && !rawWrite(0x111668,0))return false;
      if(!wait(0x111668,0x11,1) || !checked(0x110084,0xb76000a1U) ||
         !read(0x110624,v) || !checked(0x110624,v|0x80U) || !checked(0x11010c,0))return false;
    }
    ++r.resetCount;return true;
  }
  bool start(bool sec2){
    const unsigned base=sec2?0x840000:0x110000;unsigned cpu=0;
    if(!read(base+0x100,cpu))return false;
    if((cpu&0x12)!=0x10)return fail(r,CoreState,"sequence-core-not-halted-before-start");
    if(!io.noteStart(sec2))return fail(r,StartRejected,"sequence-start-order-rejected");
    if(!rawWrite(base+((cpu&0x40)?0x130:0x100),2))return false;
    if(sec2)r.sec2Start=true;else r.falconStart=true;
    io.delayUs(100);return true;
  }
  bool resume(){
    if(!r.falconHalted || !io.currentLibosArgs(r.libosArgs) || !r.libosArgs ||
       (r.libosArgs&4095) || r.libosArgs>=(1ULL<<40))return fail(r,PointerLost,"sequence-libos-pointer-unavailable");
    const U64 args=r.libosArgs;
    if(!reset(true) || !checked(0x110040,unsigned(args)) || !checked(0x110044,unsigned(args>>32)))return false;
    if(!io.currentLibosArgs(r.libosArgs) || r.libosArgs!=args)return fail(r,PointerLost,"sequence-libos-pointer-changed");
    if(!read(0x1180f8,r.handoff))return false;
    if(r.handoff&0x04000000U)return fail(r,CoreState,"sequence-stale-sec2-handoff");
    if(!start(true) || !wait(0x1180f8,0x04000000U,0x04000000U))return false;
    if(!read(0x840100,r.sec2Cpu) || !read(0x840040,r.sec2Mailbox0) || !read(0x111388,r.riscv) ||
       !read(0x111668,r.bcr) || !read(0x1180f8,r.handoff))return false;
    if(r.sec2Mailbox0 || !(r.riscv&0x80))return fail(r,FirmwareError,"sequence-gsp-resume-failed");
    r.resumed=true;return true;
  }
  bool finish(bool good,unsigned words){if(good){++r.completed;r.word+=words;}return good;}
  bool write(unsigned a,unsigned v){
    r.opcode=Write;
    if(!rawWrite(a,v))return false;
    if(a==0x110118){r.imemCommands+=v==0x614;r.dmemCommands+=v==0x600;}
    return finish(true,3);
  }
  bool poll(unsigned a,unsigned m,unsigned v,unsigned timeout,unsigned error){
    (void)error;r.opcode=Poll;
    // This first canonical request contains only timeout0. Nonzero vendor
    // timeout units are intentionally not guessed by this executor.
    if(timeout)return fail(r,InvalidProfile,"sequence-noncanonical-timeout");
    return finish(wait(a,m,v),6);
  }
  bool core(unsigned op){
    r.opcode=op;bool good=false;
    if(op==CoreReset)good=reset(false);
    else if(op==CoreStart)good=start(false);
    else if(op==CoreWait){
      good=r.falconStart && wait(0x110100,0x10,0x10) && read(0x110100,r.falconCpu) &&
        read(0x110040,r.falconMailbox0) && read(0x110044,r.falconMailbox1);
      // Preserve the actual mailbox result. NVIDIA's sequencer waits for halt
      // here; its CORE_RESUME then validates SEC2's mailbox and RISC-V state.
      r.falconHalted=good;
    }else if(op==CoreResume)good=resume();
    else return fail(r,InvalidProfile,"sequence-core-op-invalid");
    return finish(good,1);
  }
};
template<class IO> void execute(IO &io,const unsigned char *payload,unsigned bytes,Result &r){
  r=Result{};Profile p;
  if(!profile(payload,bytes,p)){fail(r,InvalidProfile,"sequence-profile-rejected");return;}
  r.validated=true;r.startedNs=io.nowNs();Executor<IO> run{io,r};
  if(!run.tick())return;
  if(!io.claimExecution()){fail(r,StartRejected,"sequence-replay-rejected");return;}
  r.status="sequence-executing";
  // Execute the canonical literal program, never instructions fetched from
  // mutable input after prevalidation. The request only authorizes a match.
  r.passed=program(run) && r.completed==OperationCount && r.word==UsedWords &&
    r.imemCommands==ImemBlocks && r.dmemCommands==DmemBlocks && r.resumed;
  if(r.passed)r.status="sequence-complete-gsp-resumed";
  else if(r.failure==None)fail(r,CoreState,"sequence-incomplete");
}
}
