#pragma once
#include "MacGSPSequencer.hpp"
#include "GSPComputePrepProtocol.hpp"
class MacGSPComputePrep {
  MacGSPContext &context;MacGSPRuntimeDma &dma;GSPExecutionOwner::Owner &owner;
  const unsigned long long generation;const bool &workspace;const GSPSequencer::Result &sequence;bool &claimed;
public:
  MacGSPComputePrep(MacGSPContext &c,MacGSPRuntimeDma &d,GSPExecutionOwner::Owner &o,
    unsigned long long g,const bool&w,const GSPSequencer::Result&s,bool&used)
    :context(c),dma(d),owner(o),generation(g),workspace(w),sequence(s),claimed(used){}
  bool ready(){return context.owned()&&context.command()==6&&dma.ready()&&dma.directlyMapped()&&workspace&&
    sequence.passed&&sequence.resumed&&owner.ledger().generation()==generation&&owner.ledger().requiresPin()&&
    owner.phase()==GSPExecutionOwner::Phase::BootReturned;}
  bool claim(){if(!ready()||claimed)return false;claimed=true;return true;}
  unsigned long long nowNs(){uint64_t abs=0,ns=0;clock_get_uptime(&abs);absolutetime_to_nanoseconds(abs,&ns);return ns;}
  void delayUs(unsigned us){context.delayUs(us);}
  bool import(){return ready()&&dma.synchronizeOne(GSPDmaProtocol::Queues,1);}
  bool publish(){return ready()&&claimed&&dma.synchronizeOne(GSPDmaProtocol::Queues,2);}
  bool read(unsigned off,unsigned char*out,unsigned bytes){
    return ready()&&out&&bytes&&bytes<=4096&&off<=0x81000&&bytes<=0x81000-off&&
      dma.copyOut(GSPDmaProtocol::Queues,off,out,bytes);
  }
  bool write(unsigned off,const unsigned char*data,unsigned bytes){
    if(!ready()||!claimed||!data)return false;
    const unsigned value=GSPContentSeal::get32(data);
    if(off==0x1020){if(bytes!=4||value>=63)return false;}
    else if(off==0x1010){if(bytes!=4||value<3||value>8)return false;}
    else if(off<0x4000||off>0x9000||off%4096||bytes!=4096)return false;
    return dma.copyIn(GSPDmaProtocol::Queues,off,data,bytes);
  }
  bool doorbell(){return ready()&&claimed&&context.write(GSPComputePrep::Doorbell,0);}
};
