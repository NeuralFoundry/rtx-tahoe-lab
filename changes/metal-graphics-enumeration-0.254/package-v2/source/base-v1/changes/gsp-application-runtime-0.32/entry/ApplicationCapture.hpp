#pragma once
#include "../submit/ApplicationSubmit.hpp"
namespace ApplicationCapture {
namespace C=ApplicationMemory;namespace L=GMMULeaves;namespace H=HostFence;
constexpr unsigned RootBytes=12288,MaxChildren=unsigned(L::MaxChildBytes),DeviceBytes=9*4096;
constexpr unsigned Addresses[9]={H::Ring,H::Command,H::Fence,C::Base,C::Base+4096,C::Base+8192,C::Base+12288,C::Base+16384,C::Base+20480};
constexpr unsigned long long BudgetNs=5000000000ULL;
enum Failure:unsigned {None,Arguments,Owner,Clock,Timeout,Read,Unreadable};
struct Result {
 bool attempted=false,passed=false;Failure failure=None;
 unsigned rootBytes=0,childBytes=0,deviceBytes=0,requested=0,reads=0,lastAddress=0,lastValue=0;
 unsigned long long elapsed=0;
};
struct Storage {unsigned char *root=nullptr,*children=nullptr,*device=nullptr;Result result;};
// Revalidate the captured queue and image together before advancing the Session.
// This reads only immutable CPU captures, never devices or caller addresses.
inline bool verifyFull(const Result &r,const unsigned char *root,const unsigned char *children,const unsigned char *device,
 const ApplicationMemory::Storage &expected,const HostFence::Result &host,unsigned completed){
 if(!r.attempted||!r.passed||r.failure!=None||!root||!children||!device||!expected.root||!expected.children||!expected.image||
  completed>4||r.rootBytes!=RootBytes||r.childBytes!=expected.liveBytes||r.requested!=r.childBytes||r.deviceBytes!=DeviceBytes||
  r.childBytes<8192||r.childBytes>MaxChildren||r.childBytes%4096||r.reads!=12+r.childBytes/4096||
  !ExecutionMemory::equal(root,expected.root,RootBytes)||!ExecutionMemory::equal(children,expected.children,r.childBytes))return false;
 struct Reader {
  const unsigned char *device;
  bool ready(){return true;}
  unsigned long long nowNs(){return 0;}
  bool readMemory(unsigned a,unsigned char *out,unsigned n){
   const unsigned char *in=nullptr;
   if(a>=H::Ring&&a-H::Ring<4096&&n<=4096-(a-H::Ring))in=device+a-H::Ring;
   else if(a>=H::Command&&a-H::Command<4096&&n<=4096-(a-H::Command))in=device+4096+a-H::Command;
   else if(a>=H::Fence&&a-H::Fence<4096&&n<=4096-(a-H::Fence))in=device+8192+a-H::Fence;
   else if(a>=C::Base&&a-C::Base<C::Bytes&&n<=C::Bytes-(a-C::Base))in=device+12288+a-C::Base;
   if(!in||!out||!n)return false;for(unsigned i=0;i<n;++i)out[i]=in[i];return true;
  }
 } reader{device};
 ApplicationSubmit::Result proof;
 if(GSPComputePrep::get32(device+H::Get-H::Ring)!=completed+1||GSPComputePrep::get32(device+H::Put-H::Ring)!=completed+1)return false;
 return ApplicationSubmit::queueGuards(reader,proof,host,completed)&&ApplicationSubmit::backing(reader,proof,expected,completed);
}
template<class IO>bool capture(IO &io,unsigned requested,unsigned char *root,unsigned char *children,unsigned char *device,Result &r){
 const void *outputs[]={root,children,device};const size_t sizes[]={RootBytes,MaxChildren,DeviceBytes};
 for(unsigned i=0;i<3;++i)if(outputs[i]&&!ExecutionPlan::disjoint(outputs[i],sizes[i],&r,sizeof(r)))return false;
 r={};
 if(requested<8192||requested>MaxChildren||requested%4096){r.failure=Arguments;return false;}
 for(unsigned i=0;i<3;++i){if(!outputs[i]){r.failure=Arguments;return false;}
  for(unsigned j=0;j<i;++j)if(!ExecutionPlan::disjoint(outputs[i],sizes[i],outputs[j],sizes[j])){r.failure=Arguments;return false;}
 }
 if(!io.ready()){r.failure=Owner;return false;}r.attempted=true;r.requested=requested;
 const auto start=io.nowNs();
 auto ready=[&](){const auto now=io.nowNs();
  if(now<start||now-start<r.elapsed){r.failure=Clock;return false;}r.elapsed=now-start;
  if(r.elapsed>=BudgetNs){r.failure=Timeout;return false;}
  if(!io.ready()){r.failure=Owner;return false;}return true;
 };
 for(unsigned part=0;part<3;++part){
  const unsigned total=part==0?RootBytes:part==1?requested:DeviceBytes;
  unsigned char *out=part==0?root:part==1?children:device;
  for(unsigned off=0;off<total;off+=4096){
   if(!ready())return false;
   const unsigned address=part==0?unsigned(L::OldBase)+off:part==1?unsigned(L::NewBase)+off:Addresses[off/4096];
   r.lastAddress=address;++r.reads;
   if(!io.readMemory(address,out+off,4096)){r.failure=Read;return false;}
   if(part==0)r.rootBytes+=4096;else if(part==1)r.childBytes+=4096;else r.deviceBytes+=4096;
   // Shader instructions/guards are arbitrary bytes. Sentinel filtering applies
   // to the page tables only; actual device bytes are retained for the client.
   if(part<2)for(unsigned i=0;i<4096;i+=4){r.lastValue=GSPComputePrep::get32(out+off+i);
    if(GMMUInvalidate::unreadable(r.lastValue)){r.lastAddress=address+i;r.failure=Unreadable;return false;}
   }
  }
 }
 if(!ready())return false;
 r.passed=true;return true;
}
// Capture after a failed submission is still useful. This combines the two
// already-owned readers and does not depend on the submission success flag.
template<class ComputeIO,class ExecutionIO>class Reader {
 ComputeIO &compute;ExecutionIO &execution;
public:
 Reader(ComputeIO &c,ExecutionIO &e):compute(c),execution(e){}
 bool ready(){return compute.ready()&&execution.ready();}
 unsigned long long nowNs(){return compute.nowNs();}
 bool readMemory(unsigned address,unsigned char *out,unsigned bytes){
  if(address>=H::Ring&&address<H::Ring+0x4000&&bytes<=H::Ring+0x4000-address)return execution.readMemory(address,out,bytes);
  return compute.readMemory(address,out,bytes);
 }
};
}
