#pragma once
#include "MacGSPComputePrep.hpp"
#include "GSPVramProtocol.hpp"
class MacGSPVram {
  MacGSPContext&context;GSPExecutionOwner::Owner&owner;const GSPComputePrep::Result&prep;
  const unsigned long long generation;const bool&lease;bool&claimed;
  bool saved=false;unsigned originalWindow=0;
  bool allowed(unsigned off){return !(off&3)&&((off>=0x7ff000&&off<=0x7ffffc)||(off>=0x700000&&off<=0x700ffc));}
public:
  MacGSPVram(MacGSPContext&c,GSPExecutionOwner::Owner&o,const GSPComputePrep::Result&p,
    unsigned long long g,const bool&l,bool&used):context(c),owner(o),prep(p),generation(g),lease(l),claimed(used){}
  bool ready(){return lease&&context.owned()&&context.command()==6&&prep.passed&&prep.completed==6&&
    owner.ledger().generation()==generation&&owner.ledger().requiresPin()&&owner.phase()==GSPExecutionOwner::Phase::BootReturned;}
  bool claim(){if(!ready()||claimed)return false;claimed=true;return true;}
  unsigned long long nowNs(){uint64_t abs=0,ns=0;clock_get_uptime(&abs);absolutetime_to_nanoseconds(abs,&ns);return ns;}
  bool read(unsigned off,unsigned&v){
    if(!ready()||!claimed||(off!=GSPVram::Window&&!allowed(off)))return false;
    v=context.read(off);
    if(off==GSPVram::Window&&!saved){originalWindow=v;saved=true;}
    return true;
  }
  bool write(unsigned off,unsigned v){
    if(!ready()||!claimed)return false;
    if(off==GSPVram::Window){
      if(!saved||(v!=originalWindow&&v!=GSPVram::windowValue(0)&&v!=GSPVram::windowValue(GSPVram::Words-1)))return false;
    }else if(!allowed(off))return false;
    return context.write(off,v);
  }
};
