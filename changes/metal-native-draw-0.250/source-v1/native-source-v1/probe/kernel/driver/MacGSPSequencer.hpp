#pragma once
#include "MacGSPContext.hpp"
#include "MacGSPRuntimeDma.hpp"
#include "GSPExecutionOwner.hpp"
#include "GSPSequencerProtocol.hpp"
#include <kern/clock.h>

class MacGSPSequencer {
  MacGSPContext &context;MacGSPRuntimeDma &dma;GSPExecutionOwner::Owner &owner;
  const unsigned long long generation;const bool &workspace;
  bool &consumed;bool &gspStartNoted;bool &sec2StartNoted;
public:
  MacGSPSequencer(MacGSPContext &c,MacGSPRuntimeDma &m,GSPExecutionOwner::Owner &o,
    unsigned long long g,const bool &w,bool &used,bool &gs,bool &ss)
    :context(c),dma(m),owner(o),generation(g),workspace(w),consumed(used),gspStartNoted(gs),sec2StartNoted(ss){}
  bool ready(){return context.owned() && context.command()==6 && dma.ready() &&
    owner.ledger().generation()==generation && owner.ledger().requiresPin() &&
    owner.phase()==GSPExecutionOwner::Phase::BootReturned;}
  bool workspaceHeld(unsigned long long start,unsigned long long end){
    return workspace && ready() && start==GSPSequencer::WorkspaceStart && end==GSPSequencer::WorkspaceEnd;
  }
  bool claimExecution(){if(!ready()||consumed)return false;consumed=true;return true;}
  unsigned long long nowNs(){uint64_t absolute=0,ns=0;clock_get_uptime(&absolute);absolutetime_to_nanoseconds(absolute,&ns);return ns;}
  void delayUs(unsigned us){context.delayUs(us);}
  bool currentLibosArgs(unsigned long long &pointer){
    if(!ready() || !dma.directlyMapped() || !dma.pages())return false;
    pointer=GSPContentSeal::pageAddress(dma.pages(),GSPDmaProtocol::LibosArgs);
    return GSPLaunchOwnership::validateDirectPointer(dma.pages(),GSPDmaProtocol::TotalPages,
      GSPDmaProtocol::LibosArgs,0,pointer,4096);
  }
  bool noteStart(bool sec2){
    if(!ready() || !consumed || !workspace || (!sec2 && sec2StartNoted) || (sec2 && !gspStartNoted))return false;
    bool &noted=sec2?sec2StartNoted:gspStartNoted;
    if(noted)return false;noted=true;return true;
  }
  unsigned read(unsigned off){
    if(!ready())return 0xffffffffU;
    switch(off){
      case 0x110040:case 0x110044:case 0x110084:case 0x1100f4:case 0x110100:
      case 0x11010c:case 0x110118:case 0x1103c0:case 0x110624:case 0x111668:
      case 0x840100:case 0x840040:case 0x111388:case 0x1180f8:return context.read(off);
      default:return 0xffffffffU;
    }
  }
  bool write(unsigned off,unsigned value){
    if(!ready() || !consumed || !workspace)return false;
    switch(off){
      case 0x110100:case 0x110130:if(!gspStartNoted||sec2StartNoted||value!=2)return false;break;
      case 0x840100:case 0x840130:if(!sec2StartNoted||value!=2)return false;break;
      case 0x1103c0:if(value>1)return false;break;
      case 0x111668:if(value && value!=0x111)return false;break;
      case 0x110084:if(value!=0xb76000a1U)return false;break;
      case 0x110624:if(!(value&0x80U))return false;break;
      case 0x11010c:case 0x110128:if(value)return false;break;
      case 0x110600:if(value!=0x114)return false;break;
      case 0x110110:if(value!=0x173c400 && value!=0x173c441)return false;break;
      case 0x110114:case 0x11011c:if((value&255) || value>0x4000)return false;break;
      case 0x110118:if(value!=0x614 && value!=0x600)return false;break;
      case 0x111210:if(value!=0x1f10)return false;break;
      case 0x11119c:if(value!=0x400)return false;break;
      case 0x111198:case 0x111180:if(value!=1)return false;break;
      case 0x110104:if(value!=0x100)return false;break;
      case 0x110040:case 0x110044:{
        unsigned long long args=0;
        if(!currentLibosArgs(args))return false;
        if(off==0x110044){if(value!=unsigned(args>>32))return false;}
        else if(value && value!=0xfe && value!=unsigned(args))return false;
        break;
      }
      default:return false;
    }
    return context.write(off,value);
  }
};
