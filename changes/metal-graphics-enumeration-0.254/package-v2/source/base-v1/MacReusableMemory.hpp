#pragma once
#include "changes/gsp-program-library-0.33/memory/MacProgramMemory.hpp"
#include "ReusableMemory.hpp"

// Use the existing actual BAR1 adapter and consume-before-write latches. Only
// the CPU image builder changes, before any of its six pages/PTEs are written.
class MacReusableMemory035 : public MacProgramMemory {
 const ExecutionTransactions::Result &execution35;
 const HostFence::Result &host35;
public:
 MacReusableMemory035(MacGSPContext &c,MacChannelMemoryMapping &map,MacExecutionMemory &mem,const MacExecutionQueueState &q,
  const ExecutionTransactions::Result &e,const MacHostFenceState &f,const HostFence::Result &h,MacProgramMemoryState &s)
  :MacProgramMemory(c,map,mem,q,e,f,h,s),execution35(e),host35(h){}
 bool stage(const ChannelCodec::Plan &golden){
  return RtxReusableMemory035::stage(*this,golden,execution35,host35,state.storage,state.result)&&
    ProgramMemory::ready(state.result,state.storage.liveBytes);
 }
};
