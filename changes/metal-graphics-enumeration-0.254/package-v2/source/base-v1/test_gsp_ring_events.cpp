#include "driver/GSPRingEventProtocol.hpp"
#include "driver/GSPQueueConsume.hpp"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace GSPRingEvents;
static unsigned checks=0,scenarios=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::abort();}}while(0)
static void put(std::vector<unsigned char>&p,unsigned off,unsigned v){for(unsigned i=0;i<4;++i)p[off+i]=static_cast<unsigned char>(v>>(8*i));}
static std::vector<unsigned char> packet(unsigned seq,unsigned fn,unsigned payload=4){
  std::vector<unsigned char> p(((80+payload+4095)/4096)*4096);
  put(p,36,seq);put(p,40,unsigned(p.size()/4096));put(p,48,0x03000000);put(p,52,0x43505256);
  put(p,56,32+payload);put(p,60,fn);unsigned char lanes[4]={};
  for(unsigned i=0;i<((80+payload+7)&~7U);++i)lanes[i%4]^=p[i];
  for(unsigned i=0;i<4;++i)p[32+i]=lanes[i];return p;
}
struct IO {
  std::vector<unsigned char> q=std::vector<unsigned char>(0x81000);
  unsigned start,pages=0,imports=0,reads=0,publishes=0,consumerWrites=0,clockCalls=0;
  U64 ns=1000000;bool owned=true,validProfile=true,claimed=false,producerBack=false,readerMoves=false,partial=false;
  int failRead=-1,failImport=-1;bool failPublish=false,failWrite=false;
  IO(unsigned s):start(s){unsigned h[]={0,0x40000,Page,63,s,1,32,Page};
    for(unsigned i=0;i<8;++i)put(q,0x41000+4*i,h[i]);put(q,0x1020,s);}
  void append(const std::vector<unsigned char> &p){
    CHECK(pages+p.size()/Page<=62);
    for(unsigned i=0;i<p.size()/Page;++i)
      std::memcpy(q.data()+0x42000+((start+pages+i)%63)*Page,p.data()+i*Page,Page);
    pages+=unsigned(p.size()/Page);put(q,0x41010,(start+pages)%63);
  }
  bool ready(){return owned;}
  bool profileValidated(){return validProfile;}
  bool claimConsumption(){if(claimed)return false;claimed=true;return true;}
  bool writeConsumerThree(){++consumerWrites;if(failWrite)return false;put(q,0x1020,3);return true;}
  bool publish(){++publishes;return !failPublish;}
  bool import(){++imports;
    if(producerBack&&imports==3)put(q,0x41010,start);
    if(readerMoves&&imports==3)put(q,0x1020,(start+1)%63);
    if(partial&&imports==2)put(q,0x41010,(start+pages)%63);
    return int(imports)!=failImport;
  }
  bool read(unsigned off,unsigned char *out,unsigned bytes){
    CHECK(off+bytes<=q.size()&&bytes<=Page);++reads;
    if(int(reads)==failRead)return false;std::memcpy(out,q.data()+off,bytes);return true;
  }
  U64 nowNs(){++clockCalls;return ns;}
  void delayUs(unsigned us){CHECK(us<=100);ns+=100000000;}
};
static Result run(IO &io,unsigned seq=2){
  ++scenarios;std::vector<unsigned char> out(MaxBytes),scratch(Page);Result r;
  const auto before=io.q;capture(io,out.data(),scratch.data(),r,io.start,seq);
  if(!io.producerBack&&!io.readerMoves&&!io.partial)CHECK(before==io.q);
  for(unsigned i=0;i<r.count;++i){const auto &row=r.records[i];Record decoded;
    CHECK(decode(out.data()+row.offset,row.bytes,seq+i,decoded));
    CHECK(row.slot==(io.start+row.offset/Page)%63);}
  return r;
}
int main(){
  for(unsigned start=0;start<63;++start){
    IO io(start);io.append(packet(2,0x101e,5000));io.append(packet(3,0x1001));const auto r=run(io);
    CHECK(r.passed&&r.initDone&&r.count==2&&r.pages==3&&r.published==3);
  }
  for(unsigned start:{0U,3U,60U,62U}){
    IO io(start);for(unsigned i=0;i<62;++i)io.append(packet(2+i,0x101e));const auto r=run(io);
    CHECK(r.passed&&r.stop==QueueFull&&r.count==62&&r.published==62);
  }
  {IO io(62);io.append(packet(2,0x101e,5000));put(io.q,0x41010,0);io.partial=true;const auto r=run(io);
    CHECK(r.passed&&r.stop==Deadline&&r.pages==2&&r.partialPolls==1);}
  {IO io(3);io.append(packet(2,0x1002));const auto r=run(io);CHECK(r.passed&&r.sequencer);}
  {IO io(3);io.append(packet(3,0x1001));const auto r=run(io);CHECK(!r.passed&&r.failure==RecordInvalid);}
  {IO io(62);io.append(packet(2,0x101e,5000));io.producerBack=true;const auto r=run(io);CHECK(!r.passed&&r.failure==ProducerRegressed);}
  {IO io(62);io.append(packet(2,0x1001));io.readerMoves=true;const auto r=run(io);CHECK(!r.passed&&r.failure==ReaderChanged);}
  {IO io(63);const auto r=run(io);CHECK(!r.passed&&r.failure==Header&&io.reads==0);}
  {IO io(3);const auto r=run(io,0xffffffffU);CHECK(!r.passed&&r.failure==Header);}
  // The only writer publishes exactly consumer3 after full prefix/profile
  // validation; late failures retain the attempted/written distinction.
  for(unsigned mode=0;mode<10;++mode){IO io(0);io.append(packet(0,0x1020));io.append(packet(1,0x1002,5000));
    if(mode==1)io.validProfile=false;
    if(mode==2)io.owned=false;
    if(mode==3)io.failImport=1;
    if(mode==4)put(io.q,0x1020,1);
    if(mode==5)put(io.q,0x41010,2);
    if(mode==6)io.claimed=true;
    if(mode==7)io.failWrite=true;
    if(mode==8)io.failPublish=true;
    if(mode==9)io.failImport=2;
    GSPQueueConsume::Result r;++scenarios;const bool good=GSPQueueConsume::consume(io,r);
    CHECK(good==(mode==0));CHECK(r.verified==good);
    CHECK(io.consumerWrites==(mode==0||mode>=7?1U:0U));
    if(mode==0){CHECK(r.readerBefore==0&&r.readerAfter==3);GSPQueueConsume::Result again;
      CHECK(!GSPQueueConsume::consume(io,again));CHECK(io.consumerWrites==1);}
  }
  std::printf("GSP continuation ring/consumer: %u scenarios, %u checks passed; simulated queues only.\n",scenarios,checks);
}
