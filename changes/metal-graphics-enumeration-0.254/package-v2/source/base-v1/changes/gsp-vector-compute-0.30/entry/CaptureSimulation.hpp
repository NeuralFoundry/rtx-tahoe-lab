#include "../submit/SubmissionSimulation.hpp"
#include "../submit/VectorSubmit.hpp"
namespace CS=VectorSubmit;
static unsigned submitScenarios=0;
struct SubmitSim {
 Bytes root,tables,backing,queue;
 bool owned=true,claimed=false,notified=false,physical=true;
 unsigned reads=0,writes=0,bells=0,polls=0,ops=0,clocks=0,mode=0,failAt=0,loseAt=0,clockFault=0,timeoutAt=0;
 unsigned long long time=1000;std::vector<unsigned> writesAt;
 SubmitSim(const ComputeSim &memory,const FenceSim &host):root(memory.root),tables(memory.tables),backing(memory.backing),queue(host.memory){++submitScenarios;}
 bool ready(){return owned;}
 unsigned long long nowNs(){++clocks;if(clocks==clockFault)return 0;if(mode!=15)time+=1000;if(clocks==timeoutAt)time+=CS::BudgetNs;return time;}
 void delayUs(unsigned us){CHECK(us==100);if(mode!=15)time+=100000000;}
 bool claim(){if(!owned||claimed)return false;claimed=true;return true;}
 bool physicalMode(){return physical;}
 bool operation(){++ops;if(ops==loseAt)owned=false;return ops!=failAt;}
 unsigned char *at(unsigned a,unsigned n){
  CHECK(n&&n<=256&&!(a&3)&&!(n&3));
  if(a>=L::OldBase&&a-L::OldBase+n<=root.size())return root.data()+a-unsigned(L::OldBase);
  if(a>=L::NewBase&&a-L::NewBase+n<=tables.size())return tables.data()+a-unsigned(L::NewBase);
  if(a>=CM::Base&&a-CM::Base+n<=backing.size())return backing.data()+a-CM::Base;
  if(a>=H::Ring&&a-H::Ring+n<=queue.size())return queue.data()+a-H::Ring;
  CHECK(false);return nullptr;
 }
 void set(unsigned a,unsigned v){R::put32(at(a,4),v);}
 // Emulates external device writes only in CPU tests. Never evidence of GPU execution.
 void deviceProgress(){
  ++polls;
  if(mode!=1&&mode!=15&&polls>=3){set(H::Get,2);for(unsigned i=0;i<61;++i)set(QmdProfile::OutputPhysical+i*4,R::get32(backing.data()+8192+0x200+i*4)+R::get32(backing.data()+8192+0x300+i*4));set(QmdProfile::FencePhysical,RtxVector030::Completion);}
  switch(mode){
   case 2:set(H::Get,3);break;case 3:set(H::Put,1);break;
   case 4:set(QmdProfile::OutputPhysical,0xbadf1000);break;case 5:set(QmdProfile::FencePhysical,0xbadf2000);break;
   case 6:if(polls>=3)backing[16384+17]^=1;break;case 7:if(polls>=3)backing[20480+19]^=1;break;
   case 8:if(polls>=3)backing[32]^=1;break;case 9:if(polls>=3)backing[8192+16]^=1;break;
   case 10:if(polls>=3)backing[12288+300]^=1;break;case 11:if(polls>=3)queue[0x2000]^=1;break;
   case 12:if(polls>=3)queue[8]^=1;break;case 13:if(polls>=4)set(QmdProfile::OutputPhysical,1);break;
   case 16:if(polls>=3)queue[0x2000+48]^=1;break;case 17:set(H::Fence,0);break;
   case 18:if(polls>=3)backing[12288]=1;break;
   case 19:if(polls<3)set(QmdProfile::OutputPhysical,0U);break;
   case 20:if(polls<3)set(QmdProfile::FencePhysical,RtxVector030::Completion);break;
   case 21:if(polls<3)set(H::Get,2);break;
   case 22:if(polls>=3)root[900]^=1;break;case 23:if(polls>=3)tables[600]^=1;break;
   case 24:if(polls>=3)set(QmdProfile::OutputPhysical+60*4,~(R::get32(backing.data()+8192+0x200+60*4)+R::get32(backing.data()+8192+0x300+60*4)));break;
   case 25:if(polls>=3)backing[16384+61*4]^=1;break;
   case 26:if(polls>=3)backing[16384+256]^=1;break;
  }
 }
 bool readMemory(unsigned a,unsigned char *out,unsigned n){
  CHECK(claimed);++reads;if(notified&&a==H::Get)deviceProgress();if(!operation())return false;std::memcpy(out,at(a,n),n);return true;
 }
 bool writeMemory(unsigned a,const unsigned char *data,unsigned n){
  CHECK(claimed&&!notified);++writes;CHECK(writes<=3);
  CHECK((writes==1&&a==CS::Command&&n==32)||(writes==2&&a==CS::Entry&&n==8)||(writes==3&&a==H::Put&&n==4));
  CHECK(a!=QmdProfile::OutputPhysical&&a!=QmdProfile::FencePhysical&&a!=H::Fence);
  writesAt.push_back(a);const bool ok=operation();std::memcpy(at(a,n),data,n);return ok;
 }
 bool notify(unsigned token){CHECK(claimed&&!notified&&writes==3&&token==4);notified=true;++bells;return operation()&&mode!=14;}
 CS::Result run(const ComputeSim &memory,const TX::Result &execution,const H::Result &host){
  CS::Result r;const bool ok=CS::execute(*this,memory.storage,memory.result,execution,host,r);
  CHECK(ok==r.passed&&writes<=3&&bells<=1&&r.operations<=CS::MaxOperations);
  if(ok)CHECK(!r.failure&&r.claimed&&r.putAttempted&&r.bellAttempted&&r.immutableVerified&&r.guardsVerified&&r.stable&&r.get==2&&r.put==2&&
    r.completedElements==61&&r.output[0]==0U&&r.initialOutput[0]==0xffffffffU&&r.completion==RtxVector030::Completion&&r.initialGet==1&&r.initialPut==1&&!r.initialCompletion);
  return r;
 }
};
