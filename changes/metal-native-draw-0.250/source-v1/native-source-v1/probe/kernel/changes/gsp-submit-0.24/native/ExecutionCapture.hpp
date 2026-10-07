#pragma once
#include "../fence/HostFence.hpp"
namespace ExecutionCapture {
constexpr unsigned Bytes=12288,Addresses[3]={HostFence::Ring,HostFence::Command,HostFence::Fence};
struct Result {bool attempted=false,passed=false;unsigned failure=0,bytes=0,reads=0,lastAddress=0;unsigned long long elapsed=0;};
template<class IO>bool capture(IO &io,unsigned char *out,Result &r){
  if(!out||!ExecutionPlan::disjoint(out,Bytes,&r,sizeof(r)))return false;r={};r.attempted=true;
  const auto start=io.nowNs();
  for(unsigned i=0;i<3;++i){
    const auto now=io.nowNs();if(now<start||now-start>=HostFence::BudgetNs){r.failure=1;return false;}r.elapsed=now-start;
    if(!io.ready()){r.failure=2;return false;}r.lastAddress=Addresses[i];++r.reads;
    if(!io.readMemory(Addresses[i],out+i*4096,4096)){r.failure=3;return false;}r.bytes+=4096;
  }
  const auto end=io.nowNs();if(end<start||end-start>=HostFence::BudgetNs){r.failure=1;return false;}r.elapsed=end-start;
  if(!io.ready()){r.failure=2;return false;}r.passed=true;return true;
}
}
