#pragma once
#include "SubmissionSimulation.hpp"
#include "BatchQueueGate.hpp"
namespace BS=BatchSubmit;namespace V=RtxBatch031;namespace Gate=BatchQueueGate;
static unsigned batchScenarios=0,failedCases=0;
struct SubmitSim {
 Bytes root,tables,backing,queue;Gate::State gate;
 bool owned=true,physical=true;unsigned job=0,reads=0,writes=0,bells=0,polls=0,ops=0,clocks=0;
 unsigned mode=0,failAt=0,loseAt=0,clockFault=0,timeoutAt=0;unsigned long long time=1000;
 std::vector<unsigned> writesAt;
 SubmitSim(const ComputeSim &memory,const FenceSim &host):root(memory.root),tables(memory.tables),backing(memory.backing),queue(host.memory){}
 bool ready(){return owned&&Gate::ready(gate,job);}
 unsigned long long nowNs(){++clocks;if(clocks==clockFault)return 0;if(mode!=15)time+=1000;if(clocks==timeoutAt)time+=BS::BudgetNs;return time;}
 void delayUs(unsigned us){CHECK(us==100);if(mode!=15)time+=100000000;}
 bool claim(unsigned j){return j==job&&owned&&Gate::claim(gate,j);}
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
 // CPU-only model of external device writes. This is not hardware evidence.
 void deviceProgress(){
  ++polls;const unsigned out=QmdProfile::OutputPhysical+job*1024,fence=QmdProfile::FencePhysical+job*256;
  if(mode!=1&&mode!=15&&polls>=3){
   set(H::Get,job+2);for(unsigned i=0;i<V::DefaultCounts[job];++i)set(out+i*4,R::get32(backing.data()+V::cbOffset(job)+512+i*4)+R::get32(backing.data()+V::cbOffset(job)+768+i*4));
   set(fence,V::completion(job));
  }
  switch(mode){
   case 2:set(H::Get,job+3);break;case 3:set(H::Put,job+1);break;
   case 4:set(out,0xbadf1000);break;case 5:set(fence,0xbadf2000);break;
   case 6:if(polls>=3)backing[V::outOffset(job)+256]^=1;break;
   case 7:if(polls>=3)backing[V::fenceOffset(job)+19]^=1;break;
   case 8:if(polls>=3)backing[32]^=1;break;case 9:if(polls>=3)backing[8192+16]^=1;break;
   case 10:if(polls>=3)backing[13312]^=1;break;case 11:if(polls>=3)queue[0x2000]^=1;break;
   case 12:if(polls>=3)queue[8]^=1;break;case 13:if(polls>=4)set(out,1);break;
   case 16:if(polls>=3)queue[0x2000+48]^=1;break;case 17:set(H::Fence,0);break;
   case 18:if(polls>=3)backing[V::qmdOffset(job)]=1;break;
   case 19:if(polls<3)set(out,0U);break;
   case 20:if(polls<3)set(fence,V::completion(job));break;
   case 21:if(polls<3)set(H::Get,job+2);break;
   case 22:if(polls>=3)root[900]^=1;break;case 23:if(polls>=3)tables[600]^=1;break;
   case 24:if(polls>=3){const unsigned i=V::DefaultCounts[job]-1;set(out+i*4,~(R::get32(backing.data()+V::cbOffset(job)+512+i*4)+R::get32(backing.data()+V::cbOffset(job)+768+i*4)));}break;
   case 25:if(polls>=3)backing[V::outOffset(job)+V::DefaultCounts[job]*4]^=1;break;
   case 26:if(polls>=3)set(fence,V::completion((job+3)%4));break;
   case 27:if(polls>=3)backing[V::outOffset((job+1)%4)]^=1;break;
   case 28:if(polls>=3&&job<3)backing[V::qmdOffset(job+1)]^=1;break;
  }
 }
 bool readMemory(unsigned a,unsigned char *out,unsigned n){
  CHECK(gate.jobs[job].claimed);++reads;if(gate.jobs[job].notified&&a==H::Get)deviceProgress();if(!operation())return false;std::memcpy(out,at(a,n),n);return true;
 }
 bool writeMemory(unsigned a,const unsigned char *data,unsigned n){
  CHECK(Gate::write(gate,job,a,data,n));++writes;CHECK(writes<=3);
  CHECK(a!=QmdProfile::OutputPhysical&&a!=QmdProfile::FencePhysical&&a!=H::Fence);
  writesAt.push_back(a);const bool ok=operation();std::memcpy(at(a,n),data,n);return ok;
 }
 bool notify(unsigned token){CHECK(token==4&&Gate::notify(gate,job));++bells;return operation()&&mode!=14;}
 BS::Result run(unsigned j,const ComputeSim &memory,const TX::Result &execution,const H::Result &host){
  ++batchScenarios;job=j;reads=writes=bells=polls=ops=clocks=0;
  BS::Result r;const bool ok=BS::execute(*this,j,memory.storage,memory.result,execution,host,r);
  CHECK(ok==r.passed&&writes<=3&&bells<=1&&r.operations<=BS::MaxOperations);
  if(ok){CHECK(Gate::finish(gate,j,r));CHECK(gate.completed==j+1&&r.writes==3&&r.get==j+2&&r.put==j+2&&r.completion==V::completion(j));}
  else {++failedCases;CHECK(gate.completed==j);CHECK(!Gate::finish(gate,j,r));}
  return r;
 }
};
