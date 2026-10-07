#pragma once
#include "MacGSPIdentity.hpp"
#include "GSPContentSeal.hpp"
#include "FWSECDisplay.hpp"
#include <mach/vm_param.h>

// Coordinator-owned BAR0 mapping. No user-selected register access is exposed.
// Internal launch/sequencer protocols perform all offset/value validation.
class MacGSPContext {
  IOPCIDevice *pci;
  IOService *owner;
  IOMemoryMap *bar=nullptr;
public:
  GSPContentSeal::Facts system;
  Boot0::Facts identity;
  FWSECPreflight::Snapshot board;
  FWSECDisplay::Snapshot display;
  unsigned boot0=0,sec2Fuse=0,fwsecFuse=0,vramMiB=0;
  explicit MacGSPContext(IOPCIDevice *p,IOService *o):pci(p),owner(o){}
  bool owned() const{return pci && pci->isOpen(owner);}
  unsigned command() const{return pci->configRead16(4);}
  void setMemory(bool v){if(owned())pci->setMemoryEnable(v);}
  void setMaster(bool v){if(owned())pci->setBusMasterEnable(v);}
  void delayUs(unsigned us){if(us<=100)IODelay(us);}
  bool map(){
    if(!owned() || command()!=0 || bar)return false;
    IODeviceMemory *m=pci->getDeviceMemoryWithRegister(0x10);
    if(!m || m->getLength()!=0x1000000)return false;
    const auto base=pci->configRead32(0x10)&0xfffffff0U;
    if(m->getPhysicalAddress()!=base)return false;
    bar=m->createMappingInTask(kernel_task,0,kIOMapAnywhere|kIOMapInhibitCache|kIOMapUnique,0,0x1000000);
    return bar && bar->getVirtualAddress() && bar->getLength()==0x1000000 && bar->getPhysicalAddress()==base;
  }
  void unmap(){if(bar){bar->release();bar=nullptr;}}
  unsigned read(unsigned off) const{
    if(!owned() || !(command()&2) || !bar || (off&3) || off>0xfffffc)return 0xffffffffU;
    __sync_synchronize();
    const auto v=*reinterpret_cast<volatile const UInt32 *>(bar->getVirtualAddress()+off);
    __sync_synchronize();return v;
  }
  bool write(unsigned off,unsigned v){
    if(!owned() || command()!=6 || !bar || (off&3) || off>0xfffffc)return false;
    __sync_synchronize();*reinterpret_cast<volatile UInt32 *>(bar->getVirtualAddress()+off)=v;__sync_synchronize();return true;
  }
  unsigned readPreflight(unsigned i){return i<FWSECPreflight::Count?read(FWSECPreflight::Offsets[i]):0xffffffffU;}
  unsigned readDisplay(unsigned i){return i<FWSECDisplay::Count?read(FWSECDisplay::Offsets[i]):0xffffffffU;}
  bool capture(){
    if(!owned() || command()!=0 || !bar)return false;
    MacGSPIdentity checks(pci,owner);identity=checks.facts();
    if(Boot0::preflight(identity))return false;
    system.bar0=identity.bar0;system.bar1=identity.bar1;
    const unsigned lo=pci->configRead32(0x1c);
    if((lo&15)!=12)return false;
    system.bar3=(static_cast<unsigned long long>(pci->configRead32(0x20))<<32)|(lo&0xfffffff0U);
    IODeviceMemory *mem=pci->getDeviceMemoryWithRegister(0x1c);
    if(!mem || mem->getPhysicalAddress()!=system.bar3 || mem->getLength()!=0x2000000)return false;
    system.maxUserVa=static_cast<unsigned long long>(MACH_VM_MAX_ADDRESS);
    system.revision=pci->configRead8(8);
    UInt8 cap=0;
    if(!pci->findPCICapability(0x10,&cap) || cap<0x40 || cap>0xe8)return false;
    system.linkCap=pci->configRead32(cap+0xc);
    if(!system.valid() || system.linkCap==0xffffffffU)return false;
    setMemory(true);
    bool good=command()==2;
    if(good){
      boot0=read(0);sec2Fuse=read(0x824148);fwsecFuse=read(0x8241e0);vramMiB=read(0x1183a4);
      good=boot0==0xb76000a1U && boot0==read(0) && sec2Fuse==1 && sec2Fuse==read(0x824148) &&
        fwsecFuse==3 && fwsecFuse==read(0x8241e0) && vramMiB==6144 && vramMiB==read(0x1183a4);
    }
    if(good)good=!board.capture(*this,vramMiB) && !display.capture(*this) &&
      FWSECRegion::validBoardEvidence(board) && FWSECDisplay::validIdleEvidence(display);
    setMemory(false);
    return good && command()==0 && owned();
  }
};
