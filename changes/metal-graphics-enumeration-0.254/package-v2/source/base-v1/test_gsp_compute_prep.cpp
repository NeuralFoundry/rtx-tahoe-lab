#include "driver/GSPComputePrepProtocol.hpp"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <cstddef>
using namespace GSPComputePrep;
static unsigned checks=0,scenarios=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::abort();}}while(0)
using Bytes=std::vector<unsigned char>;
// Natural ABI layout transcribed from pinned NVIDIA cl0000/cl0080/cl2080.
struct RootAbi{unsigned client,pid;char name[100];alignas(8) unsigned long long osPid;};
struct DeviceAbi{unsigned id,share,targetClient,targetDevice,flags;alignas(8) unsigned long long size,start,limit;unsigned mode;};
static_assert(sizeof(RootAbi)==120&&offsetof(RootAbi,osPid)==112,"NV0000 ABI");
static_assert(sizeof(DeviceAbi)==56&&offsetof(DeviceAbi,size)==24&&offsetof(DeviceAbi,mode)==48,"NV0080 ABI");
struct VaspaceAbi{unsigned index,flags;alignas(8) unsigned long long size,start,limit;unsigned bigPage;alignas(8) unsigned long long base;};
struct FifoEntryAbi{unsigned engine[16],pbdma[2],fault[2],count;char name[16];};
struct FifoAbi{unsigned base,count;unsigned char more;FifoEntryAbi entries[32];};
struct GrBufferAbi{unsigned size,alignment;};
struct GrAbi{GrBufferAbi engines[8][26];};
static_assert(sizeof(VaspaceAbi)==48&&offsetof(VaspaceAbi,base)==40,"VASPACE ABI");
static_assert(sizeof(FifoEntryAbi)==100&&sizeof(FifoAbi)==3212&&offsetof(FifoAbi,entries)==12,"FIFO ABI");
static_assert(sizeof(GrAbi)==1664,"GR context ABI");
static void put(Bytes&p,unsigned off,unsigned v){CHECK(off+4<=p.size());for(unsigned i=0;i<4;++i)p[off+i]=static_cast<unsigned char>(v>>(i*8));}
static void checksum(Bytes&p){put(p,32,0);unsigned sum=0;const unsigned end=(48+get32(p.data()+56)+7)&~7U;
  CHECK(end<=p.size());for(unsigned i=0;i<end;++i)sum^=unsigned(p[i])<<((i%4)*8);put(p,32,sum);}
