#pragma once
#include "MacGSPBar1.hpp"
#include "GSPPageTablesRMProtocol.hpp"
#include "GSPPageTablesSnapshot.hpp"
class MacGSPPageTablesMapping {
  IOPCIDevice*pci;MacGSPContext&context;IOMemoryMap*memory=nullptr;
public:
  unsigned long long barBase=0;
  MacGSPPageTablesMapping(IOPCIDevice*p,MacGSPContext&c):pci(p),context(c){}
  ~MacGSPPageTablesMapping(){if(memory)memory->release();}
  bool map(){
    if(!pci||!context.owned()||context.command()!=0||memory)return false;
    auto*descriptor=pci->getDeviceMemoryWithRegister(0x14);
    if(!descriptor||descriptor->getLength()!=0x4000000||descriptor->getPhysicalAddress()!=context.system.bar1)return false;
    const unsigned low=pci->configRead32(0x14);
    barBase=(static_cast<unsigned long long>(pci->configRead32(0x18))<<32)|(low&0xfffffff0U);
    if((low&15)!=12||barBase!=descriptor->getPhysicalAddress()||barBase>=0x10000000000ULL||barBase%0x4000000)return false;
    memory=descriptor->createMappingInTask(kernel_task,0,kIOMapAnywhere|kIOMapInhibitCache|kIOMapUnique,GSPPageTables::Start,GSPPageTables::Bytes);
    return mapped();
  }
  bool mapped()const{return memory&&memory->getVirtualAddress()&&memory->getLength()==GSPPageTables::Bytes&&memory->getPhysicalAddress()==barBase+GSPPageTables::Start;}
  unsigned long long physical()const{return mapped()?memory->getPhysicalAddress():0;}
  bool read(unsigned off,unsigned&value){
    if(!context.owned()||context.command()!=6||!mapped()||(off&3)||off>GSPPageTables::Bytes-4)return false;
    __sync_synchronize();value=*reinterpret_cast<volatile const UInt32*>(memory->getVirtualAddress()+off);__sync_synchronize();return true;
  }
  bool write(unsigned off,unsigned value){
    if(!context.owned()||context.command()!=6||!mapped()||(off&3)||off>GSPPageTables::Bytes-4)return false;
    __sync_synchronize();*reinterpret_cast<volatile UInt32*>(memory->getVirtualAddress()+off)=value;__sync_synchronize();return true;
  }
};
class MacGSPPageTablesMemory {
  MacGSPContext&context;MacGSPPageTablesMapping&memory;GSPExecutionOwner::Owner&owner;const GSPComputePrep::Result&prep;
  const GSPBar1::Result&bar1;const unsigned long long generation;const bool&lease;bool&claimed;
  bool saved=false;unsigned originalWindow=0;
  bool allowed(unsigned off){return !(off&3)&&off>=GSPPageTables::Start&&off<=GSPPageTables::Start+GSPPageTables::Bytes-4;}
public:
  MacGSPPageTablesMemory(MacGSPContext&c,MacGSPPageTablesMapping&m,GSPExecutionOwner::Owner&o,const GSPComputePrep::Result&p,
    const GSPBar1::Result&b,unsigned long long g,const bool&l,bool&used):context(c),memory(m),owner(o),prep(p),bar1(b),generation(g),lease(l),claimed(used){}
  bool ready(){return bar1.passed&&lease&&memory.mapped()&&context.owned()&&context.command()==6&&prep.passed&&prep.completed==6&&
    owner.ledger().generation()==generation&&owner.ledger().requiresPin()&&owner.phase()==GSPExecutionOwner::Phase::BootReturned;}
  bool claim(){if(!ready()||claimed)return false;claimed=true;return true;}
  unsigned long long nowNs(){uint64_t abs=0,ns=0;clock_get_uptime(&abs);absolutetime_to_nanoseconds(abs,&ns);return ns;}
  bool read(unsigned off,unsigned&v){
    if(!ready()||!claimed)return false;
    if(off==GSPPageTables::Window){v=context.read(off);if(!saved){originalWindow=v;saved=true;}return true;}
    return allowed(off)&&memory.read(off-GSPPageTables::Start,v);
  }
  bool write(unsigned off,unsigned v){
    if(!ready()||!claimed)return false;
    if(off==GSPPageTables::Window)return saved&&(v==originalWindow||v==0)&&context.write(off,v);
    return allowed(off)&&memory.write(off-GSPPageTables::Start,v);
  }
};

class MacGSPPageTablesRM {
  MacGSPContext &context;MacGSPRuntimeDma &dma;GSPExecutionOwner::Owner &owner;
  const GSPPageTables::Result&tables;const unsigned long long generation;const bool &workspace;const GSPSequencer::Result &sequence;bool &claimed;
public:
  MacGSPPageTablesRM(MacGSPContext &c,MacGSPRuntimeDma &d,GSPExecutionOwner::Owner &o,
    unsigned long long g,const bool&w,const GSPSequencer::Result&s,const GSPPageTables::Result&t,bool&used)
    :context(c),dma(d),owner(o),tables(t),generation(g),workspace(w),sequence(s),claimed(used){}
  bool ready(){return tables.passed&&!tables.windowRestored&&context.owned()&&context.command()==6&&dma.ready()&&dma.directlyMapped()&&workspace&&
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
    else if(off==0x1010){if(bytes!=4||value!=9)return false;}
    else if(off!=0xa000||bytes!=4096)return false;
    return dma.copyIn(GSPDmaProtocol::Queues,off,data,bytes);
  }
  bool doorbell(){return ready()&&claimed&&context.write(GSPComputePrep::Doorbell,0);}
};
