#pragma once
#include "MacGSPContext.hpp"
#include "MacGSPRuntimeDma.hpp"
#include "GSPExecutionOwner.hpp"
#include "GSPFirstBootProtocol.hpp"
#include "FWSECExecution.hpp"

class MacGSPFirstBoot {
  MacGSPContext &context;
  MacGSPRuntimeDma &memory;
  GSPExecutionOwner::Owner &owner;
  const SEC2FirstBootStage::Result &stage;
  const FWSECExecution::Result &fwsec;
  const unsigned long long generation;
public:
  MacGSPFirstBoot(MacGSPContext &ctx,MacGSPRuntimeDma &dma,GSPExecutionOwner::Owner &o,
                 const SEC2FirstBootStage::Result &s,const FWSECExecution::Result &f,unsigned long long gen)
    :context(ctx),memory(dma),owner(o),stage(s),fwsec(f),generation(gen){}
  bool held() const{return context.owned() && owner.ledger().owned() && owner.ledger().generation()==generation;}
  unsigned command() const{return context.command();}
  void delayUs(unsigned us){context.delayUs(us);}
  bool launchGate() const {
    return held() && owner.phase()==GSPExecutionOwner::Phase::BootSetup && owner.startMask()==1 &&
      memory.ready() && fwsec.passed && fwsec.boot.passed && fwsec.stage.lifecycle.cleanupVerified &&
      stage.staged && stage.imemCompleted==137 && stage.dmemCompleted==98 && stage.dmemMatched==6272;
  }
  bool currentPointers(unsigned long long &meta,unsigned long long &args){
    if(!held() || !memory.ready() || !memory.pages())return false;
    const auto *pages=memory.pages();
    meta=GSPContentSeal::pageAddress(pages,GSPDmaProtocol::Metadata);
    args=GSPContentSeal::pageAddress(pages,GSPDmaProtocol::LibosArgs);
    return GSPLaunchOwnership::validateDirectPointer(pages,GSPDmaProtocol::TotalPages,GSPDmaProtocol::Metadata,0,meta,4096) &&
      GSPLaunchOwnership::validateDirectPointer(pages,GSPDmaProtocol::TotalPages,GSPDmaProtocol::LibosArgs,0,args,4096);
  }
  bool frtsMatches(){return held() && context.read(0x1fa824)==fwsec.boot.wprLo && context.read(0x1fa828)==fwsec.boot.wprHi;}
  bool noteStart(){return owner.startSec2(generation);}
  unsigned read(GSPFirstBoot::Reg reg){
    if(unsigned(reg)>=GSPFirstBoot::Count || reg==GSPFirstBoot::SecCpuAlias)return 0xffffffffU;
    return context.read(GSPFirstBoot::Offsets[reg]);
  }
  bool write(GSPFirstBoot::Reg reg,unsigned value){
    using namespace GSPFirstBoot;
    if(!held() || command()!=6 || unsigned(reg)>=Count)return false;
    const auto phase=owner.phase();
    if(phase!=GSPExecutionOwner::Phase::BootSetup && phase!=GSPExecutionOwner::Phase::Sec2Started)return false;
    unsigned long long meta=0,args=0;
    switch(reg){
      case GspEngine: if(phase!=GSPExecutionOwner::Phase::BootSetup || value>1)return false;break;
      case GspBcr: if(value!=0x111)return false;break;
      case SecRm: if(value!=0xb76000a1U)return false;break;
      case SecBromPara: if(value!=0x10)return false;break;
      case SecBromEngine: case SecBromAlgorithm: if(value!=1)return false;break;
      case SecBromUcode: if(value!=3)return false;break;
      case SecBootVector: if(value!=0x100)return false;break;
      case GspMailbox0: case GspMailbox1: case SecMailbox0: case SecMailbox1:
        if(!currentPointers(meta,args))return false;
        if(value!=(reg==GspMailbox0?unsigned(args):reg==GspMailbox1?unsigned(args>>32):
           reg==SecMailbox0?unsigned(meta):unsigned(meta>>32)))return false;
        break;
      case SecCpu: case SecCpuAlias:
        if(phase!=GSPExecutionOwner::Phase::Sec2Started || value!=2)return false;break;
      default:return false;
    }
    return context.write(Offsets[reg],value);
  }
};
