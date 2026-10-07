#pragma once
#include "ProgramCapture.hpp"
#include "../stage/MacProgramStage.hpp"

// Full diagnostic capture remains available after an uncertain stage or queue
// operation while the original owner and acquired physical window still hold.
class MacProgramCapture {
 MacChannelMemoryMapping &mapping;MacProgramMemoryState &memory;
 RtxProgramAccess033::State &state;ProgramCapture::Storage &output;MacProgramStage prior;
 bool buffers()const{
  const auto &m=memory.storage;
  const struct Span{const void *p;size_t n;} outs[]={{output.root,12288},{output.children,GMMULeaves::MaxChildBytes},{output.device,ProgramCapture::DeviceBytes}},
  ins[]={{&state,sizeof(state)},{&memory,sizeof(memory)},{&output,sizeof(output)},
   {m.root,12288},{m.children,GMMULeaves::MaxChildBytes},{m.image,ProgramMemory::Bytes},{m.library,512},{m.code,4096}};
  for(const auto &out:outs)for(const auto &in:ins)if(!RtxProgram033::separate(out.p,out.n,in.p,in.n))return false;
  return true;
 }
public:
 MacProgramCapture(MacGSPContext &c,MacChannelMemoryMapping &map,GSPExecutionOwner::Owner &o,MacExecutionMemoryState &b,
  MacProgramMemoryState &m,const ExecutionTransactions::Result &e,const HostFence::Result &h,RtxProgramAccess033::State &s,
  ProgramCapture::Storage &out,unsigned j):mapping(map),memory(m),state(s),output(out),prior(c,map,o,b,m,e,h,s,j){}
 bool ready(){return buffers()&&prior.ready();}
 uint64_t nowNs(){return prior.nowNs();}
 bool readMemory(unsigned address,uint8_t *out,unsigned bytes){
  if(!ready()||!out||!bytes||bytes>4096||(address&3)||(bytes&3)||!mapping.span(address,bytes))return false;
  const struct Span{unsigned base,bytes;} spans[]={{unsigned(GMMULeaves::OldBase),12288},{unsigned(GMMULeaves::NewBase),memory.storage.liveBytes},
   {ProgramMemory::Base,ProgramMemory::Bytes},{HostFence::Ring,4096},{HostFence::Command,4096},{HostFence::Fence,4096}};
  for(const auto &s:spans)if(address>=s.base&&address-s.base<s.bytes&&bytes<=s.bytes-(address-s.base))return mapping.read(address,out,bytes);
  return false;
 }
 bool capture(){return ready()&&ProgramCapture::capture(*this,memory.storage.liveBytes,output.root,output.children,output.device,output.result);}
};
