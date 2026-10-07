#pragma once
#include "../../gsp-channel-0.23/transactions/ChannelCodec.hpp"
// Pure decoders/bit packing for GA106/PF. No MMIO or authority to ring a doorbell.
namespace WorkSubmitToken {
namespace R=GSPComputePrep;
struct Runlist {bool valid=false;unsigned sequence=0,entry=0,id=0,pbdmas=0,pbdma[2]={0,0},fault[2]={0,0};};
inline bool fifo(const unsigned char *packet,unsigned bytes,unsigned sequence,Runlist &out,unsigned client=R::Client,unsigned subdevice=R::Subdevice){
  out={};GSPInitEvents::Record record;
  if(!GSPInitEvents::decode(packet,bytes,sequence,record)||record.function!=76||record.result||record.payloadBytes!=24+3212||
    R::get32(packet+68)||R::get32(packet+72)||R::get32(packet+80)!=client||R::get32(packet+84)!=subdevice||
    R::get32(packet+88)!=0x20801112||R::get32(packet+92)||R::get32(packet+96)!=3212||R::get32(packet+100))return false;
  const auto *p=packet+104;const unsigned count=R::get32(p+4);
  if(R::get32(p)||!count||count>32||p[8])return false;
  unsigned matches=0;Runlist candidate;
  for(unsigned i=0;i<count;++i){const auto *e=p+12+i*100;
    if(R::get32(e+8)!=1)continue;
    if(++matches!=1)return false;
    candidate.sequence=sequence;candidate.entry=i;candidate.id=R::get32(e+12);candidate.pbdmas=R::get32(e+80);
    if(candidate.id>=128||!candidate.pbdmas||candidate.pbdmas>2)return false;
    for(unsigned j=0;j<candidate.pbdmas;++j){candidate.pbdma[j]=R::get32(e+64+j*4);candidate.fault[j]=R::get32(e+72+j*4);
      if(candidate.pbdma[j]>=32||candidate.fault[j]>=256)return false;
    }
    if(candidate.pbdmas==2&&(candidate.pbdma[0]==candidate.pbdma[1]||candidate.fault[0]==candidate.fault[1]))return false;
  }
  if(matches!=1)return false;candidate.valid=true;out=candidate;return true;
}
inline bool compose(const Runlist &runlist,unsigned channelId,unsigned rawGspToken,unsigned &candidate){
  candidate=0;
  // GA106 CPU HAL: VECTOR11:0 and RUNLIST_ID22:16. GSP's raw channel token
  // must match the returned channelId; richer/unknown encodings stop here.
  if(!runlist.valid||runlist.id>=128||channelId>=4096||rawGspToken!=channelId)return false;
  candidate=(runlist.id<<16)|channelId;return true;
}
}
