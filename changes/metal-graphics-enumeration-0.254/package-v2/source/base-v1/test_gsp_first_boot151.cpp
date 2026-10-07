#include "driver/GSPFirstBootProtocol151.hpp"
#include "driver/GSPFirstBootProtocol.hpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
using namespace GSPFirstBoot151;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::abort();}}while(0)
struct IO {
  unsigned regs[Count]={},writes[Count]={};
  unsigned pci=6,notes=0,pointerCalls=0,delays=0;
  unsigned legacyReadsAfterReset=0,bcrPending=0;
  int failWrite=-1,failRead=-1;
  bool gate=true,own=true,frts=true,pointers=true,changedPointer=false,startOk=true,returns=true,active=true,noted=false;
  bool resetReleased=false,lockFalcon=false,scrubStuck=false,delayBcr=false,bcrStuck=false;
  IO(){regs[Boot0]=0xb76000a1;regs[GspHwcfg2]=0x400;regs[GspCpu]=regs[SecCpu]=0x10;
    regs[GspBcr]=regs[SecBcr]=1;regs[SecDma]=2;regs[WprLo]=0x17fe000;regs[WprHi]=0x17ff000;}
  bool launchGate(){return gate;}
  bool held(){return own;}
  unsigned command(){return pci;}
  bool currentPointers(U64 &a,U64 &b){++pointerCalls;a=0x12345000+(changedPointer && pointerCalls>1?4096:0);b=0x45678000;return pointers;}
  bool frtsMatches(){return frts;}
  unsigned read(Reg r){
    CHECK(r!=SecCpuAlias);
    if(resetReleased && (r==GspDmaCtl || r==GspCpu)){
      ++legacyReadsAfterReset;if(lockFalcon)return 0xbadf5620U;
    }
    if(r==GspBcr && bcrPending && !bcrStuck && !--bcrPending)regs[GspBcr]=0x111;
    return int(r)==failRead?0xffffffffU:regs[r];
  }
  unsigned read(GSPFirstBoot::Reg r){return read(Reg(r));}
  bool noteStart(){++notes;noted=true;return startOk;}
  bool write(Reg r,unsigned value){
    ++writes[r];if(int(r)==failWrite)return false;
    if(r==SecCpu || r==SecCpuAlias){CHECK(noted);CHECK(value==2);if(returns){regs[SecMailbox0]=0;regs[SecCpu]=0x10;}
      else regs[SecCpu]=2;regs[GspRiscv]=active?0x80:0;}
    else regs[r]=value;
    if(r==GspEngine && value==0){resetReleased=true;if(lockFalcon)regs[GspBcr]=0x110;if(scrubStuck)regs[GspHwcfg2]|=0x1000;}
    if(r==GspBcr && delayBcr){regs[r]=0x110;bcrPending=4;}
    return true;
  }
  bool write(GSPFirstBoot::Reg r,unsigned value){return write(Reg(r),value);}
  void delayUs(unsigned us){delays+=us;}
};
int main(){
  {
    IO io;Result r;run(io,r);CHECK(r.passed);CHECK(r.sec2Halted && r.gspActive);CHECK(io.notes==1);
    CHECK(io.writes[SecCpu]==1 && !io.writes[SecCpuAlias]);CHECK(io.regs[GspBcr]==0x111);
    CHECK(io.regs[SecBromPara]==0x10 && io.regs[SecBootVector]==0x100);
    CHECK(io.regs[GspMailbox0]==0x45678000);CHECK(!io.writes[WprLo] && !io.writes[WprHi]);
  }
  {IO io;io.regs[SecCpu]|=0x40;Result r;run(io,r);CHECK(r.passed && r.aliasUsed);CHECK(io.writes[SecCpuAlias]==1);}
  for(unsigned mode=0;mode<8;++mode){
    IO io;Result r;
    if(mode==0)io.gate=false;if(mode==1)io.own=false;if(mode==2)io.pci=2;if(mode==3)io.pointers=false;
    if(mode==4)io.frts=false;if(mode==5)io.regs[SecCpu]=2;if(mode==6)io.regs[GspRiscv]=0x80;if(mode==7)io.regs[Boot0]=0;
    run(io,r);CHECK(!r.passed);CHECK(!io.notes);CHECK(!r.startAttempted);for(unsigned w:io.writes)CHECK(w==0);
  }
  const Reg written[]={SecRm,SecMailbox0,SecMailbox1,SecBromPara,SecBromEngine,SecBromUcode,
    SecBromAlgorithm,SecBootVector,GspEngine,GspBcr,GspMailbox0,GspMailbox1,SecCpu};
  for(Reg reg:written){IO io;io.failWrite=reg;Result r;run(io,r);CHECK(!r.passed);
    CHECK(io.notes==(reg==SecCpu?1U:0U));}
  for(Reg reg:{GspCpu,GspRiscv,GspBcr,GspEngine,GspHwcfg2,GspDmaCtl,SecCpu,SecRiscv,SecBcr,SecDma,SecEngine}){
    IO io;io.failRead=reg;Result r;run(io,r);CHECK(!r.passed);CHECK(!io.notes);
  }
  {IO io;io.changedPointer=true;Result r;run(io,r);CHECK(!r.passed);CHECK(!io.notes);}
  {IO io;io.startOk=false;Result r;run(io,r);CHECK(!r.passed && r.startAttempted);CHECK(!io.writes[SecCpu]);}
  {IO io;io.returns=false;Result r;run(io,r);CHECK(!r.passed);CHECK(r.haltPolls==40000);CHECK(io.notes==1);}
  {IO io;io.active=false;Result r;run(io,r);CHECK(!r.passed);CHECK(r.sec2Halted && !r.gspActive);}
  {IO io;io.lockFalcon=true;GSPFirstBoot::Result old;GSPFirstBoot::run(io,old);
    CHECK(!old.passed && !old.startAttempted);CHECK(io.legacyReadsAfterReset==1);CHECK(old.writes==10);}
  {IO io;io.lockFalcon=true;Result r;run(io,r);CHECK(r.passed);CHECK(io.legacyReadsAfterReset==0);}
  {IO io;io.lockFalcon=true;io.delayBcr=true;Result r;run(io,r);CHECK(r.passed);
    CHECK(r.lastPollReg==GspBcr && r.lastPollValue==0x111 && r.lastPollCount==4);}
  {IO io;io.scrubStuck=true;Result r;run(io,r);CHECK(!r.passed && !r.startAttempted);
    CHECK(r.failedReg==GspHwcfg2 && r.lastPollCount==200 && (r.lastPollValue&0x1000));CHECK(!io.writes[GspBcr]);}
  {IO io;io.delayBcr=true;io.bcrStuck=true;Result r;run(io,r);CHECK(!r.passed && !r.startAttempted);
    CHECK(r.failedReg==GspBcr && r.lastPollCount==200 && r.lastPollValue==0x110);}
  std::printf("GSP 0.15.1 first boot: %u checks passed; Falcon lockout regression modeled\n",checks);
}
