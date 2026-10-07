#include "driver/GSPPageTablesSnapshot.hpp"
#include <vector>
#include <cstdio>
#include <cstdlib>
namespace P=GSPPageTables;
namespace S=GSPPageTablesSnapshot;
static unsigned checks=0,scenarios=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::abort();}}while(0)
struct Sim{
  unsigned reads=0,failRead=0,mode=0,clocks=0;P::U64 ns=100;bool owned=true;
  bool ready(){return owned;}
  P::U64 nowNs(){++clocks;ns+=100;if(mode==2&&clocks==9)return 0;if(mode==3&&clocks==9)ns+=P::BudgetNs;return ns;}
  bool read(unsigned off,unsigned&value){CHECK(off>=P::Start&&off<P::End&&!(off&3));++reads;
    if(reads==failRead)return false;value=P::word((off-P::Start)/4);
    if(mode==1&&reads==4)owned=false;
    if(mode==4&&reads==7)value=0xbad0acff;
    if(mode==5&&reads==7)value=0xffffffff;
    if(mode==6&&reads==7)value=0xbadf0000;
    return true;}
};
static S::Result run(Sim&io){++scenarios;std::vector<unsigned char>buf(P::Bytes);S::Result r;S::capture(io,buf.data(),r);
  CHECK(r.words<=P::Words&&r.bytes<=P::Bytes&&r.bytes%4==0);
  if(r.passed)CHECK(!r.failure&&r.words==P::Words&&r.bytes==P::Bytes&&P::imageMatches(buf.data(),P::Bytes));
  return r;}
int main(){
  Sim base;CHECK(run(base).passed);
  for(unsigned i=1;i<=P::Words;++i){Sim io;io.failRead=i;CHECK(!run(io).passed);}
  for(unsigned mode=1;mode<=6;++mode){Sim io;io.mode=mode;CHECK(!run(io).passed);}
  {Sim io;io.owned=false;CHECK(!run(io).passed&&io.reads==0);}
  {Sim io;S::Result r;S::capture(io,nullptr,r);CHECK(r.failure==1&&io.reads==0);}
  std::printf("Page-table snapshot: %u scenarios / %u checks passed; simulated only.\n",scenarios,checks);
}
