#pragma once
#include "../memory/ChannelMemory.hpp"
namespace ChannelSnapshot {
constexpr unsigned RootBytes=12288,MaxChildren=unsigned(GMMULeaves::MaxChildBytes);
constexpr unsigned long long BudgetNs=5000000000ULL;
struct Result {bool attempted=false,passed=false;unsigned failure=0,rootBytes=0,childBytes=0,requested=0,reads=0,lastAddress=0,lastValue=0;unsigned long long elapsed=0;};
template<class IO>void capture(IO &io,unsigned requested,unsigned char *root,unsigned char *children,Result &r){
  r={};
  if(requested<8192||requested>MaxChildren||requested%4096||!ChannelMemory::disjoint(root,RootBytes,children,requested)){r.failure=1;return;}
  if(!io.ready()){r.failure=2;return;}
  r.attempted=true;r.requested=requested;const auto start=io.nowNs();
  auto ready=[&](){const auto now=io.nowNs();
    if(now<start||now-start<r.elapsed){r.failure=3;return false;}r.elapsed=now-start;
    if(r.elapsed>=BudgetNs||!io.ready()){r.failure=4;return false;}return true;
  };
  for(unsigned part=0;part<2;++part){const unsigned total=part?requested:RootBytes,base=unsigned(part?GMMULeaves::NewBase:GMMULeaves::OldBase);auto *out=part?children:root;
    for(unsigned off=0;off<total;off+=4096){if(!ready())return;
      r.lastAddress=base+off;++r.reads;
      if(!io.readMemory(base+off,out+off,4096)){r.failure=5;return;}
      (part?r.childBytes:r.rootBytes)+=4096;
      for(unsigned i=0;i<4096;i+=4){const auto value=GSPComputePrep::get32(out+off+i);r.lastValue=value;
        if(GMMUInvalidate::unreadable(value)){r.lastAddress=base+off+i;r.failure=6;return;}
      }
    }
  }
  if(ready())r.passed=true;
}
}
