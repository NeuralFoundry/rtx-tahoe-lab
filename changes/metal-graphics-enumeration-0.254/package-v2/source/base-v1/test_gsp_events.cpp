#include "driver/GSPEventProtocol.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace GSPEvents;
static unsigned checks=0,scenarios=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::abort();}}while(0)
static void put(std::vector<unsigned char> &p,unsigned offset,unsigned value){
  for(unsigned i=0;i<4;++i)p[offset+i]=static_cast<unsigned char>(value>>(8*i));
}
static void checksum(std::vector<unsigned char> &p){
  put(p,32,0);const unsigned end=(48+GSPContentSeal::get32(p.data()+56)+7)&~7U;
  unsigned char lanes[4]={};for(unsigned i=0;i<end;++i)lanes[i%4]^=p[i];
  for(unsigned i=0;i<4;++i)p[32+i]=lanes[i];
}
static std::vector<unsigned char> packet(unsigned seq,unsigned function,unsigned payload=4){
  const unsigned count=(80+payload+4095)/4096;std::vector<unsigned char> p(count*Page);
  put(p,36,seq);put(p,40,count);put(p,48,0x03000000);put(p,52,0x43505256);
  put(p,56,32+payload);put(p,60,function);put(p,68,0xffffffff);checksum(p);return p;
}
struct IO {
  std::vector<unsigned char> queue=std::vector<unsigned char>(0x81000);
  U64 ns=1000000,step=100000000; // Model time advances in delayUs, independent of wall time.
  unsigned imported=0,appended=0,clockCalls=0,reads=0;
  int failImport=-1,failRead=-1,changeSlot=-1;
  unsigned slotReads[MaxPages]={};
  bool owned=true,partial=false,regress=false,moveReader=false,clockBack=false,clockStuck=false;
  bool loseOwner=false;
  IO(){unsigned h[]={0,0x40000,Page,63,0,1,32,Page};
    for(unsigned i=0;i<8;++i)put(queue,GSPFirstStatus::StatusOffset+4*i,h[i]);}
  void append(const std::vector<unsigned char> &p){
    CHECK(appended+p.size()/Page<=MaxPages);
    std::memcpy(queue.data()+GSPFirstStatus::EntriesOffset+appended*Page,p.data(),p.size());
    appended+=static_cast<unsigned>(p.size()/Page);publish(appended);
  }
  void publish(unsigned n){put(queue,GSPFirstStatus::StatusOffset+16,n);}
  bool ready(){return owned && !(loseOwner && imported>1);}
  U64 nowNs(){++clockCalls;return clockBack && clockCalls>2?0:ns;}
  bool import(){++imported;
    if(partial && imported==2)publish(appended);
    if(regress && imported==3)publish(0);
    if(moveReader && imported==3)put(queue,HostStatusReadOffset,1);
    return int(imported)!=failImport;
  }
  bool read(unsigned off,unsigned char *dst,unsigned n){
    ++reads;CHECK(off<=queue.size() && n<=queue.size()-off && n<=Page);
    if(int(reads)==failRead)return false;
    std::memcpy(dst,queue.data()+off,n);
    if(off>=GSPFirstStatus::EntriesOffset){const unsigned slot=(off-GSPFirstStatus::EntriesOffset)/Page;
      CHECK(slot<MaxPages);++slotReads[slot];
      if(int(slot)==changeSlot && slotReads[slot]>1)dst[100]^=1;}
    return true;
  }
  void delayUs(unsigned us){CHECK(us<=100);if(!clockStuck)ns+=step;}
};
static void run(IO &io,Result &r){
  ++scenarios;std::vector<unsigned char> out(MaxBytes),scratch(Page);
  const auto before=io.queue;capture(io,out.data(),scratch.data(),r);
  if(!io.partial && !io.regress && !io.moveReader)CHECK(io.queue==before);
  unsigned cursor=0;
  for(unsigned i=0;i<r.count;++i){const auto &row=r.records[i];
    CHECK(row.offset==cursor && row.slot==cursor/Page && row.sequence==i);
    Record independent;CHECK(decode(out.data()+row.offset,row.bytes,i,independent));cursor+=row.bytes;
  }
  CHECK(cursor==r.bytes && r.pages*Page==r.bytes);CHECK(r.count<=MaxRecords && r.pages<=MaxPages);
}
int main(int argc,char **argv){
  {IO io;put(io.queue,GSPFirstStatus::StatusOffset+32,2);io.append(packet(0,0x1001));
    Result r;run(io,r);CHECK(r.passed && r.initDone && r.reader==0);}
  {IO io;io.append(packet(0,0x1020,1212));io.append(packet(1,0x101e));io.append(packet(2,0x1001));
    Result r;run(io,r);CHECK(r.passed && r.initDone && r.stop==InitDone);CHECK(r.count==3 && r.nocatCount==1);CHECK(!r.sequencer);}
  {IO io;io.append(packet(0,0x1020));io.append(packet(1,0x1002,5000));io.append(packet(2,0x1001));
    Result r;run(io,r);CHECK(r.passed && r.sequencer && r.stop==Sequencer);CHECK(r.count==2 && r.pages==3 && !r.initDone);}
  {IO io;io.append(packet(0,0x1020));Result r;run(io,r);CHECK(r.passed && r.stop==Deadline && r.count==1);CHECK(r.elapsedNs>=DurationNs);}
  {IO io;Result r;run(io,r);CHECK(!r.passed && r.stop==Deadline && r.count==0);}
  {IO io;io.clockStuck=true;Result r;run(io,r);CHECK(!r.passed && r.stop==Deadline && r.polls==MaxPolls);}
  {IO io;for(unsigned i=0;i<62;++i)io.append(packet(i,0x101e));Result r;run(io,r);
    CHECK(r.passed && r.stop==QueueFull && r.count==62 && r.bytes==MaxBytes);}
  {IO io;io.append(packet(0,0x101e,5000));io.publish(1);io.partial=true;Result r;run(io,r);
    CHECK(r.passed && r.count==1 && r.pages==2 && r.partialPolls==1 && r.pendingPages==0);}
  {IO io;io.append(packet(0,0x101e));io.append(packet(1,0x101e,5000));io.publish(2);Result r;run(io,r);
    CHECK(r.passed && r.stop==Deadline && r.count==1 && r.pendingPages==2);}
  {IO io;io.append(packet(0,0x101e));io.append(packet(2,0x1001));Result r;run(io,r);
    CHECK(!r.passed && r.count==1 && r.failure==RecordInvalid && r.failedSlot==1);}
  {IO io;io.append(packet(0,0x101e));io.append(packet(1,0x1001));io.changeSlot=1;Result r;run(io,r);
    CHECK(!r.passed && r.count==1 && r.failure==RecordChanged && !r.initDone);}
  for(unsigned off:{0U,32U,44U,48U,52U,76U,80U,84U}){
    IO io;auto p=packet(0,0x1001);p[off]^=1;io.append(p);Result r;run(io,r);CHECK(!r.passed && r.failure==RecordInvalid);}
  for(unsigned mode=0;mode<10;++mode){IO io;io.append(packet(0,0x101e));Result r;
    if(mode==0)io.owned=false;
    if(mode==1)io.failImport=1;
    if(mode==2)io.failRead=1;
    if(mode==3)io.regress=true;
    if(mode==4)io.moveReader=true;
    if(mode==5)io.clockBack=true;
    if(mode==6)io.loseOwner=true;
    if(mode==7)put(io.queue,GSPFirstStatus::StatusOffset+12,64);
    if(mode==8)put(io.queue,GSPFirstStatus::EntriesOffset+40,17);
    if(mode==9)io.publish(63);
    run(io,r);CHECK(!r.passed && r.stop==Error && !r.initDone);
  }
  for(unsigned call=2;call<=6;++call){IO io;io.append(packet(0,0x1001));io.failRead=int(call);Result r;run(io,r);CHECK(!r.passed && r.failure==Read);}
  {IO io;io.append(packet(0,0x1001));io.failImport=2;Result r;run(io,r);CHECK(!r.passed && r.failure==Import);}
  {IO io;auto p=packet(0,0x1001);put(p,64,1);checksum(p);io.append(p);Result r;run(io,r);CHECK(r.passed && r.stop==Deadline && !r.initDone);}
  if(argc==2){char path[1024];std::snprintf(path,sizeof(path),"%s/events.bin",argv[1]);FILE *f=nullptr;
#ifdef _MSC_VER
    fopen_s(&f,path,"rb");
#else
    f=std::fopen(path,"rb");
#endif
    CHECK(f!=nullptr);std::vector<unsigned char> data(MaxBytes);
    const auto bytes=std::fread(data.data(),1,data.size(),f);CHECK(std::fgetc(f)==EOF);std::fclose(f);
    CHECK(bytes && bytes%Page==0);data.resize(bytes);IO io;io.append(data);Result r;run(io,r);
    CHECK(r.passed && r.initDone && r.count==3 && r.pages==4 && r.nocatCount==1);
  }
  std::printf("GSP event collector: %u scenarios passed; no queue writes; bounded time/space\n",scenarios);
}
