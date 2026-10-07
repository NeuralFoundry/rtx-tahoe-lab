#include "driver/GSPPageTablesProtocol.hpp"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <cstddef>
using namespace GSPPageTables;
using Buffer=std::vector<unsigned char>;
static unsigned long long checks=0,scenarios=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::abort();}}while(0)
struct Sim{
  Buffer memory=Buffer(Bytes,0x5a);unsigned win=0x80173d90,reads=0,writes=0,clocks=0,memoryWrites=0;
  unsigned failRead=0,failWrite=0,mode=0;bool owned=true,claimed=false;U64 ns=100;
  bool ready(){return owned;}
  bool claim(){if(claimed)return false;claimed=true;return true;}
  U64 nowNs(){++clocks;ns+=100;if(mode==2&&clocks==90)return 0;if(mode==3&&clocks>90)ns+=BudgetNs;return ns;}
  unsigned offset(unsigned off){CHECK(!(off&3)&&off>=Start&&off<End&&win==0);return off-Start;}
  bool read(unsigned off,unsigned&v){
    ++reads;if(reads==failRead)return false;
    if(off==Window){v=win;if(mode==1&&reads==2)v^=1;return true;}
    v=GSPContentSeal::get32(memory.data()+offset(off));
    if(mode==4&&memoryWrites>=Words)v^=1;
    if(mode==5&&reads==99)owned=false;
    if(mode==6&&memoryWrites==0)v=0xbad0acff;
    if(mode==7&&memoryWrites==0)v=0xffffffff;
    if(mode==8&&memoryWrites==0)v=0xbadf0000;
    return true;
  }
  bool write(unsigned off,unsigned v){
    ++writes;
    if(off==Window){CHECK(v==0x80173d90||v==0);win=v;}
    else{put32(memory.data()+offset(off),v);++memoryWrites;}
    return writes!=failWrite; // A failed posted write may already be visible.
  }
};
static Result run(Sim&io){
  ++scenarios;Buffer capture(Bytes);Result r;stage(io,capture.data(),r);
  CHECK(r.inspected<=Words&&r.written<=Words&&r.checked<=Words&&r.captured<=Bytes&&r.ticks<=MaxTicks+1);
  if(r.passed){CHECK(r.failure==None&&r.checked==Words&&r.inspected==Words&&r.written==Words&&r.captured==Bytes);CHECK(imageMatches(capture.data(),Bytes));}
  if(io.memoryWrites)CHECK(r.modified&&r.inspected==Words);
  if(r.modified)CHECK(!r.passed||imageMatches(io.memory.data(),Bytes));
  const auto before=io.memory;restoreWindow(io,r);
  CHECK(before==io.memory); // Table backing is retained, never restored/reused.
  if(r.passed)CHECK(r.windowRestored&&io.win==0x80173d90);
  if(io.owned&&r.windowSaved&&!io.failRead&&!io.failWrite&&io.mode!=3)CHECK(r.windowRestored);
  return r;
}
static void checksum(Buffer&b){put32(b.data()+32,0);unsigned sum=0;for(unsigned i=0;i<288;i+=4)sum^=GSPContentSeal::get32(b.data()+i);put32(b.data()+32,sum);}
static void fixtures(const char*output){
  Buffer tables(Bytes),params(ParamsBytes),req(Page);CHECK(image(tables.data(),Bytes));CHECK(parameters(params.data(),ParamsBytes));CHECK(request(req.data(),Page));
  CHECK(RootBytes==32&&index(0)==0&&index(1)==0&&index(2)==128);
  CHECK(get64(tables.data())==0x100322&&get64(tables.data()+4096)==0x100422);
  for(unsigned i=0;i<Bytes;++i){tables[i]^=1;CHECK(!imageMatches(tables.data(),Bytes));tables[i]^=1;}
  CHECK(!image(nullptr,Bytes)&&!image(tables.data(),Bytes-1)&&!imageMatches(nullptr,Bytes)&&!imageMatches(tables.data(),Bytes+1));
  CHECK(!parameters(nullptr,ParamsBytes)&&!parameters(params.data(),ParamsBytes-1)&&!request(nullptr,Page)&&!request(req.data(),Page-1));
  CHECK(GSPContentSeal::get32(params.data()+32)==3&&params[60]==47&&params[84]==38&&params[108]==29);
  CHECK(get64(params.data()+48)==32&&get64(params.data()+72)==4096&&get64(params.data()+96)==4096);
  GSPInitEvents::Record row;CHECK(GSPInitEvents::decode(req.data(),Page,8,row));CHECK(!replyIdentity(req.data(),row));
  Buffer response=req;put32(response.data()+36,12);put32(response.data()+64,0);put32(response.data()+68,0);checksum(response);
  CHECK(GSPInitEvents::decode(response.data(),Page,12,row)&&replyIdentity(response.data(),row));
  for(unsigned i=104;i<288;++i){response[i]^=1;checksum(response);CHECK(GSPInitEvents::decode(response.data(),Page,12,row));CHECK(!replyIdentity(response.data(),row));response[i]^=1;checksum(response);}
  for(unsigned off: {68U,72U,80U,84U,88U,92U,96U,100U}){response[off]^=1;checksum(response);CHECK(GSPInitEvents::decode(response.data(),Page,12,row));CHECK(!replyIdentity(response.data(),row));response[off]^=1;checksum(response);}
  Buffer vas(48);put64(vas.data()+8,(1ULL<<49)-0x4000000);put64(vas.data()+40,0x4000000);
  CHECK(vaspaceRange(vas.data(),48));put64(vas.data()+16,~0ULL);put64(vas.data()+24,~0ULL);CHECK(vaspaceRange(vas.data(),48));
  put64(vas.data()+8,VirtualEnd-1);CHECK(!vaspaceRange(vas.data(),48));put64(vas.data()+8,VirtualEnd);CHECK(vaspaceRange(vas.data(),48));
  put64(vas.data()+40,VirtualStart+1);CHECK(!vaspaceRange(vas.data(),48));put64(vas.data()+40,VirtualStart);CHECK(vaspaceRange(vas.data(),48));
  put64(vas.data()+8,~0ULL);CHECK(!vaspaceRange(vas.data(),48));CHECK(!vaspaceRange(nullptr,48)&&!vaspaceRange(vas.data(),47));
  if(output){std::ofstream f(output,std::ios::binary);CHECK(bool(f));f.write(reinterpret_cast<const char*>(tables.data()),Bytes);f.write(reinterpret_cast<const char*>(params.data()),ParamsBytes);f.write(reinterpret_cast<const char*>(req.data()),Page);CHECK(bool(f));}
}
int main(int argc,char**argv){
  fixtures(argc==2?argv[1]:nullptr);
  Sim baseline;const auto success=run(baseline);CHECK(success.passed);const unsigned totalReads=baseline.reads,totalWrites=baseline.writes;
  for(unsigned i=1;i<=totalReads;++i){Sim io;io.failRead=i;const auto r=run(io);CHECK(!r.passed);}
  for(unsigned i=1;i<=totalWrites;++i){Sim io;io.failWrite=i;const auto r=run(io);CHECK(!r.passed);}
  for(unsigned mode=1;mode<=8;++mode){Sim io;io.mode=mode;const auto r=run(io);CHECK(!r.passed);if(mode>=6)CHECK(io.memoryWrites==0&&!r.modified);}
  {Sim io;io.owned=false;const auto r=run(io);CHECK(r.failure==Owner&&io.reads==0&&io.writes==0);}
  {Sim io;io.claimed=true;const auto r=run(io);CHECK(r.failure==Replay&&io.reads==0&&io.writes==0);}
  {Sim io;Result r;stage(io,nullptr,r);CHECK(r.failure==Storage&&io.reads==0&&io.writes==0);}
  {Sim io;Buffer b(Bytes);Result r;stage(io,b.data(),r);CHECK(r.passed);io.mode=3;restoreWindow(io,r);CHECK(!r.passed&&r.failure==Restore);}
  std::printf("Page tables: %llu scenarios / %llu checks passed; baseline %u reads/%u writes, simulated only.\n",scenarios,checks,totalReads,totalWrites);
}
