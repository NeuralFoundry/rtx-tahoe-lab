#include "ChannelTransactions.hpp"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
namespace T=ChannelTransactions;namespace C=ChannelCodec;namespace R=GSPComputePrep;namespace P=GSPPageTables;
using Bytes=std::vector<unsigned char>;
static unsigned long long scenarios=0,checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"scenario%llu line%d: %s\n",scenarios,__LINE__,#x);std::abort();}}while(0)
static void put(Bytes &b,unsigned off,unsigned value){CHECK(off+4<=b.size());R::put32(b.data()+off,value);}
static void checksum(Bytes &b){put(b,32,0);unsigned sum=0,end=(48+R::get32(b.data()+56)+7)&~7U;CHECK(end<=b.size());for(unsigned i=0;i<end;i+=4)sum^=R::get32(b.data()+i);put(b,32,sum);}
static Bytes load(const std::string &path){std::ifstream f(path,std::ios::binary);CHECK(bool(f));return Bytes(std::istreambuf_iterator<char>(f),{});}
static void save(const std::string &path,const Bytes &bytes){std::ofstream f(path,std::ios::binary);CHECK(bool(f));f.write(reinterpret_cast<const char *>(bytes.data()),std::streamsize(bytes.size()));CHECK(bool(f));}
struct Sim {
  Bytes q=Bytes(0x81000),prepBytes=Bytes(6*4096),pdBytes=Bytes(4096),gr;
  R::Result prep,pd;C::Plan expected;
  unsigned reads=0,writes=0,imports=0,publishes=0,bells=0,clocks=0,responseReads=0,prepareCalls=0;
  unsigned failRead=0,failWrite=0,failImport=0,failPublish=0,failBell=0,mode=0,targetStep=0,extra=0,rxSequence=13;
  bool owned=true,claimed=false,ring=true;R::U64 ns=100;
  Sim(const Bytes &packet,unsigned start=14):gr(packet.begin()+104,packet.begin()+104+1664){
    CHECK(packet.size()==4096);CHECK(C::plan(gr.data(),unsigned(gr.size()),expected));
    unsigned h[]={0,0x40000,4096,63,9,1,32,4096};for(unsigned i=0;i<8;++i)put(q,0x1000+i*4,h[i]);
    h[4]=start;for(unsigned i=0;i<8;++i)put(q,0x41000+i*4,h[i]);put(q,0x1020,start);put(q,0x41020,9);
    const auto prepEnd=(start+62)%63;
    prep.passed=true;prep.completed=prep.sent=prep.count=prep.pages=6;prep.bytes=6*4096;prep.txWriter=prep.txReader=8;
    prep.rxReader=prep.rxProducer=prepEnd;prep.initialReader=(prepEnd+57)%63;prep.initialSequence=6;prep.rxSequence=12;
    for(unsigned step=0;step<6;++step){
      Bytes p(4096);CHECK(R::request(step,p.data()));put(p,36,step+6);put(p,64,0);put(p,68,0);
      if(step==3){P::put64(p.data()+120,(1ULL<<49)-0x4000000);P::put64(p.data()+152,0x4000000);}
      checksum(p);std::memcpy(prepBytes.data()+step*4096,p.data(),4096);
      prep.records[step]={step*4096,4096,R::function(step),0,step+6,R::headerBytes(step)+R::paramSize(step),step,(prep.initialReader+step)%63,0};
    }
    CHECK(P::request(pdBytes.data(),4096));put(pdBytes,36,12);put(pdBytes,64,0);put(pdBytes,68,0);checksum(pdBytes);
    pd.passed=true;pd.completed=pd.sent=pd.count=pd.pages=1;pd.bytes=4096;pd.txWriter=pd.txReader=9;
    pd.initialReader=prepEnd;pd.initialSequence=12;pd.rxReader=pd.rxProducer=start;pd.rxSequence=13;
    pd.records[0]={0,4096,76,0,12,208,0,prepEnd,0};
  }
  bool ready(){return owned;}bool ringReady(){return ring;}
  bool claim(){if(claimed)return false;claimed=true;return true;}
  R::U64 nowNs(){++clocks;if(mode==20&&clocks>8)return 0;ns+=100;return ns;}
  void delayUs(unsigned us){CHECK(us==100);ns+=100000000;}
  bool import(){++imports;if(mode==12&&responseReads)put(q,0x41010,R::get32(q.data()+0x1020));return imports!=failImport;}
  bool publish(){++publishes;return publishes!=failPublish;}
  bool read(unsigned off,unsigned char *p,unsigned n){
    CHECK(n<=4096&&off+n<=q.size());++reads;if(reads==failRead)return false;std::memcpy(p,q.data()+off,n);
    if(off>=0x42000&&n==4096){++responseReads;if(mode==11&&responseReads%2==0)p[128]^=1;}
    if(mode==18&&reads==8)owned=false;return true;
  }
  bool write(unsigned off,const unsigned char *p,unsigned n){
    CHECK((off==0x1020||off==0x1010)?n==4:(off>=0xb000&&off<=0xf000&&off%4096==0&&n==4096));
    ++writes;std::memcpy(q.data()+off,p,n);return writes!=failWrite;
  }
  void append(Bytes &p){const unsigned w=R::get32(q.data()+0x41010);CHECK(p.size()==4096&&w<63);std::memcpy(q.data()+0x42000+w*4096,p.data(),4096);put(q,0x41010,(w+1)%63);}
  bool doorbell(){
    ++bells;if(bells==failBell)return false;CHECK(bells<=5);const unsigned step=bells-1;
    CHECK(R::get32(q.data()+0x1010)==10+step);
    Bytes canonical(4096);CHECK(C::request(step,expected,canonical.data(),4096));CHECK(std::memcmp(q.data()+0xb000+step*4096,canonical.data(),4096)==0);
    if(mode==9&&step==targetStep)return true;
    for(unsigned i=0;i<extra;++i){Bytes p(4096);put(p,36,rxSequence++);put(p,40,1);put(p,48,0x03000000);put(p,52,0x43505256);put(p,56,40);put(p,60,0x100c);checksum(p);append(p);}
    Bytes p=canonical;put(p,36,rxSequence++);put(p,64,0);put(p,68,0);
    if(step==0){put(p,112+132,37);put(p,112+136,1);}
    if(step==1)std::memcpy(p.data()+104,gr.data(),1664);
    if(step==targetStep){
      if(mode==1)put(p,64,0x56);if(mode==2)put(p,C::function(step)==76?92:96,0x56);
      if(mode==3)put(p,80,0xbad);if(mode==4)put(p,C::function(step)==76?96:100,1);
      if(mode==5)put(p,60,0x1003);if(mode==6)put(p,36,99);
      if(mode==7)put(p,C::function(step)==76?100:104,1);if(mode==13)put(p,40,2);
      if(mode==14)put(p,68,1);if(mode==15)put(p,72,1);if(mode==16)put(p,84,0xbad);
      if(mode==17&&step==0)put(p,112+144,0xbad);if(mode==19)put(p,88,0xbad);
      if(mode==24&&step==0)put(p,112+136,2);if(mode==25&&step==2)put(p,104+48,0xbad);
    }
    checksum(p);if(mode==8&&step==targetStep)p[80]^=1;append(p);
    if(mode!=10||step!=targetStep)put(q,0x41020,10+step);return true;
  }
  bool prepareContext(const C::Plan &p){
    ++prepareCalls;CHECK(prepareCalls==1&&bells==2&&R::get32(q.data()+0x41020)==11&&R::get32(q.data()+0x41010)==R::get32(q.data()+0x1020));
    CHECK(C::planValid(p)&&p.backingBytes==expected.backingBytes&&p.physicalEnd==expected.physicalEnd);
    if(mode==21)return false;if(mode==22){ns+=R::BudgetNs;return true;}
    GMMULeaves::Range ranges[10];CHECK(C::mappingRanges(p,ranges));
    Bytes old(12288),child(GMMULeaves::MaxChildBytes);GMMULeaves::write64(old.data(),0x100322);GMMULeaves::write64(old.data()+4096,0x100422);
    GMMULeaves::Result mapped;return GMMULeaves::build(old.data(),old.size(),ranges,10,child.data(),child.size(),mapped);
  }
  bool contextReady(const C::Plan &p){return mode!=23&&C::planValid(p);}
};
static T::Result run(Sim &io,Bytes *fixtures=nullptr){
  ++scenarios;Bytes out(R::MaxBytes),requests(5*4096),scratch(4096);T::Result r;
  T::execute(io,io.prep,io.prepBytes.data(),io.pd,io.pdBytes.data(),out.data(),requests.data(),scratch.data(),r);
  CHECK(r.rpc.sent<=5&&r.rpc.doorbells<=5&&r.rpc.completed<=5&&r.rpc.count<=R::MaxRecords&&r.rpc.pages<=R::MaxPages&&r.rpc.ticks<=R::MaxTicks+1);
  if(r.rpc.passed){CHECK(r.rpc.completed==5&&r.rpc.sent==5&&r.rpc.txWriter==14&&r.rpc.txReader==14&&r.contextPrepared&&r.contextPreparationAttempted&&r.channelId==37&&r.subdeviceMask==1);}
  if(!r.contextPrepared)CHECK(r.rpc.sent<=2);
  if(fixtures)*fixtures=requests;return r;
}
int main(int argc,char **argv){
  CHECK(argc==3);const auto packet=load(argv[1]);CHECK(packet.size()==4096);C::Plan plan;
  GSPInitEvents::Record grRecord;CHECK(GSPInitEvents::decode(packet.data(),4096,11,grRecord));R::Result original;original.step=5;CHECK(R::reply(packet.data(),grRecord,original));
  CHECK(C::plan(packet.data()+104,1664,plan));CHECK(plan.backingBytes==13254656&&plan.physicalEnd==33095680);
  Sim baseline(packet);Bytes requests;CHECK(run(baseline,&requests).rpc.passed);save(std::string(argv[2])+"/channel-requests-native.bin",requests);
  {Sim real(packet);const std::string path=argv[1];const auto parent=path.substr(0,path.find_last_of("/\\")+1);
    for(unsigned i=0;i<6;++i){const auto p=load(parent+"record-"+(i+6<10?"00":"0")+std::to_string(i+6)+".bin");CHECK(p.size()==4096);std::memcpy(real.prepBytes.data()+i*4096,p.data(),4096);}
    CHECK(T::prefix(real.prep,real.prepBytes.data(),real.pd,real.pdBytes.data()));CHECK(run(real).rpc.passed);
  }
  for(unsigned start:{0U,1U,59U,60U,61U,62U}){Sim io(packet,start);CHECK(run(io).rpc.passed);}
  {Sim io(packet);io.extra=2;CHECK(run(io).rpc.passed);}
  {Sim io(packet);io.extra=3;CHECK(!run(io).rpc.passed);}
  for(unsigned step=0;step<5;++step)for(unsigned mode:{1U,2U,3U,4U,5U,6U,7U,8U,9U,10U,13U,14U,15U,16U,19U}){Sim io(packet);io.targetStep=step;io.mode=mode;CHECK(!run(io).rpc.passed);}
  for(unsigned mode:{11U,12U,17U,18U,20U,21U,22U,23U,24U,25U}){Sim io(packet);io.mode=mode;if(mode==25)io.targetStep=2;CHECK(!run(io).rpc.passed);}
  for(unsigned i=1;i<=baseline.reads;++i){Sim io(packet);io.failRead=i;CHECK(!run(io).rpc.passed);}
  for(unsigned i=1;i<=baseline.writes;++i){Sim io(packet);io.failWrite=i;CHECK(!run(io).rpc.passed);}
  for(unsigned i=1;i<=baseline.imports;++i){Sim io(packet);io.failImport=i;CHECK(!run(io).rpc.passed);}
  for(unsigned i=1;i<=baseline.publishes;++i){Sim io(packet);io.failPublish=i;CHECK(!run(io).rpc.passed);}
  for(unsigned i=1;i<=5;++i){Sim io(packet);io.failBell=i;CHECK(!run(io).rpc.passed);}
  for(unsigned kind:{0U,16U,17U,18U,19U,20U,23U,24U})for(unsigned fault=0;fault<6;++fault){
    Sim io(packet);const unsigned sizes[]={0,0xffffffff,4096,4096,4096,0xfffff000},aligns[]={4096,4096,0,3,0xffffffff,4096};
    put(io.gr,kind*8,sizes[fault]);put(io.gr,kind*8+4,aligns[fault]);CHECK(!run(io).rpc.passed&&io.prepareCalls==0&&io.bells==2);
  }
  {Sim changed(packet);put(changed.gr,0,R::get32(changed.gr.data())+0x20000);CHECK(C::plan(changed.gr.data(),1664,changed.expected));CHECK(changed.expected.backingBytes>plan.backingBytes);
    CHECK(run(changed,&requests).rpc.passed);save(std::string(argv[2])+"/channel-requests-changed-gr-native.bin",requests);}
  {Sim io(packet);io.ring=false;CHECK(!run(io).rpc.passed&&io.writes==0);}
  {Sim io(packet);io.owned=false;CHECK(!run(io).rpc.passed&&io.writes==0);}
  {Sim io(packet);io.claimed=true;CHECK(!run(io).rpc.passed&&io.writes==0);}
  {Sim io(packet);CHECK(run(io).rpc.passed);const auto before=io.writes;CHECK(!run(io).rpc.passed&&io.writes==before);}
  for(unsigned field=0;field<7;++field){Sim io(packet);
    if(field==0)io.pd.passed=false;if(field==1)io.pd.rxSequence=0;if(field==2)io.pd.records[0].offset=1;
    if(field==3)io.pdBytes[104]^=1;if(field==4)io.prepBytes[64]^=1;if(field==5)io.pd.txReader=8;if(field==6)io.pd.failure=R::Frame;
    CHECK(!run(io).rpc.passed&&io.writes==0);
  }
  std::printf("{\"passed\":true,\"scenarios\":%llu,\"checks\":%llu,\"hardware_accessed\":false,\"prefix_022_synthetic\":true,\"post_channel_gr_used\":true}\n",scenarios,checks);
}
