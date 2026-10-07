#pragma once
#include "../../../driver/GSPVirtualRegisters.hpp"
#include "../../../driver/MacGSPPageTables.hpp"
#include "ChannelMemory.hpp"
#include "../transactions/ChannelTransactions.hpp"

// Real macOS BAR1 adapter, not wired into a kext entry point yet. Mapping and all
// storage must be allocated before firmware exposure and retained with owner.
class MacChannelMemoryMapping {
  IOPCIDevice *pci;MacGSPContext &context;IOMemoryMap *memory=nullptr;
public:
  static constexpr unsigned Start=0x1002000,Bytes=0x4000000-Start;
  unsigned long long barBase=0;
  MacChannelMemoryMapping(IOPCIDevice *p,MacGSPContext &c):pci(p),context(c){}
  ~MacChannelMemoryMapping(){if(memory)memory->release();}
  bool map(){
    if(!pci||!context.owned()||context.command()!=0||memory)return false;
    auto *descriptor=pci->getDeviceMemoryWithRegister(0x14);
    if(!descriptor||descriptor->getLength()!=0x4000000||descriptor->getPhysicalAddress()!=context.system.bar1)return false;
    const unsigned low=pci->configRead32(0x14);
    barBase=(static_cast<unsigned long long>(pci->configRead32(0x18))<<32)|(low&0xfffffff0U);
    if((low&15)!=12||barBase!=descriptor->getPhysicalAddress()||barBase>=0x10000000000ULL||barBase%0x4000000)return false;
    memory=descriptor->createMappingInTask(kernel_task,0,kIOMapAnywhere|kIOMapInhibitCache|kIOMapUnique,Start,Bytes);
    return mapped();
  }
  bool mapped()const{return memory&&memory->getVirtualAddress()&&memory->getLength()==Bytes&&memory->getPhysicalAddress()==barBase+Start;}
  bool span(unsigned address,unsigned bytes)const{return bytes&&bytes<=4096&&!(address&3)&&!(bytes&3)&&address>=Start&&address<0x4000000&&bytes<=0x4000000-address;}
  bool read(unsigned address,unsigned char *out,unsigned bytes){
    if(!out||!context.owned()||context.command()!=6||!mapped()||!span(address,bytes))return false;
    auto *p=reinterpret_cast<volatile const UInt32 *>(memory->getVirtualAddress()+address-Start);
    __sync_synchronize();for(unsigned i=0;i<bytes;i+=4)GSPComputePrep::put32(out+i,p[i/4]);__sync_synchronize();return true;
  }
  bool write(unsigned address,const unsigned char *data,unsigned bytes){
    if(!data||!context.owned()||context.command()!=6||!mapped()||!span(address,bytes))return false;
    auto *p=reinterpret_cast<volatile UInt32 *>(memory->getVirtualAddress()+address-Start);
    __sync_synchronize();for(unsigned i=0;i<bytes;i+=4)p[i/4]=GSPComputePrep::get32(data+i);__sync_synchronize();return true;
  }
};

struct MacChannelMemoryState {
  bool leaseOwned=false,ringClaimed=false,contextsClaimed=false,windowObserved=false,physicalMode=false,queueClaimed=false,queueBellPending=false;
  unsigned originalWindow=0,expectedProducer=10;
  unsigned char *oldCapture=nullptr,*ringImage=nullptr,*children=nullptr,*scratch=nullptr;
  ChannelCodec::Plan plan;ChannelMemory::Result ring,contexts;
  unsigned long long excludedStagingNs=0;
};

