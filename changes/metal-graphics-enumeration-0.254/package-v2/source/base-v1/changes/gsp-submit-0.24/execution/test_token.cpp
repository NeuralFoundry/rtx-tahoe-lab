#include "WorkSubmitToken.hpp"
#include "reference/dev_ctrl.h"
#include <vector>
#include <fstream>
#include <cstdio>
#include <cstdlib>
namespace T=WorkSubmitToken;using Bytes=std::vector<unsigned char>;static unsigned long long checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::abort();}}while(0)
#define FIELD(v,f) (((v)>>(0?f))&((1ULL<<((1?f)-(0?f)+1))-1))
static void seal(Bytes &p){T::R::put32(p.data()+32,0);unsigned sum=0;for(unsigned i=0;i<((48+T::R::get32(p.data()+56)+7)&~7U);i+=4)sum^=T::R::get32(p.data()+i);T::R::put32(p.data()+32,sum);}
int main(int argc,char **argv){
 CHECK(argc==2);std::ifstream f(argv[1],std::ios::binary);CHECK(bool(f));Bytes raw(std::istreambuf_iterator<char>(f),{});CHECK(raw.size()==4096);
 T::Runlist run;CHECK(T::fifo(raw.data(),4096,10,run));CHECK(run.valid&&run.sequence==10&&run.entry==0&&run.id==0&&run.pbdmas==2);
 CHECK(run.pbdma[0]==0&&run.pbdma[1]==1&&run.fault[0]==32&&run.fault[1]==33);
 unsigned token=0;
 for(unsigned id=0;id<128;++id)for(unsigned chid=0;chid<4096;++chid){
   auto synthetic=run;synthetic.id=id;CHECK(T::compose(synthetic,chid,chid,token));
   CHECK(FIELD(token,NV_CTRL_VF_DOORBELL_VECTOR)==chid);CHECK(FIELD(token,NV_CTRL_VF_DOORBELL_RUNLIST_ID)==id);
   CHECK((token&~0x7f0fffU)==0);
 }
 for(unsigned id:{128U,~0U}){auto bad=run;bad.id=id;CHECK(!T::compose(bad,37,37,token)&&!token);}
 for(unsigned chid:{4096U,~0U})CHECK(!T::compose(run,chid,chid,token)&&!token);
 CHECK(!T::compose(run,37,38,token)&&!token);CHECK(!T::compose(run,37,0x10025,token));
 {auto bad=run;bad.valid=false;CHECK(!T::compose(bad,37,37,token));}
 CHECK(!T::fifo(raw.data(),4095,10,run));CHECK(!T::fifo(raw.data(),4096,11,run));
 for(unsigned off:{36U,40U,44U,48U,52U,56U,60U,64U,68U,72U,76U,80U,84U,88U,92U,96U,100U}){
   auto bad=raw;bad[off]^=1;seal(bad);CHECK(!T::fifo(bad.data(),4096,10,run)&&!run.valid);
 }
 // FIFO parameter/header and selected GR routing failures; unrelated engine
 // padding is intentionally not interpreted as a hardware capability.
 const unsigned offsets[]={104,108,108,112,116+8,116+100+8,116+12,116+80,116+80,116+64,116+68,116+72,116+76};
 const unsigned values[]={1,0,33,1,9,1,128,0,3,32,0,256,32};
 for(unsigned i=0;i<sizeof(offsets)/sizeof(*offsets);++i){auto bad=raw;T::R::put32(bad.data()+offsets[i],values[i]);seal(bad);CHECK(!T::fifo(bad.data(),4096,10,run)&&!run.valid);}
 CHECK(T::fifo(raw.data(),4096,10,run));CHECK(T::compose(run,37,37,token)&&token==37);
 std::printf("{\"passed\":true,\"checks\":%llu,\"historical_gr_runlist\":0,\"pbdma\":[0,1],\"fault\":[32,33],\"token_field_combinations\":524288,\"hardware_accessed\":false,\"doorbell_ready\":false}\n",checks);
}
