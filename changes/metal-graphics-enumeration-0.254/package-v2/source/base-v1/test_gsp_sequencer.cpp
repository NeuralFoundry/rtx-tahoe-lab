#include "driver/GSPSequencerProtocol.hpp"
#include <vector>
#include <map>
#include <cstdio>
#include <cstdlib>
using namespace GSPSequencer;
static unsigned checks=0,scenarios=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::abort();}}while(0)
static void put(std::vector<unsigned char>&p,unsigned off,unsigned v){for(unsigned i=0;i<4;++i)p[off+i]=static_cast<unsigned char>(v>>(i*8));}
struct Encoder {
  std::vector<unsigned char> p=std::vector<unsigned char>(PayloadBytes);unsigned cursor=40;
  Encoder(){put(p,0,CapacityWords);put(p,4,UsedWords);CHECK(program(*this));CHECK(cursor==PayloadBytes);}
  bool word(unsigned v){CHECK(cursor+4<=p.size());put(p,cursor,v);cursor+=4;return true;}
  bool write(unsigned a,unsigned v){return word(Write)&&word(a)&&word(v);}
  bool poll(unsigned a,unsigned m,unsigned v,unsigned t,unsigned e){return word(Poll)&&word(a)&&word(m)&&word(v)&&word(t)&&word(e);}
  bool core(unsigned op){return word(op);}
};
struct IO {
  std::map<unsigned,unsigned> regs;
  std::vector<std::pair<unsigned,unsigned>> writes;
  U64 ns=1000000;unsigned reads=0,writeCalls=0,timeCalls=0,pointerCalls=0;
  int failRead=-1,failWrite=-1;
  bool owned=true,workspace=true,claimed=false,gspNoted=false,secNoted=false,alias=false;
  bool stallMailbox=false,stallDma=false,stallHalt=false,stallHandoff=false,staleHandoff=false;
  bool noRiscv=false,wrongSecMailbox=false,clockBack=false,clockStuck=false,pointerChanged=false;
  bool startDenied=false,ownerLost=false,workspaceLost=false;
  IO(){regs[0x1100f4]=0x80000400U;regs[0x111668]=0x111;regs[0x110100]=regs[0x840100]=0x10;
    regs[0x110040]=0x80000000U;regs[0x110118]=2;}
  bool ready(){return owned && !(ownerLost && writeCalls>12);}
  bool workspaceHeld(U64 begin,U64 end){CHECK(begin==WorkspaceStart&&end==WorkspaceEnd);return workspace && !(workspaceLost&&writeCalls>12);}
  bool claimExecution(){if(claimed)return false;claimed=true;return true;}
  U64 nowNs(){++timeCalls;if(clockBack&&timeCalls>30)return 0;if(!clockStuck)ns+=1000;return ns;}
  void delayUs(unsigned us){CHECK(us<=100);if(!clockStuck)ns+=U64(us)*1000;}
  bool currentLibosArgs(U64 &p){p=0x123456000ULL+(pointerChanged&&pointerCalls?4096:0);++pointerCalls;return true;}
  bool noteStart(bool sec2){if(startDenied)return false;bool &noted=sec2?secNoted:gspNoted;if(noted)return false;noted=true;return true;}
  unsigned read(unsigned address){
    if(int(++reads)==failRead)return 0xffffffffU;
    if(stallMailbox&&address==0x110040&&!writeCalls)return 0;
    if(stallDma&&address==0x110118)return 1;
    if(staleHandoff&&address==0x1180f8)return 0x04000000;
    unsigned v=regs[address];if(alias&&(address==0x110100||address==0x840100))v|=0x40;
    return v;
  }
  bool write(unsigned address,unsigned value){
    if(int(++writeCalls)==failWrite)return false;
    writes.push_back({address,value});regs[address]=value;
    if(address==0x1103c0 && !(value&1)){regs[0x111668]=0x111;regs[0x110100]=0x10;}
    if(address==0x111668 && !value)regs[address]=1;
    if(address==0x110118)regs[address]=2;
    if(address==0x110100 || address==0x110130){CHECK(value==2&&gspNoted);
      regs[0x110100]=stallHalt?2:0x10;regs[0x110040]=0;}
    if(address==0x840100 || address==0x840130){CHECK(value==2&&secNoted);
      regs[0x840100]=0x10;regs[0x840040]=wrongSecMailbox?1:0;
      regs[0x1180f8]=stallHandoff?0:0x04000000;regs[0x111388]=noRiscv?0:0x80;}
    return true;
  }
};
static Result run(IO &io,const std::vector<unsigned char>&p){++scenarios;Result r;execute(io,p.data(),unsigned(p.size()),r);return r;}
int main(){
  Encoder e;IO success;const auto good=run(success,e.p);
  CHECK(good.passed&&good.resumed&&good.completed==420&&good.word==1564);
  CHECK(good.imemCommands==64&&good.dmemCommands==36&&good.resetCount==2);
  CHECK(good.falconStart&&good.falconHalted&&good.sec2Start);
  CHECK(success.writes.size()==good.writes);const auto writes=success.writes.size();
  const auto replay=run(success,e.p);CHECK(!replay.passed&&replay.failure==StartRejected&&success.writes.size()==writes);
  {IO io;io.alias=true;auto r=run(io,e.p);CHECK(r.passed);
    unsigned aliases=0;for(auto w:io.writes)aliases+=w.first==0x110130||w.first==0x840130;CHECK(aliases==2);}
  {auto p=e.p;p[40]^=1;IO io;auto r=run(io,p);CHECK(!r.passed&&!r.validated&&io.writes.empty()&&io.reads==0);}
  for(unsigned i=1;i<=good.writes;++i){IO io;io.failWrite=int(i);const auto r=run(io,e.p);CHECK(!r.passed&&r.failure==WriteFailed&&io.writeCalls==i);}
  for(unsigned i=1;i<=good.reads;++i){IO io;io.failRead=int(i);const auto r=run(io,e.p);CHECK(!r.passed&&r.failure==Unreadable&&io.reads==i);}
  for(unsigned mode=0;mode<15;++mode){IO io;
    if(mode==0)io.owned=false;
    if(mode==1)io.workspace=false;
    if(mode==2)io.stallMailbox=true;
    if(mode==3)io.stallDma=true;
    if(mode==4)io.stallHalt=true;
    if(mode==5)io.stallHandoff=true;
    if(mode==6)io.staleHandoff=true;
    if(mode==7)io.noRiscv=true;
    if(mode==8)io.wrongSecMailbox=true;
    if(mode==9)io.clockBack=true;
    if(mode==10)io.pointerChanged=true;
    if(mode==11)io.startDenied=true;
    if(mode==12)io.ownerLost=true;
    if(mode==13)io.workspaceLost=true;
    if(mode==14){io.clockStuck=true;io.stallMailbox=true;}
    const auto r=run(io,e.p);CHECK(!r.passed&&r.failure!=None);CHECK(r.ticks<=MaxTicks+1);
    if(mode<3||mode==14)CHECK(io.writes.empty());
  }
  std::printf("Sequencer executor: %u scenarios / %u checks passed; simulated MMIO only.\n",scenarios,checks);
}
