#include "driver/GSPFirstStatus.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace GSPFirstStatus;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::abort();}}while(0)
static void put(std::vector<unsigned char> &p,unsigned offset,unsigned value){
  for(unsigned i=0;i<4;++i)p[offset+i]=static_cast<unsigned char>(value>>(8*i));
}
static void checksum(std::vector<unsigned char> &p){
  put(p,32,0);unsigned end=(48+GSPContentSeal::get32(p.data()+56)+7)&~7U;
  unsigned char lanes[4]={};for(unsigned i=0;i<end;++i)lanes[i%4]^=p[i];
  for(unsigned i=0;i<4;++i)p[32+i]=lanes[i];
}
static std::vector<unsigned char> packet(unsigned function=0x1001,unsigned payload=4){
  unsigned count=(80+payload+4095)/4096;
  std::vector<unsigned char> p(count*4096);
  put(p,40,count);put(p,48,0x03000000);put(p,52,0x43505256);put(p,56,32+payload);
  put(p,60,function);put(p,68,0xffffffff);put(p,80,0x12345678);checksum(p);return p;
}
struct IO {
  std::vector<unsigned char> queue=std::vector<unsigned char>(0x81000);
  bool owned=true,imports=true,reads=true,change=false;
  unsigned imported=0,entryReads=0,delay=0;
  IO(){unsigned h[]={0,0x40000,4096,63,1,1,32,4096};
    for(unsigned i=0;i<8;++i)put(queue,StatusOffset+i*4,h[i]);
    const auto p=packet();std::memcpy(queue.data()+EntriesOffset,p.data(),p.size());}
  bool ready(){return owned;}
  bool import(){++imported;return imports;}
  bool read(unsigned off,unsigned char *dst,unsigned n){
    CHECK(off<=queue.size() && n<=queue.size()-off);
    if(!reads)return false;
    std::memcpy(dst,queue.data()+off,n);
    if(off>=EntriesOffset && ++entryReads>1 && change)dst[100]^=1;
    return true;
  }
  void delayUs(unsigned us){delay+=us;}
};
int main(int argc,char **argv){
  {auto p=packet();Result r;CHECK(record(p.data(),static_cast<unsigned>(p.size()),r));
    CHECK(r.captured && r.checksumValid && r.initDone && !r.sequencer);CHECK(r.payloadBytes==4);}
  {auto p=packet(0x1002,5000);Result r;CHECK(record(p.data(),static_cast<unsigned>(p.size()),r));
    CHECK(r.sequencer && !r.initDone && r.recordBytes==8192);}
  {auto p=packet();p[4095]=255;Result r;CHECK(record(p.data(),4096,r));}
  const unsigned corrupt[]={0,32,36,40,44,48,52,56,76,80,84};
  for(unsigned off:corrupt){auto p=packet();p[off]^=1;Result r;CHECK(!record(p.data(),4096,r));CHECK(!r.captured);}
  for(unsigned mode=0;mode<6;++mode){auto p=packet();
    if(mode==0)put(p,36,1);
    if(mode==1)put(p,44,1);
    if(mode==2)put(p,48,0);
    if(mode==3)put(p,76,1);
    if(mode==4)put(p,60,71);
    if(mode==5)put(p,56,31);
    checksum(p);Result r;CHECK(!record(p.data(),4096,r));}
  {auto p=packet();put(p,64,1);checksum(p);Result r;CHECK(record(p.data(),4096,r));CHECK(!r.initDone);}
  {auto p=packet(0x1001,8);Result r;CHECK(record(p.data(),4096,r));CHECK(!r.initDone);}
  std::vector<unsigned char> out(MaximumBytes),scratch(4096);
  {IO io;const auto before=io.queue;Result r;capture(io,out.data(),scratch.data(),r);
    CHECK(r.captured && r.initDone && r.headerValid);CHECK(io.queue==before);CHECK(io.imported==2);}
  for(unsigned mode=0;mode<8;++mode){IO io;Result r;
    if(mode==0)io.owned=false;
    if(mode==1)io.imports=false;
    if(mode==2)io.reads=false;
    if(mode==3)put(io.queue,StatusOffset+16,0);
    if(mode==4)put(io.queue,StatusOffset+12,64);
    if(mode==5)put(io.queue,EntriesOffset+40,2);
    if(mode==6)io.change=true;
    if(mode==7)put(io.queue,StatusOffset+16,63);
    const auto before=io.queue;capture(io,out.data(),scratch.data(),r);CHECK(!r.captured);CHECK(io.queue==before);
    if(mode==3)CHECK(r.polls==20000);
  }
  if(argc==2){const char *names[]={"init-done.bin","sequencer.bin","multi-page.bin"};
    for(unsigned i=0;i<3;++i){char path[1024];std::snprintf(path,sizeof(path),"%s/%s",argv[1],names[i]);
      FILE *f=nullptr;
#ifdef _MSC_VER
      fopen_s(&f,path,"rb");
#else
      f=std::fopen(path,"rb");
#endif
      CHECK(f!=nullptr);std::size_t size=std::fread(out.data(),1,out.size(),f);CHECK(std::fgetc(f)==EOF);std::fclose(f);
      Result r;CHECK(record(out.data(),static_cast<unsigned>(size),r));CHECK(i==0?r.initDone:r.sequencer);
    }
  }
  std::printf("GSP first status: %u checks passed; no queue writes\n",checks);
}
