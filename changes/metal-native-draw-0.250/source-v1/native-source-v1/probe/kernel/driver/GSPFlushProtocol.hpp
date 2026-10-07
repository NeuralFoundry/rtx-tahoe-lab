#pragma once
#include "GSPDmaProtocol.hpp"

// Private sysmem-flush page. No extension of the nine public upload buffers.
// NVIDIA 570.144 GA100 flush address contract; this driver uses 40-bit DMA.
namespace GSPFlush075 {
using U32=GSPDmaProtocol::U32;using U64=GSPDmaProtocol::U64;
constexpr U32 Page=4096,Low=0x100c10,High=0x100c40,HighField=0x00ffffff;
constexpr U64 Limit=1ULL<<40,Magic=0x525458464c555331ULL;
struct MemoryInfo {
 U64 address=0,physical=0,segmentBytes=0,physicalBytes=0;
 U32 segments=0,operation=0,error=0,completeError=0,clearError=0,memoryCompleteError=0;
 bool started=false,allocated=false,dmaAllocated=false,zeroed=false,memoryPrepared=false;
 bool descriptorAttached=false,dmaPrepared=false,synchronized=false,ready=false,exposed=false;
 bool cleanupAttempted=false,cleanupSucceeded=false,retained=false;
};
inline bool validAddress(U64 address,U64 bytes){return address&&!(address&(Page-1))&&bytes==Page&&address<=Limit-Page;}
inline bool valid(const MemoryInfo&m){return m.ready&&m.allocated&&m.dmaAllocated&&m.zeroed&&m.memoryPrepared&&
 m.descriptorAttached&&m.dmaPrepared&&m.synchronized&&!m.cleanupAttempted&&m.segments==1&&
 validAddress(m.address,m.segmentBytes)&&m.physical==m.address&&m.physicalBytes>=Page;}
class Memory {
 MemoryInfo m_;
 bool error(U32 op,U32 e){m_.operation=op;m_.error=e?e:1;m_.ready=false;return false;}
public:
 const MemoryInfo&info()const{return m_;}
 void expose(){m_.exposed=m_.retained=true;}
 template<class B> bool prepare(B&b){
  if(m_.started||m_.cleanupAttempted)return false;
  m_.started=true;U32 e=0;
  if((e=b.mapperReady()))return error(1,e);
  if((e=b.allocate()))return error(2,e);m_.allocated=true;
  if((e=b.zero()))return error(3,e);m_.zeroed=true;
  if((e=b.createDma()))return error(4,e);m_.dmaAllocated=true;
  if((e=b.prepareMemory()))return error(5,e);m_.memoryPrepared=true;
  if((e=b.attach()))return error(6,e);m_.descriptorAttached=true;
  if((e=b.prepareDma()))return error(7,e);m_.dmaPrepared=true;
  if((e=b.segment(m_.address,m_.segmentBytes,m_.segments)))return error(8,e);
  if(m_.segments!=1||!validAddress(m_.address,m_.segmentBytes))return error(9,1);
  if((e=b.physical(m_.physical,m_.physicalBytes)))return error(10,e);
  if(m_.physical!=m_.address||m_.physicalBytes<Page)return error(11,1);
  if((e=b.synchronize()))return error(12,e);m_.synchronized=true;
  if((e=b.verifyZero()))return error(13,e);
  m_.ready=true;m_.operation=14;return true;
 }
 template<class B> bool cleanup(B&b){
  if(m_.exposed){m_.retained=true;return false;}
  if(m_.cleanupAttempted)return m_.cleanupSucceeded;
  m_.cleanupAttempted=true;m_.ready=false;
  if(m_.dmaPrepared){m_.completeError=b.completeDma();if(m_.completeError){m_.retained=true;return false;}m_.dmaPrepared=false;}
  // Clear even when setMemoryDescriptor/prepare did not complete successfully.
  if(m_.dmaAllocated){m_.clearError=b.clear();if(m_.clearError){m_.retained=true;return false;}m_.descriptorAttached=false;}
  if(m_.memoryPrepared){m_.memoryCompleteError=b.completeMemory();if(m_.memoryCompleteError){m_.retained=true;return false;}m_.memoryPrepared=false;}
  if(m_.dmaAllocated){b.releaseDma();m_.dmaAllocated=false;}
  if(m_.allocated){b.releaseMemory();m_.allocated=false;}
  m_.cleanupSucceeded=true;m_.retained=false;return true;
 }
};
enum Failure:U32{None,Ownership,Descriptor,MemoryEnable,OriginalRead,HighWrite,LowWrite,Readback,Restore};
struct Result {
 U64 generation=0,address=0;
 U32 phase=0,failure=None,beforeLow=~0U,beforeHigh=~0U,plannedLow=0,plannedHigh=0,afterLow=~0U,afterHigh=~0U;
 U32 commandBefore=~0U,commandEnabled=~0U,commandAfter=~0U,reads=0,writes=0;
 bool begun=false,exposed=false,memoryAttempted=false,highAttempted=false,lowAttempted=false,verified=false,restored=false,passed=false;
};
// IO is bound to the sealed generation, FwsecPreparing phase and exclusive
// provider. BME stays OFF throughout this transaction; only MSE is toggled.
template<class IO> bool program(IO&io,Memory&memory,Result&r,U64 generation){
 if(r.begun)return false;r.begun=true;r.generation=generation;r.phase=1;
 const auto &m=memory.info();r.address=m.address;
 auto fail=[&](Failure e){if(!r.failure)r.failure=e;return false;};
 if(!generation||!io.ready()){fail(Ownership);return false;}
 r.commandBefore=io.command();if(r.commandBefore!=0){fail(Ownership);return false;}
 if(!valid(m)||m.exposed){fail(Descriptor);return false;}
 r.plannedLow=U32(m.address>>8);
 // The shared owner has already latched exposure. Retain private DMA too
 // before enabling memory or issuing even a potentially partial write.
 memory.expose();r.exposed=true;r.phase=2;
 if(!io.retain()){fail(Ownership);return false;}
 r.memoryAttempted=true;io.enableMemory();r.commandEnabled=io.command();
 bool good=io.ready()&&r.commandEnabled==2;
 if(!good)fail(MemoryEnable);
 if(good){
  r.phase=3;r.beforeLow=io.read(Low);++r.reads;
  if(!io.ready()||io.command()!=2||r.beforeLow==~0U)good=fail(OriginalRead);
 }
 if(good){
  r.beforeHigh=io.read(High);++r.reads;
  if(!io.ready()||io.command()!=2||r.beforeHigh==~0U)good=fail(OriginalRead);
 }
 if(good){
  r.plannedHigh=(r.beforeHigh&~HighField)|U32((m.address>>40)&0x7f);
  r.phase=4;r.highAttempted=true;++r.writes;
  if(!io.ready()||io.command()!=2||!io.write(High,r.plannedHigh))good=fail(HighWrite);
 }
 if(good){
  r.phase=5;r.lowAttempted=true;++r.writes;
  if(!io.ready()||io.command()!=2||!io.write(Low,r.plannedLow))good=fail(LowWrite);
 }
 if(good){
  r.phase=6;r.afterHigh=io.read(High);++r.reads;
  if(!io.ready()||io.command()!=2||r.afterHigh!=r.plannedHigh)good=fail(Readback);
 }
 if(good){
  r.afterLow=io.read(Low);++r.reads;
  r.verified=io.ready()&&io.command()==2&&r.afterLow==r.plannedLow;
  if(!r.verified)good=fail(Readback);
 }
 // A failed/uncertain write never restores the old DMA address or frees the
 // page. Restore only our MSE enable while exclusive ownership still holds.
 if(io.ready()&&io.command()==2)io.disableMemory();
 r.commandAfter=io.command();r.restored=io.ready()&&r.commandAfter==0;
 if(!r.restored)good=fail(Restore);
 r.passed=good&&r.verified&&r.restored;r.phase=r.passed?7:8;return r.passed;
}
inline void snapshot(const MemoryInfo&m,const Result&r,U64 generation,U64*out){
 for(unsigned i=0;i<64;++i)out[i]=0;
 const U64 values[]={Magic,1,generation,r.phase,r.failure,m.address,m.physical,Page,m.segmentBytes,m.physicalBytes,m.segments,
  m.started,m.allocated,m.dmaAllocated,m.zeroed,m.memoryPrepared,m.descriptorAttached,m.dmaPrepared,m.synchronized,m.ready,
  m.exposed,m.retained,m.cleanupAttempted,m.cleanupSucceeded,m.operation,m.error,m.completeError,m.clearError,m.memoryCompleteError,
  r.begun,r.generation,r.exposed,r.memoryAttempted,r.highAttempted,r.lowAttempted,r.verified,r.restored,r.passed,
  r.beforeLow,r.beforeHigh,r.plannedLow,r.plannedHigh,r.afterLow,r.afterHigh,r.commandBefore,r.commandEnabled,r.commandAfter,r.reads,r.writes,
  Low,High,Limit,Page,8,0x7f};
 static_assert(sizeof(values)/sizeof(values[0])<=64,"flush evidence ABI");
 for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);++i)out[i]=values[i];
}
}
