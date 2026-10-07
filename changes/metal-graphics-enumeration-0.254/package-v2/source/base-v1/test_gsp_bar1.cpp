#include "driver/GSPBar1Protocol.hpp"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <fstream>
using namespace GSPBar1;
static unsigned checks=0,scenarios=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::abort();}}while(0)
using Buffer=std::vector<unsigned char>;
struct Sim{
  Buffer bytes=Buffer(Bytes);unsigned win=0x12340,reads=0,writes=0,clocks=0,claims=0,memoryWrites=0;
  unsigned failRead=0,failWrite=0,mode=0;bool owned=true,claimed=false;U64 ns=100;
  Sim(){for(unsigned i=0;i<Bytes;++i)bytes[i]=static_cast<unsigned char>((i*23+i/16)^0x75);}
  bool ready(){return owned;}
  bool claim(){++claims;if(claimed)return false;claimed=true;return true;}
  U64 nowNs(){++clocks;ns+=100;if(mode==2&&clocks==90)return 0;if(mode==3&&clocks>90)ns+=BudgetNs;return ns;}
  unsigned offset(unsigned off){CHECK(!(off&3)&&off>=Aperture&&off<Aperture+Bytes&&win==0);return off-Aperture;}
  bool read(unsigned off,unsigned&v){
    ++reads;if(reads==failRead)return false;
    if(off==Window){v=win;if(mode==1&&reads==2)v^=1;return true;}
    const unsigned at=offset(off);v=GSPContentSeal::get32(bytes.data()+at);
    if(mode==4&&memoryWrites>=Words)v^=1;
    if(mode==6&&memoryWrites==0)v=0xbad0acff;
    if(mode==7&&memoryWrites==0)v=0xffffffff;
    if(mode==5&&reads==99)owned=false;
    return true;
  }
  bool write(unsigned off,unsigned v){
    ++writes;
    // Failure after mutation models a posted write whose completion is unknown.
    if(off==Window){CHECK(v==0x12340||v==windowValue(0)||v==windowValue(Words-1));win=v;}
    else{const unsigned at=offset(off);GSPComputePrep::put32(bytes.data()+at,v);++memoryWrites;}
    return writes!=failWrite;
  }
};
static Result run(Sim&io,Buffer*captured=nullptr){
  ++scenarios;Buffer original(Bytes),capture(CaptureBytes);const auto before=io.bytes;Result r;
  execute(io,original.data(),capture.data(),r);
  CHECK(r.savedWords<=Words&&r.writtenWords<=Words*2&&r.checkedWords<=Words*2&&r.restoredWords<=Words&&r.capturedBytes<=CaptureBytes);
  CHECK(r.ticks<=MaxTicks+1);
  if(r.passed){CHECK(r.failure==None&&r.savedWords==Words&&r.checkedWords==Words*2&&r.writtenWords==Words*2);
    CHECK(r.originalRestored&&r.windowRestored&&io.win==0x12340&&io.bytes==before&&r.restoredWords==Words);
    for(unsigned pass=0;pass<2;++pass)for(unsigned i=0;i<Words;++i)
      CHECK(GSPContentSeal::get32(capture.data()+pass*Bytes+i*4)==pattern(i,pass));
  }
  if(r.originalRestored)CHECK(io.bytes==before);
  if(captured)*captured=capture;
  return r;
}
int main(int argc,char**argv){
  Sim base;Buffer capture;const auto good=run(base,&capture);CHECK(good.passed&&good.windowChanges==1);
  if(argc==2){std::ofstream f(argv[1],std::ios::binary);CHECK(bool(f));f.write(reinterpret_cast<const char*>(capture.data()),capture.size());CHECK(bool(f));}
  for(unsigned n=1;n<=base.reads;++n){Sim io;io.failRead=n;CHECK(!run(io).passed);}
  for(unsigned n=1;n<=base.writes;++n){Sim io;io.failWrite=n;CHECK(!run(io).passed);}
  for(unsigned mode=1;mode<=7;++mode){Sim io;io.mode=mode;CHECK(!run(io).passed);}
  for(unsigned mode:{6U,7U}){Sim io;io.mode=mode;auto r=run(io);CHECK(!r.passed&&io.memoryWrites==0&&r.originalRestored&&r.windowRestored);}
  {Sim io;io.owned=false;CHECK(!run(io).passed&&io.writes==0&&io.claims==0);}
  {Sim io;io.claimed=true;CHECK(!run(io).passed&&io.writes==0);}
  {Sim io;CHECK(run(io).passed);unsigned prior=io.writes;CHECK(!run(io).passed&&io.writes==prior);}
  {Sim io;Result r;Buffer b(Bytes);execute(io,b.data(),b.data(),r);CHECK(!r.passed&&io.writes==0);}
  std::printf("BAR1: %u scenarios / %u checks passed; baseline %u reads/%u writes, simulated only.\n",scenarios,checks,base.reads,base.writes);
}