class MacChannelMemory {
  MacGSPContext &context;MacChannelMemoryMapping &mapping;GSPExecutionOwner::Owner &owner;
  const GSPPageTables::Result &tables;const GSPPageTablesRM::Result &pd;const GSPPageTablesSnapshot::Result &post;
  const unsigned long long generation;
  bool within(unsigned address,unsigned bytes,unsigned base,unsigned length)const{return address>=base&&address<base+length&&bytes<=base+length-address;}
  bool contextBacking(unsigned address,unsigned bytes)const{
    if(!state.contextsClaimed||!ChannelCodec::planValid(state.plan))return false;
    for(const auto &b:state.plan.buffers)if(within(address,bytes,unsigned(b.physical),unsigned(b.allocated)))return true;return false;
  }
  bool equalBytes(const unsigned char *a,const unsigned char *b,unsigned bytes)const{for(unsigned i=0;i<bytes;++i)if(a[i]!=b[i])return false;return true;}
public:
  MacChannelMemoryState &state;
  MacChannelMemory(MacGSPContext &c,MacChannelMemoryMapping &m,GSPExecutionOwner::Owner &o,
    const GSPPageTables::Result &t,const GSPPageTablesRM::Result &r,const GSPPageTablesSnapshot::Result &p,unsigned long long g,MacChannelMemoryState &s)
    :context(c),mapping(m),owner(o),tables(t),pd(r),post(p),generation(g),state(s){}
  bool ready(){return state.leaseOwned&&state.oldCapture&&state.ringImage&&state.children&&state.scratch&&mapping.mapped()&&
    context.owned()&&context.command()==6&&tables.passed&&tables.windowRestored&&pd.passed&&pd.completed==1&&post.passed&&post.bytes==12288&&
    owner.ledger().generation()==generation&&owner.ledger().requiresPin()&&owner.phase()==GSPExecutionOwner::Phase::BootReturned;}
  unsigned long long nowNs(){uint64_t absolute=0,ns=0;clock_get_uptime(&absolute);absolutetime_to_nanoseconds(absolute,&ns);return ns;}
  bool claimRing(){if(!ready()||state.ringClaimed)return false;state.ringClaimed=true;return true;}
  bool claimContexts(const ChannelCodec::Plan &p){if(!ready()||!ringReady()||state.contextsClaimed||!ChannelCodec::planValid(p))return false;state.plan=p;state.contextsClaimed=true;return true;}
  bool readMemory(unsigned address,unsigned char *out,unsigned bytes){
    if(!ready()||!state.ringClaimed||!state.physicalMode||!mapping.span(address,bytes))return false;
    const bool allowed=within(address,bytes,0x1002000,12288)||within(address,bytes,0x1005000,unsigned(GMMULeaves::MaxChildBytes))||
      within(address,bytes,ChannelMemory::RingStart,ChannelMemory::RingBytes)||contextBacking(address,bytes);
    return allowed&&mapping.read(address,out,bytes);
  }
  bool writeMemory(unsigned address,const unsigned char *data,unsigned bytes){
    if(!ready()||!data||!state.ringClaimed||!state.physicalMode||!mapping.span(address,bytes))return false;
    bool zero=false;const unsigned char *expected=nullptr;unsigned char parent[8];
    if(!state.contextsClaimed){
      if(within(address,bytes,ChannelMemory::RingStart,ChannelMemory::RingBytes))zero=true;
      else if(within(address,bytes,0x1005000,8192))expected=state.ringImage+address-0x1005000;
      else if(bytes==4&&(address==GMMULeaves::OldBase+GMMULeaves::ParentOffset||address==GMMULeaves::OldBase+GMMULeaves::ParentOffset+4)){
        GMMULeaves::write64(parent,GMMULeaves::ParentValue);expected=parent+address-unsigned(GMMULeaves::OldBase+GMMULeaves::ParentOffset);
      }else return false;
    }else{
      if(contextBacking(address,bytes))zero=true;
      else if(within(address,bytes,0x1007000,state.contexts.childBytes>=8192?state.contexts.childBytes-8192:0))expected=state.children+address-0x1005000;
      else if(bytes==4&&address>=0x1005010&&address<0x1006000)expected=state.children+address-0x1005000;
      else return false;
    }
    if(zero){for(unsigned i=0;i<bytes;++i)if(data[i])return false;}
    else if(!expected||!equalBytes(data,expected,bytes))return false;
    return mapping.write(address,data,bytes);
  }
  bool readRegister(unsigned address,unsigned &value){
    if(!ready()||!state.ringClaimed)return false;
    if(address!=ChannelMemory::Window&&(!state.physicalMode||(address!=GMMUInvalidate::PDBRegister&&address!=GMMUInvalidate::UpperRegister&&address!=GMMUInvalidate::CommandRegister)))return false;
    if(!GSPVirtualRegisters::read(context,address,value))return false;
    if(address==ChannelMemory::Window){
      if(!state.windowObserved&&!GMMUInvalidate::unreadable(value)){state.originalWindow=value;state.windowObserved=true;}
      state.physicalMode=value==0;
    }return true;
  }
  bool writeRegister(unsigned address,unsigned value){
    if(!ready()||!state.ringClaimed)return false;
    if(address==ChannelMemory::Window){if(!state.windowObserved||(value!=0&&value!=state.originalWindow))return false;state.physicalMode=false;}
    else {
      const auto &stage=state.contextsClaimed?state.contexts:state.ring;
      if(!state.physicalMode||!stage.backingVerified||!stage.childrenVerified||!stage.parentAttempted||
        !((address==GMMUInvalidate::PDBRegister&&value==GMMUInvalidate::PDBValue)||(address==GMMUInvalidate::UpperRegister&&value==0)||
          (address==GMMUInvalidate::CommandRegister&&value==GMMUInvalidate::CommandValue)))return false;
    }
    return GSPVirtualRegisters::write(context,address,value);
  }
  bool ringReady(){return ready()&&state.ring.passed&&state.ring.invalidation.passed&&!state.ring.windowRestored&&state.physicalMode;}
  bool contextReady(const ChannelCodec::Plan &p){
    if(!ringReady()||!state.contexts.passed||!state.contexts.invalidation.passed||!ChannelCodec::planValid(p)||!ChannelCodec::planValid(state.plan))return false;
    for(unsigned i=0;i<9;++i){const auto &a=p.buffers[i],&b=state.plan.buffers[i];
      if(a.id!=b.id||a.kind!=b.kind||a.physical!=b.physical||a.virtualAddress!=b.virtualAddress||a.bytes!=b.bytes||a.allocated!=b.allocated||a.alignment!=b.alignment||a.usePhysical!=b.usePhysical||a.useVirtual!=b.useVirtual)return false;
    }return true;
  }
  bool stageRing(){ChannelMemory::ring(*this,state.oldCapture,state.ringImage,state.scratch,state.ring);return ringReady();}
  bool stageContexts(const ChannelCodec::Plan &p){ChannelMemory::contexts(*this,p,state.oldCapture,state.ringImage,state.children,state.scratch,state.ring,state.contexts);return contextReady(p);}
  void restoreWindow(){ChannelMemory::restoreWindow(*this,state.ring);}
};

