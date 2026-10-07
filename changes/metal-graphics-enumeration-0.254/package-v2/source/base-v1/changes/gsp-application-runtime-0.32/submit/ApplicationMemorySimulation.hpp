#include "../../gsp-compute-0.25/memory/HostSimulation.hpp"
#include "../memory/ApplicationMemory.hpp"
namespace CM=ApplicationMemory;
static unsigned computeScenarios=0;
struct ComputeSim {
 Bytes root,tables,backing=Bytes(CM::Bytes,0x55),rootScratch=Bytes(12288),expected=Bytes(L::MaxChildBytes),children=Bytes(L::MaxChildBytes),image=Bytes(CM::Bytes),command=Bytes(32),scratch=Bytes(4096);
 RtxApplication032::Request requests[4]; CM::Storage storage;CM::Result result;
 bool owned=true,claimed=false,sticky=false;unsigned ops=0,reads=0,writes=0,memoryWrites=0,registerWrites=0;
 unsigned failAt=0,loseAt=0,corruptRead=0,unreadableReg=0,clocks=0,clockFault=0,timeoutAt=0,pending=0,pdb=0,upper=0,window=0;
 unsigned long long time=100;std::vector<unsigned> writeAddresses;
 ComputeSim(RuntimeSim &runtime):root(runtime.vRoot),tables(runtime.vTables){
  ++computeScenarios;
  for(unsigned j=0;j<4;++j){requests[j].generation=7;requests[j].requestId=j+1;}
  storage={requests,runtime.root.data(),runtime.children.data(),runtime.fullImage.data(),runtime.storage.goldenBytes,runtime.contexts.childBytes,
   rootScratch.data(),expected.data(),children.data(),image.data(),command.data(),scratch.data()};
 }
 bool ready(){return owned;}
 unsigned long long nowNs(){++clocks;if(clocks==clockFault)return 0;time+=100;if(clocks==timeoutAt)time+=M::BudgetNs;return time;}
 bool claim(){if(!owned||claimed)return false;claimed=true;return true;}
 bool operation(){++ops;if(ops==loseAt)owned=false;return ops!=failAt;}
 unsigned char *at(unsigned a,unsigned n){
  CHECK(n&&n<=4096&&!(a&3)&&!(n&3));
  if(a>=L::OldBase&&a-L::OldBase+n<=root.size())return root.data()+a-unsigned(L::OldBase);
  if(a>=L::NewBase&&a-L::NewBase+n<=storage.liveBytes)return tables.data()+a-unsigned(L::NewBase);
  if(a>=CM::Base&&a-CM::Base+n<=backing.size())return backing.data()+a-CM::Base;
  CHECK(false);return nullptr;
 }
 bool readMemory(unsigned a,unsigned char *out,unsigned n){
  CHECK(claimed);++reads;if(!operation())return false;std::memcpy(out,at(a,n),n);if(reads==corruptRead)out[0]^=1;return true;
 }
 bool writeMemory(unsigned a,const unsigned char *data,unsigned n){
  CHECK(claimed);++writes;const unsigned phase=memoryWrites++;CHECK(phase<18);
  if(phase<6){CHECK(a==CM::Base+phase*4096&&n==4096);CHECK(std::memcmp(data,image.data()+phase*4096,n)==0);}
  else{
   const unsigned off=CM::PteOffset+(phase-6)/2*8+(phase%2==0?4:0);
   CHECK(a==L::NewBase+off&&n==4);CHECK(std::memcmp(backing.data(),image.data(),CM::Bytes)==0);
   CHECK(std::memcmp(data,children.data()+off,n)==0);
   if(phase%2)CHECK(R::get32(at(a+4,4))==R::get32(children.data()+off+4));
  }
  // First sum is zero, its poison is all ones; completion is initially zero.
  CHECK(R::get32(image.data()+16384)==0xffffffffU&&R::get32(image.data()+20480)==0);
  writeAddresses.push_back(a);const bool ok=operation();std::memcpy(at(a,n),data,n);return ok;
 }
 bool readRegister(unsigned a,unsigned &value){
  ++reads;if(!operation())return false;
  if(a==M::Window)value=window;
  else if(a==GMMUInvalidate::PDBRegister)value=pdb;
  else if(a==GMMUInvalidate::UpperRegister)value=upper;
  else {CHECK(a==GMMUInvalidate::CommandRegister);value=pending?0x80000041:0x41;if(pending&&!sticky)--pending;}
  if(reads==unreadableReg)value=0xbadf1100;return true;
 }
 bool writeRegister(unsigned a,unsigned value){
  ++writes;CHECK(memoryWrites==18);const unsigned phase=registerWrites++;CHECK(phase<3);
  const unsigned addresses[]={GMMUInvalidate::PDBRegister,GMMUInvalidate::UpperRegister,GMMUInvalidate::CommandRegister};
  const unsigned values[]={GMMUInvalidate::PDBValue,0,GMMUInvalidate::CommandValue};CHECK(a==addresses[phase]&&value==values[phase]);
  const bool ok=operation();if(phase==0)pdb=value;else if(phase==1)upper=value;else pending=2;return ok;
 }
 bool run(const C::Plan &golden,const TX::Result &execution,const H::Result &host){
  const bool pass=CM::execute(*this,golden,execution,host,storage,result);
  CHECK(pass==result.memory.passed);CHECK(memoryWrites<=18&&registerWrites<=3&&result.memory.linksPublished<=6);
  if(pass)CHECK(CM::ready(result,storage.liveBytes)&&memoryWrites==18&&registerWrites==3);
  return pass;
 }
};
