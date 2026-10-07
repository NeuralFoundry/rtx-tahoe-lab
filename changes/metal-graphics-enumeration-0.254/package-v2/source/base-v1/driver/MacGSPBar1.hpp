#pragma once
#include "MacGSPComputePrep.hpp"
#include "GSPBar1Protocol.hpp"
class MacGSPBar1Mapping {
  IOPCIDevice*pci;MacGSPContext&context;IOMemoryMap*memory=nullptr;
public:
  unsigned long long barBase=0;
  MacGSPBar1Mapping(IOPCIDevice*p,MacGSPContext&c):pci(p),context(c){}
  ~MacGSPBar1Mapping(){if(memory)memory->release();}
  bool map(){
    if(!pci||!context.owned()||context.command()!=0||memory)return false;
    auto*descriptor=pci->getDeviceMemoryWithRegister(0x14);
    if(!descriptor||descriptor->getLength()!=0x4000000||descriptor->getPhysicalAddress()!=context.system.bar1)return false;
    const unsigned low=pci->configRead32(0x14);
    barBase=(static_cast<unsigned long long>(pci->configRead32(0x18))<<32)|(low&0xfffffff0U);
    if((low&15)!=12||barBase!=descriptor->getPhysicalAddress()||barBase>=0x10000000000ULL||barBase%0x4000000)return false;
    memory=descriptor->createMappingInTask(kernel_task,0,kIOMapAnywhere|kIOMapInhibitCache|kIOMapUnique,GSPBar1::Start,GSPBar1::Bytes);
    return mapped();
  }
  bool mapped()const{return memory&&memory->getVirtualAddress()&&memory->getLength()==GSPBar1::Bytes&&memory->getPhysicalAddress()==barBase+GSPBar1::Start;}
  unsigned long long physical()const{return mapped()?memory->getPhysicalAddress():0;}
  bool read(unsigned off,unsigned&value){
    if(!context.owned()||context.command()!=6||!mapped()||(off&3)||off>GSPBar1::Bytes-4)return false;
    __sync_synchronize();value=*reinterpret_cast<volatile const UInt32*>(memory->getVirtualAddress()+off);__sync_synchronize();return true;
  }
  bool write(unsigned off,unsigned value){
    if(!context.owned()||context.command()!=6||!mapped()||(off&3)||off>GSPBar1::Bytes-4)return false;
    __sync_synchronize();*reinterpret_cast<volatile UInt32*>(memory->getVirtualAddress()+off)=value;__sync_synchronize();return true;
  }
};
class MacGSPBar1 {
  MacGSPContext&context;MacGSPBar1Mapping&memory;GSPExecutionOwner::Owner&owner;const GSPComputePrep::Result&prep;
  const unsigned long long generation;const bool&lease;bool&claimed;
  bool saved=false;unsigned originalWindow=0;
  bool allowed(unsigned off){return !(off&3)&&off>=GSPBar1::Aperture&&off<=GSPBar1::Aperture+GSPBar1::Bytes-4;}
public:
  MacGSPBar1(MacGSPContext&c,MacGSPBar1Mapping&m,GSPExecutionOwner::Owner&o,const GSPComputePrep::Result&p,
    unsigned long long g,const bool&l,bool&used):context(c),memory(m),owner(o),prep(p),generation(g),lease(l),claimed(used){}
  bool ready(){return lease&&memory.mapped()&&context.owned()&&context.command()==6&&prep.passed&&prep.completed==6&&
    owner.ledger().generation()==generation&&owner.ledger().requiresPin()&&owner.phase()==GSPExecutionOwner::Phase::BootReturned;}
  bool claim(){if(!ready()||claimed)return false;claimed=true;return true;}
  unsigned long long nowNs(){uint64_t abs=0,ns=0;clock_get_uptime(&abs);absolutetime_to_nanoseconds(abs,&ns);return ns;}
  bool read(unsigned off,unsigned&v){
    if(!ready()||!claimed)return false;
    if(off==GSPBar1::Window){v=context.read(off);if(!saved){originalWindow=v;saved=true;}return true;}
    return allowed(off)&&memory.read(off-GSPBar1::Aperture,v);
  }
  bool write(unsigned off,unsigned v){
    if(!ready()||!claimed)return false;
    if(off==GSPBar1::Window)return saved&&(v==originalWindow||v==0)&&context.write(off,v);
    return allowed(off)&&memory.write(off-GSPBar1::Aperture,v);
  }
};
