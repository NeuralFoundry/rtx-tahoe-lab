#pragma once
#include "ExecutionCodec.hpp"
// Offline journal verification only. It cannot attest memory publication,
// queue consumption, ownership, or a native GPU/firmware generation.
namespace ExecutionTranscript {
namespace E=ExecutionCodec;namespace P=ExecutionPlan;namespace C=ChannelCodec;namespace R=GSPComputePrep;
struct Result {bool passed=false;unsigned completed=0,records=0,events=0,pages=0,nextSequence=0,channelId=~0U,subdeviceMask=~0U,rawToken=~0U,candidate=0,graphicsCaps=0;
  P::Plan privatePlan;WorkSubmitToken::Runlist runlist;};
inline bool verify(const unsigned char *requests,unsigned requestBytes,const unsigned char *records,unsigned recordBytes,unsigned firstSequence,
 const C::Plan &golden,unsigned predecessorId,unsigned char *scratch,Result &out){
  const void *inputs[]={requests,records,&golden,scratch};const size_t lengths[]={requestBytes,recordBytes,sizeof(golden),4096};
  for(unsigned i=0;i<4;++i)if(inputs[i]&&!P::disjoint(inputs[i],lengths[i],&out,sizeof(out)))return false;
  out={};
  if(!requests||requestBytes!=E::RequestBytes||!records||recordBytes<E::RequestBytes||recordBytes>R::MaxBytes||recordBytes%4096||
     firstSequence>~0U-R::MaxRecords||predecessorId!=3||!P::goldenFits(golden)||!scratch)return false;
  for(unsigned i=0;i<3;++i)if(!P::disjoint(inputs[i],lengths[i],scratch,4096))return false;
  unsigned offset=0;out.nextSequence=firstSequence;
  while(offset<recordBytes){
    if(out.completed>=E::Steps||out.records>=R::MaxRecords)return false;
    const unsigned count=R::get32(records+offset+40);
    if(!count||count>16||count>(recordBytes-offset)/4096)return false;
    GSPInitEvents::Record r;
    if(!GSPInitEvents::decode(records+offset,count*4096,out.nextSequence,r))return false;
    ++out.records;out.pages+=count;++out.nextSequence;
    if(r.function==E::function(out.completed)){
      const unsigned step=out.completed;
      if(!E::request(step,E::FirstSequence+step,golden,predecessorId,out.privatePlan,scratch,4096))return false;
      for(unsigned i=0;i<4096;++i)if(requests[step*4096+i]!=scratch[i])return false;
      E::Reply reply;
      if(!E::reply(records+offset,count*4096,r.sequence,step,golden,predecessorId,out.privatePlan,reply))return false;
      if(step==E::ChannelStep){out.channelId=reply.channelId;out.subdeviceMask=reply.subdeviceMask;}
      if(step==E::GraphicsStep)out.graphicsCaps=reply.graphicsCaps;
      if(step==E::SizeStep)out.privatePlan=reply.privatePlan;
      if(step==E::FifoStep)out.runlist=reply.runlist;
      if(step==E::TokenStep)out.rawToken=reply.rawToken;
      ++out.completed;
    }else{if(!R::asyncSupported(records+offset,r))return false;++out.events;}
    offset+=count*4096;
  }
  if(out.completed!=E::Steps||!P::valid(out.privatePlan,golden)||!WorkSubmitToken::compose(out.runlist,P::HardwareChannelId,out.rawToken,out.candidate))return false;
  out.passed=true;return true;
}
}
