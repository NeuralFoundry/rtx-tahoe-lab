#include "driver/GSPPageTablesRMProtocol.hpp"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
namespace P=GSPPageTables;
namespace R=GSPComputePrep;
namespace T=GSPPageTablesRM;
using Buffer=std::vector<unsigned char>;
static unsigned long long checks=0,scenarios=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::abort();}}while(0)
static void put(Buffer&p,unsigned off,unsigned v){CHECK(off+4<=p.size());P::put32(p.data()+off,v);}
static void checksum(Buffer&p){put(p,32,0);unsigned sum=0;const unsigned end=(48+R::get32(p.data()+56)+7)&~7U;CHECK(end<=p.size());
  for(unsigned i=0;i<end;i+=4)sum^=R::get32(p.data()+i);put(p,32,sum);}
struct Sim {
  Buffer q=Buffer(0x81000),prepBytes=Buffer(6*4096);R::Result prep;P::Result tables;
  unsigned reads=0,writes=0,imports=0,publishes=0,bells=0,clocks=0,responseReads=0;
  unsigned failRead=0,failWrite=0,failImport=0,failPublish=0,failBell=0,mode=0,extra=0;
  bool owned=true,claimed=false;P::U64 ns=100;
  Sim(unsigned start=13){
    unsigned h[]={0,0x40000,4096,63,8,1,32,4096};for(unsigned i=0;i<8;++i)put(q,0x1000+i*4,h[i]);
    h[4]=start;for(unsigned i=0;i<8;++i)put(q,0x41000+i*4,h[i]);put(q,0x1020,start);put(q,0x41020,8);
    prep.passed=true;prep.completed=prep.sent=prep.count=prep.pages=6;prep.bytes=6*4096;prep.txWriter=prep.txReader=8;
    prep.rxReader=prep.rxProducer=start;prep.initialReader=(start+63-6)%63;prep.initialSequence=6;prep.rxSequence=12;
    for(unsigned step=0;step<6;++step){
      Buffer p(4096);CHECK(R::request(step,p.data()));put(p,36,step+6);put(p,64,0);put(p,68,0);
      if(step==3){P::put64(p.data()+120,(1ULL<<49)-0x4000000);P::put64(p.data()+152,0x4000000);}
      checksum(p);std::memcpy(prepBytes.data()+step*4096,p.data(),4096);
      prep.records[step]={step*4096,4096,R::function(step),0,step+6,R::headerBytes(step)+R::paramSize(step),step,(prep.initialReader+step)%63,0};
    }
    tables.passed=tables.modified=tables.windowSaved=true;tables.inspected=tables.written=tables.checked=P::Words;tables.captured=P::Bytes;
  }
  bool ready(){return owned;}
  bool claim(){if(claimed)return false;claimed=true;return true;}
  P::U64 nowNs(){++clocks;if(mode==20&&clocks>8)return 0;ns+=100;return ns;}
  void delayUs(unsigned us){CHECK(us==100);ns+=100000000;}
  bool import(){++imports;if(mode==12&&responseReads)put(q,0x41010,R::get32(q.data()+0x1020));return imports!=failImport;}
  bool publish(){++publishes;return publishes!=failPublish;}
  bool read(unsigned off,unsigned char*p,unsigned n){CHECK(n<=4096&&off+n<=q.size());++reads;if(reads==failRead)return false;
    std::memcpy(p,q.data()+off,n);if(off>=0x42000&&n==4096){++responseReads;if(mode==11&&responseReads%2==0)p[128]^=1;}
    if(mode==18&&reads==8)owned=false;return true;}
  bool write(unsigned off,const unsigned char*p,unsigned n){CHECK((off==0x1020||off==0x1010)?n==4:(off==0xa000&&n==4096));
    ++writes;std::memcpy(q.data()+off,p,n);return writes!=failWrite;}
  void append(Buffer&p){const unsigned w=R::get32(q.data()+0x41010);CHECK(p.size()==4096&&w<63);std::memcpy(q.data()+0x42000+w*4096,p.data(),4096);put(q,0x41010,(w+1)%63);}
  bool doorbell(){
    ++bells;if(bells==failBell)return false;if(mode==9)return true;
    CHECK(bells==1&&R::get32(q.data()+0x1010)==9);
    Buffer canonical(4096);CHECK(P::request(canonical.data(),4096));CHECK(std::memcmp(q.data()+0xa000,canonical.data(),4096)==0);
    unsigned sequence=12;
    for(unsigned i=0;i<extra;++i){Buffer p(4096);put(p,36,sequence++);put(p,40,1);put(p,48,0x03000000);put(p,52,0x43505256);put(p,56,40);put(p,60,0x100c);checksum(p);append(p);}
    Buffer p=canonical;put(p,36,sequence);put(p,64,0);put(p,68,0);
    if(mode==1)put(p,64,0x56);
    if(mode==2)put(p,92,0x56);
    if(mode==3)put(p,80,0xbad);
    if(mode==4)put(p,96,1);
    if(mode==5)put(p,60,0x1003);
    if(mode==6)put(p,36,99);
    if(mode==7)put(p,100,1);
    if(mode==13)put(p,40,2);
    if(mode==14)put(p,68,1);
    if(mode==15)put(p,72,1);
    if(mode==16)put(p,84,0xcf000002);
    if(mode==17)put(p,144,0xabcdef);
    if(mode==19)put(p,88,0xbad);
    checksum(p);if(mode==8)p[112]^=1;append(p);if(mode!=10)put(q,0x41020,9);return true;
  }
};
static R::Result run(Sim&io){
  ++scenarios;Buffer out(R::MaxBytes),req(4096),scratch(4096);R::Result r;
  T::execute(io,io.prep,io.prepBytes.data(),io.tables,out.data(),req.data(),scratch.data(),r);
  CHECK(r.sent<=1&&r.doorbells<=1&&r.completed<=1&&r.count<=R::MaxRecords&&r.pages<=R::MaxPages&&r.ticks<=R::MaxTicks+1);
  if(r.passed){CHECK(r.completed==1&&r.sent==1&&r.doorbells==1&&r.txWriter==9&&r.txReader==9&&r.lastFunction==76&&r.lastResult==0&&r.lastParamStatus==0);}
  return r;
}
int main(int argc,char**argv){
  if(argc==2){
    Sim real;
    for(unsigned i=0;i<6;++i){
      const std::string path=std::string(argv[1])+"/record-"+(i+6<10?"00":"0")+std::to_string(i+6)+".bin";
      std::ifstream f(path,std::ios::binary);CHECK(bool(f));Buffer bytes(std::istreambuf_iterator<char>(f),{});CHECK(bytes.size()==4096);
      std::memcpy(real.prepBytes.data()+i*4096,bytes.data(),4096);
    }
    CHECK(T::prefix(real.prep,real.prepBytes.data()));CHECK(run(real).passed);
    std::puts("Actual 0.21 six-RM transcript and VASPACE bounds accepted by new prefix validator.");
  }
  Sim baseline;CHECK(T::prefix(baseline.prep,baseline.prepBytes.data()));CHECK(run(baseline).passed);
  for(unsigned start: {0U,1U,61U,62U}){Sim io(start);CHECK(run(io).passed);}
  {Sim io;io.extra=3;CHECK(run(io).passed);}
  {Sim io;io.extra=16;CHECK(!run(io).passed);}
  for(unsigned mode=1;mode<=20;++mode){Sim io;io.mode=mode;CHECK(!run(io).passed);}
  for(unsigned i=1;i<=baseline.reads;++i){Sim io;io.failRead=i;CHECK(!run(io).passed);}
  for(unsigned i=1;i<=baseline.writes;++i){Sim io;io.failWrite=i;CHECK(!run(io).passed);}
  for(unsigned i=1;i<=baseline.imports;++i){Sim io;io.failImport=i;CHECK(!run(io).passed);}
  for(unsigned i=1;i<=baseline.publishes;++i){Sim io;io.failPublish=i;CHECK(!run(io).passed);}
  {Sim io;io.failBell=1;CHECK(!run(io).passed);}
  {Sim io;io.owned=false;CHECK(!run(io).passed&&io.writes==0);}
  {Sim io;io.claimed=true;CHECK(!run(io).passed&&io.writes==0);}
  {Sim io;io.tables.passed=false;CHECK(!run(io).passed&&io.writes==0);}
  {Sim io;io.tables.checked=1;CHECK(!run(io).passed&&io.writes==0);}
  {Sim io;io.tables.windowRestored=true;CHECK(!run(io).passed&&io.writes==0);}
  {Sim io;io.prep.rxSequence=0;CHECK(!run(io).passed&&io.writes==0);}
  {Sim io;io.prep.records[3].offset=0;CHECK(!run(io).passed&&io.writes==0);}
  for(unsigned i=0;i<6;++i){Sim io;io.prepBytes[i*4096+64]^=1;CHECK(!run(io).passed&&io.writes==0);}
  {Sim io;io.prep.rxReader=63;CHECK(!run(io).passed&&io.writes==0);}
  {Sim io;put(io.q,0x41020,7);CHECK(!run(io).passed&&io.writes==0);}
  {Sim io;put(io.q,0x1020,12);CHECK(!run(io).passed&&io.writes==0);}
  {Sim io;CHECK(run(io).passed);const auto writes=io.writes;CHECK(!run(io).passed&&io.writes==writes);}
  std::printf("Page table RM: %llu scenarios / %llu checks passed; simulated only.\n",scenarios,checks);
}
