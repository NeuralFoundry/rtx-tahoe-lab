#include "../../gsp-submit-0.24/fence/RuntimeSimulation.hpp"
#include "../../gsp-submit-0.24/fence/HostFence.hpp"
#include "../../gsp-submit-0.24/fence/reference/dev_vm_tu102.h"
#include <iostream>
namespace H=HostFence;
static unsigned fenceScenarios=0;
struct FenceSim {
  Bytes memory=Bytes(0x4000);bool owned=true,claimed=false,notified=false,mappings=true;
  unsigned reads=0,writes=0,bells=0,polls=0,failRead=0,failWrite=0,loseAt=0,corruptAt=0,mode=0;
  unsigned long long time=100000;std::vector<unsigned> writesAt;
  FenceSim(){++fenceScenarios;}
  bool ready(){return owned;}
  unsigned long long nowNs(){if(mode!=9&&mode!=14)time+=1000;return time;}
  void delayUs(unsigned us){CHECK(us==100);if(mode!=9&&mode!=14)time+=100000000;}
  bool verifyMappings(){return mappings;}
  bool claim(){if(claimed||!owned)return false;claimed=true;return true;}
  void set(unsigned address,unsigned value){R::put32(memory.data()+address-H::Ring,value);}
  bool readMemory(unsigned address,unsigned char *out,unsigned bytes){
    CHECK(claimed&&address>=H::Ring&&address+bytes<=H::Ring+memory.size()&&bytes<=64&&bytes%4==0);++reads;
    if(notified&&address==H::Get){
      ++polls;
      if(mode!=1&&mode!=14&&polls>=3){set(H::Get,1);set(H::Fence,SubmitCodec::FenceValue);}
      if(mode==2)set(H::Get,2);if(mode==3)set(H::Put,0);if(mode==4)set(H::Fence,0xbadf0000);
      if(mode==5&&polls>=3)set(H::Fence+12,1);
      if(mode==6&&polls>=4)set(H::Fence,0);
      if(mode==11&&polls<3)set(H::Get,1);
      if(mode==12&&polls<3)set(H::Fence,SubmitCodec::FenceValue);
    }
    if(reads==failRead)return false;
    std::memcpy(out,memory.data()+address-H::Ring,bytes);if(reads==corruptAt)out[0]^=1;
    if(reads==loseAt)owned=false;if(mode==7&&reads==2)time=H::BudgetNs;if(mode==8&&reads==2)time=0;
    return true;
  }
  bool writeMemory(unsigned address,const unsigned char *data,unsigned bytes){
    CHECK(claimed&&!notified);++writes;CHECK(writes<=3);
    CHECK((writes==1&&address==H::Command&&bytes==20)||(writes==2&&address==H::Ring&&bytes==8)||(writes==3&&address==H::Put&&bytes==4));
    CHECK(address!=H::Fence);writesAt.push_back(address);std::memcpy(memory.data()+address-H::Ring,data,bytes);
    if(mode==10&&writes==2)set(H::Fence,SubmitCodec::FenceValue);
    return writes!=failWrite;
  }
  bool notify(unsigned token){CHECK(claimed&&!notified&&writes==3&&R::get32(memory.data()+H::Put-H::Ring)==1&&token==4);notified=true;++bells;return mode!=13;}
};
static H::Result fenceRun(FenceSim &io,const C::Plan &golden,const TX::Result &execution,const Bytes &requests,const Bytes &records){
  Bytes scratch(4096);H::Result r;const bool ok=H::execute(io,golden,execution,requests.data(),records.data(),scratch.data(),r);
  CHECK(ok==r.passed&&io.bells<=1&&io.writes<=3&&r.operations<=H::MaxOperations+1);
  if(ok)CHECK(!r.failure&&r.claimed&&r.putAttempted&&r.bellAttempted&&r.initialGet==0&&r.initialPut==0&&r.initialFence==0&&r.lastGet==1&&r.lastPut==1&&r.lastFence==SubmitCodec::FenceValue);
  return r;
}