static Bytes packet(unsigned sequence,unsigned function,unsigned payload){
  Bytes p(((80+payload+4095)/4096)*4096);put(p,36,sequence);put(p,40,unsigned(p.size()/4096));
  put(p,48,0x03000000);put(p,52,0x43505256);put(p,56,payload+32);put(p,60,function);checksum(p);return p;
}
static Bytes load(const char*path){std::ifstream f(path,std::ios::binary);CHECK(bool(f));return Bytes(std::istreambuf_iterator<char>(f),{});}
struct Sim {
  Bytes q=Bytes(0x81000),initBytes;GSPInitEvents::Result init;
  unsigned reads=0,writes=0,imports=0,publishes=0,bells=0,claims=0,clocks=0;
  unsigned failRead=0,failWrite=0,failImport=0,failPublish=0,failBell=0;
  unsigned nextSequence=6,mutation=0,mutationStep=0,asyncCount=0,responseReads=0;bool owned=true,claimed=false,backClock=false;
  U64 ns=1000000;
  Sim(unsigned extra=0){
    unsigned h[]={0,0x40000,4096,63,2,1,32,4096};for(unsigned i=0;i<8;++i)put(q,0x1000+i*4,h[i]);
    h[4]=(7+extra)%63;for(unsigned i=0;i<8;++i)put(q,0x41000+i*4,h[i]);put(q,0x1020,3);put(q,0x41020,2);
    init.startSlot=3;init.startSequence=2;init.producer=h[4];init.passed=init.initDone=init.headerValid=true;init.stop=GSPInitEvents::InitDone;
    for(unsigned i=0;i<4+extra;++i){const bool last=i==3+extra;Bytes p=packet(i+2,last?0x1001:i==0?0x1020:0x100c,last?0:i==0?1212:8);
      GSPInitEvents::Record row;CHECK(GSPInitEvents::decode(p.data(),unsigned(p.size()),i+2,row));
      row.offset=init.bytes;row.slot=(3+i)%63;init.records[init.count++]=row;init.bytes+=unsigned(p.size());++init.pages;
      initBytes.insert(initBytes.end(),p.begin(),p.end());}
    nextSequence=6+extra;
  }
  bool ready(){return owned;}
  bool claim(){++claims;if(claimed)return false;claimed=true;return true;}
  U64 nowNs(){++clocks;if(backClock&&clocks>8)return 0;ns+=100;return ns;}
  void delayUs(unsigned us){CHECK(us==100);ns+=100000000;}
  bool import(){++imports;
    if(mutation==12&&responseReads)put(q,0x41010,get32(q.data()+0x1020));
    return imports!=failImport;}
  bool publish(){++publishes;return publishes!=failPublish;}
  bool read(unsigned off,unsigned char*p,unsigned n){CHECK(n<=Page&&off+n<=q.size());++reads;
    if(reads==failRead)return false;std::memcpy(p,q.data()+off,n);
    if(off>=0x42000&&n==Page){++responseReads;if(mutation==11&&responseReads%2==0)p[128]^=1;}
    if(mutation==18&&reads==8)owned=false;
    return true;}
  bool write(unsigned off,const unsigned char*p,unsigned n){CHECK((off==0x1020||off==0x1010)?n==4:(off>=0x4000&&off<=0x9000&&n==Page));
    ++writes;if(writes==failWrite)return false;std::memcpy(q.data()+off,p,n);return true;}
  void append(Bytes p){const unsigned w=get32(q.data()+0x41010);CHECK(p.size()==Page);
    std::memcpy(q.data()+0x42000+w*Page,p.data(),p.size());put(q,0x41010,(w+1)%63);}
  bool doorbell(){
    ++bells;if(bells==failBell)return false;if(mutation==9)return true;
    const unsigned w=get32(q.data()+0x1010);CHECK(w==2+bells);
    const auto *req=q.data()+0x2000+(w-1)*Page;
    const unsigned step=bells-1;
    CHECK(get32(req+36)==w-1&&get32(req+60)==(step<4?103U:76U)&&get32(req+64)==~0U);
    CHECK(get32(req+80)==0xc1e00004U);
    const unsigned sizes[]={120,56,4,48,3212,1664},classes[]={0,0x80,0x2080,0x90f1};
    if(step<4){CHECK(get32(req+88)==0xcf000000U+step);CHECK(get32(req+100)==sizes[step]&&get32(req+92)==classes[step]);}
    else{CHECK(get32(req+84)==0xcf000002U&&get32(req+88)==(step==4?0x20801112U:0x20800a32U));
      CHECK(get32(req+92)==0&&get32(req+96)==sizes[step]&&get32(req+100)==0);
      for(unsigned i=104;i<104+sizes[step];++i)CHECK(req[i]==0);}
    if(step==0)for(unsigned i=112;i<232;++i)CHECK(req[i]==0);
    if(step==1)CHECK(get32(req+116)==0xc1e00004U);
    if(step==3)for(unsigned i=112;i<160;++i)CHECK(req[i]==0);
    for(unsigned i=0;i<asyncCount;++i){auto log=packet(nextSequence++,0x100c,8);append(log);}
    Bytes p(req,req+Page);put(p,36,nextSequence++);put(p,64,0);put(p,68,0);
    if(step==mutationStep){
    if(mutation==1)put(p,64,0x56);
    if(mutation==2)put(p,step<4?96:92,0x56);
    if(mutation==3)put(p,80,0xbad);
    if(mutation==4)put(p,step<4?100:96,1);
    if(mutation==5)put(p,60,0x1003);
    if(mutation==6)put(p,36,99);
    if(mutation==7)put(p,step<4?108:100,1);
    if(mutation==13)put(p,40,2);
    if(mutation==14)put(p,68,1);
    if(mutation==15)put(p,72,1);
    if(mutation==19)put(p,step<4?88:88,0xbad);
    }
    checksum(p);if(mutation==8&&step==mutationStep)p[112]^=1;
    append(p);if(mutation!=10)put(q,0x41020,w);
    if(mutation==16)put(q,0x1010,62);
    if(mutation==17)put(q,0x41020,0);
    return true;
  }
};
static Result run(Sim&io){++scenarios;Bytes out(MaxBytes),req(RequestBytes),scratch(Page);Result r;
  execute(io,io.init,io.initBytes.data(),out.data(),req.data(),scratch.data(),r);
  CHECK(!r.passed||r.failure==None);CHECK(r.sent<=6&&r.completed<=r.sent&&r.doorbells<=r.sent&&r.count<=16&&r.pages<=32);
  if(r.passed){CHECK(r.completed==6&&r.sent==6&&r.doorbells==6&&r.prefixConsumed&&r.attempted);
    CHECK(r.txReader==8&&r.txWriter==8);CHECK(r.rxReader==r.rxProducer);CHECK(r.consumerWrites==r.count+1);
    unsigned replies=0;for(unsigned i=0;i<r.count;++i){const auto &row=r.records[i];GSPInitEvents::Record decoded;
      CHECK(GSPInitEvents::decode(out.data()+row.offset,row.bytes,row.sequence,decoded));if(row.function==103||row.function==76)++replies;}
    CHECK(replies==6);}
  return r;
}
int main(int argc,char**argv){
  if(argc>=2){auto p=load(argv[1]);GSPInitEvents::Record row;CHECK(p.size()==4096);
    CHECK(GSPInitEvents::decode(p.data(),unsigned(p.size()),5,row));CHECK(row.function==0x1001&&!row.result&&!row.payloadBytes&&(row.flags&1));
    std::puts("Actual 0.17 empty INIT_DONE packet accepted with checksum/sequence verification.");}
  if(argc>=3){Bytes p(Page);std::ofstream f(argv[2],std::ios::binary);CHECK(bool(f));
    for(unsigned i=0;i<6;++i){CHECK(request(i,p.data()));f.write(reinterpret_cast<const char*>(p.data()),Page);}CHECK(bool(f));}
  for(unsigned bytes=0;bytes<12;++bytes){auto p=packet(5,0x1001,bytes);GSPInitEvents::Record row;
    CHECK(GSPInitEvents::decode(p.data(),unsigned(p.size()),5,row));CHECK(bool(row.flags&1)==(bytes==0||bytes==4));
    put(p,64,1);checksum(p);CHECK(GSPInitEvents::decode(p.data(),unsigned(p.size()),5,row));CHECK(!(row.flags&1));}
  for(unsigned step=3;step<6;++step)for(unsigned mode:{1U,2U,3U,4U,7U,14U,15U,19U}){
    Sim io;io.mutation=mode;io.mutationStep=step;auto r=run(io);CHECK(!r.passed&&r.completed==step&&r.count==step+1);}
  {Bytes p(Page);CHECK(!request(6,p.data())&&!request(0,nullptr));}
  Sim baseline;auto good=run(baseline);CHECK(good.passed&&good.count==6);
  for(unsigned extra:{1U,52U,55U,56U,58U}){Sim io(extra);CHECK(run(io).passed);}
  {Sim io;io.asyncCount=1;CHECK(run(io).passed);}
  {Sim io;io.asyncCount=5;CHECK(!run(io).passed);}
  for(unsigned mode=1;mode<=18;++mode){Sim io;io.mutation=mode;auto r=run(io);CHECK(!r.passed&&r.failure!=None&&r.completed==0);}
  for(unsigned n=1;n<=baseline.reads;++n){Sim io;io.failRead=n;CHECK(!run(io).passed);}
  for(unsigned n=1;n<=baseline.writes;++n){Sim io;io.failWrite=n;CHECK(!run(io).passed);}
  for(unsigned n=1;n<=baseline.imports;++n){Sim io;io.failImport=n;CHECK(!run(io).passed);}
  for(unsigned n=1;n<=baseline.publishes;++n){Sim io;io.failPublish=n;CHECK(!run(io).passed);}
  for(unsigned n=1;n<=6;++n){Sim io;io.failBell=n;CHECK(!run(io).passed);}
  {Sim io;io.owned=false;CHECK(!run(io).passed&&io.writes==0);}
  {Sim io;io.claimed=true;CHECK(!run(io).passed&&io.writes==0);}
  {Sim io;io.backClock=true;CHECK(!run(io).passed);}
  for(unsigned byte=0;byte<80;++byte){Sim io;io.initBytes[byte]^=1;CHECK(!run(io).passed&&io.writes==0);}
  {Sim io;io.init.initDone=false;CHECK(!run(io).passed&&io.writes==0);}
  {Sim io;io.init.records[1].offset=0;CHECK(!run(io).passed&&io.writes==0);}
  {Sim io;put(io.q,0x41020,1);CHECK(!run(io).passed&&io.writes==0);}
  {Sim io;put(io.q,0x1020,4);CHECK(!run(io).passed&&io.writes==0);}
  {Sim io;auto r=run(io);CHECK(r.passed);const unsigned writes=io.writes;CHECK(!run(io).passed&&io.writes==writes);}
  std::printf("RM transport: %u scenarios / %u checks passed; simulated hardware only.\n",scenarios,checks);
}