class MacChannelTransactions {
  MacGSPContext &context;MacGSPRuntimeDma &dma;MacChannelMemory &memory;
public:
  MacChannelTransactions(MacGSPContext &c,MacGSPRuntimeDma &d,MacChannelMemory &m):context(c),dma(d),memory(m){}
  bool ready(){return memory.ringReady()&&dma.ready()&&dma.directlyMapped();}
  bool ringReady(){return ready();}
  bool claim(){if(!ready()||memory.state.queueClaimed)return false;memory.state.queueClaimed=true;return true;}
  // RM waits get15s; the separately measured memory stage gets90s. This clock
  // excludes only a successfully bounded synchronous context-staging interval.
  unsigned long long nowNs(){const auto actual=memory.nowNs();return actual>=memory.state.excludedStagingNs?actual-memory.state.excludedStagingNs:0;}
  void delayUs(unsigned us){context.delayUs(us);}
  bool prepareContext(const ChannelCodec::Plan &p){
    if(!ready()||!memory.state.queueClaimed||memory.state.expectedProducer!=12||memory.state.queueBellPending)return false;
    const auto start=memory.nowNs();const bool passed=memory.stageContexts(p);const auto end=memory.nowNs();
    if(!passed||end<start||end-start>=ChannelMemory::BudgetNs||memory.state.excludedStagingNs>~0ULL-(end-start))return false;
    memory.state.excludedStagingNs+=end-start;return true;
  }
  bool contextReady(const ChannelCodec::Plan &p){return ready()&&memory.contextReady(p);}
  bool import(){return ready()&&dma.synchronizeOne(GSPDmaProtocol::Queues,1);}
  bool publish(){return ready()&&memory.state.queueClaimed&&dma.synchronizeOne(GSPDmaProtocol::Queues,2);}
  bool read(unsigned off,unsigned char *out,unsigned bytes){return ready()&&out&&bytes&&bytes<=4096&&off<=0x81000&&bytes<=0x81000-off&&dma.copyOut(GSPDmaProtocol::Queues,off,out,bytes);}
  bool write(unsigned off,const unsigned char *data,unsigned bytes){
    auto &s=memory.state;if(!ready()||!s.queueClaimed||!data||s.expectedProducer<10||s.expectedProducer>15)return false;
    const unsigned value=GSPComputePrep::get32(data);
    if(off==0x1020)return bytes==4&&value<63&&dma.copyIn(GSPDmaProtocol::Queues,off,data,bytes);
    if(off==0x1010){if(bytes!=4||s.expectedProducer>14||value!=s.expectedProducer||s.queueBellPending)return false;
      if(!dma.copyIn(GSPDmaProtocol::Queues,off,data,bytes))return false;++s.expectedProducer;s.queueBellPending=true;return true;
    }
    if(s.expectedProducer>14||s.queueBellPending||off!=0x2000+(s.expectedProducer-1)*4096||bytes!=4096)return false;
    if(s.expectedProducer>=12&&!memory.contextReady(s.plan))return false;
    if(!ChannelCodec::request(s.expectedProducer-10,s.plan,s.scratch,4096))return false;
    for(unsigned i=0;i<4096;++i)if(data[i]!=s.scratch[i])return false;
    return dma.copyIn(GSPDmaProtocol::Queues,off,data,bytes);
  }
  bool doorbell(){if(!ready()||!memory.state.queueClaimed||!memory.state.queueBellPending)return false;
    memory.state.queueBellPending=false;return context.write(GSPComputePrep::Doorbell,0);
  }
};
