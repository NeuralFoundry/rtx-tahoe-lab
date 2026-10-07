#pragma once
#include "../layout/ExecutionPlan.hpp"
#include "../execution/WorkSubmitToken.hpp"
// Fixed execution-channel protocol proposal; no queue, memory or device access.
namespace ExecutionCodec {
namespace P=ExecutionPlan;namespace C=ChannelCodec;namespace R=GSPComputePrep;namespace W=WorkSubmitToken;
constexpr unsigned Steps=14,RequestBytes=Steps*4096;
constexpr unsigned FirstSequence=ExternalVAS::FinalProducer,FinalProducer=FirstSequence+Steps;
static_assert(Steps<=R::MaxRecords,"Full reply transcript fits existing record budget");
constexpr unsigned GroupStep=0,ShareStep=1,ChannelStep=2,ComputeStep=3,GraphicsStep=4,SizeStep=5,PhysicalStep=6,VirtualStep=7,
  CopyStep=8,FifoStep=9,BindStep=10,EnableStep=11,ScheduleStep=12,TokenStep=13;
inline bool allocation(unsigned step){return step==GroupStep||step==ShareStep||step==ChannelStep||step==ComputeStep||step==GraphicsStep||step==CopyStep;}
inline unsigned function(unsigned step){return allocation(step)?103:76;}
inline unsigned params(unsigned step){constexpr unsigned sizes[Steps]={20,12,368,0,16,1664,560,560,0,3212,4,2,2,4};return step<Steps?sizes[step]:0;}
inline unsigned header(unsigned step){return allocation(step)?32:24;}
inline unsigned control(unsigned step){return step==SizeStep?0x20800a32:step==PhysicalStep||step==VirtualStep?0x2080012b:step==FifoStep?0x20801112:
  step==ScheduleStep?ContextSharePlan::GroupSchedule:step==BindStep?SubmitCodec::Controls[0]:step==EnableStep?SubmitCodec::Controls[1]:step==TokenStep?SubmitCodec::Controls[2]:0;}
inline unsigned object(unsigned step){return step==GroupStep||step==ScheduleStep?P::Group:step==ShareStep?P::Share:
  step==ChannelStep||step==BindStep||step==EnableStep||step==TokenStep?P::Channel:step==ComputeStep?P::Compute:step==GraphicsStep?P::Graphics:step==CopyStep?P::Copy:P::Subdevice;}
inline unsigned parent(unsigned step){return step==GroupStep?P::Device:step==ShareStep||step==ChannelStep?P::Group:P::Channel;}
inline unsigned klass(unsigned step){return step==GroupStep?ContextSharePlan::GroupClass:step==ShareStep?ContextSharePlan::ShareClass:
  step==ChannelStep?0xc56f:step==ComputeStep?0xc7c0:step==GraphicsStep?0xc797:step==CopyStep?0xc7b5:0;}
inline bool parameters(unsigned step,const C::Plan &golden,unsigned predecessorId,const P::Plan &plan,unsigned char *out,unsigned capacity){
  if(step>=Steps||!out||capacity<params(step)||predecessorId!=3||!P::goldenFits(golden)||
     !P::disjoint(&golden,sizeof(golden),out,params(step))||!P::disjoint(&plan,sizeof(plan),out,params(step)))return false;
  if(step==GroupStep||step==ShareStep||step==ScheduleStep)return ContextSharePlan::parameters(
    step==GroupStep?ContextSharePlan::Operation::GroupAlloc:step==ShareStep?ContextSharePlan::Operation::ShareAlloc:ContextSharePlan::Operation::Schedule,out,capacity);
  if(step==ChannelStep)return P::channelParameters(golden,predecessorId,out,capacity);
  if(step==PhysicalStep||step==VirtualStep)return P::promotion(plan,golden,step==PhysicalStep,out,capacity);
  for(unsigned i=0;i<params(step);++i)out[i]=0;
  // NVIDIA 570.144 NV_GR_ALLOCATION_PARAMETERS: version 2, no flags, size 16; caps is output.
  if(step==GraphicsStep){R::put32(out,2);R::put32(out+8,16);}
  if(step==BindStep)R::put32(out,1);if(step==EnableStep)out[0]=1;if(step==TokenStep)R::put32(out,~0U);return true;
}
inline bool request(unsigned step,unsigned sequence,const C::Plan &golden,unsigned predecessorId,const P::Plan &plan,unsigned char *out,unsigned capacity){
  if(step>=Steps||sequence==~0U||!out||capacity<4096||predecessorId!=3||!P::goldenFits(golden)||
     ((step==PhysicalStep||step==VirtualStep)&&!P::valid(plan,golden))||!P::disjoint(&golden,sizeof(golden),out,4096)||!P::disjoint(&plan,sizeof(plan),out,4096))return false;
  for(unsigned i=0;i<4096;++i)out[i]=0;
  R::put32(out+36,sequence);R::put32(out+40,1);R::put32(out+48,0x03000000);R::put32(out+52,0x43505256);
  R::put32(out+56,32+header(step)+params(step));R::put32(out+60,function(step));R::put32(out+64,~0U);R::put32(out+68,~0U);
  R::put32(out+80,P::Client);R::put32(out+84,allocation(step)?parent(step):object(step));
  if(allocation(step)){R::put32(out+88,object(step));R::put32(out+92,klass(step));R::put32(out+100,params(step));}
  else{R::put32(out+88,control(step));R::put32(out+96,params(step));}
  if(!parameters(step,golden,predecessorId,plan,out+80+header(step),params(step)))return false;
  unsigned sum=0;for(unsigned i=0;i<((80+header(step)+params(step)+7)&~7U);i+=4)sum^=R::get32(out+i);
  R::put32(out+32,sum);return true;
}
struct Reply {bool accepted=false;unsigned channelId=~0U,subdeviceMask=~0U,rawToken=~0U,subcontextId=~0U,graphicsCaps=0;P::Plan privatePlan;W::Runlist runlist;};
inline bool reply(const unsigned char *raw,unsigned bytes,unsigned sequence,unsigned step,const C::Plan &golden,unsigned predecessorId,const P::Plan &plan,Reply &out){
  if(!P::disjoint(raw,bytes,&out,sizeof(out))||!P::disjoint(&golden,sizeof(golden),&out,sizeof(out))||!P::disjoint(&plan,sizeof(plan),&out,sizeof(out)))return false;
  out={};if(step>=Steps||sequence==~0U||predecessorId!=3||!P::goldenFits(golden))return false;
  GSPInitEvents::Record r;
  if(!GSPInitEvents::decode(raw,bytes,sequence,r)||r.function!=function(step)||r.result||R::get32(raw+68)||R::get32(raw+72)||
     r.payloadBytes!=header(step)+params(step)||R::get32(raw+80)!=P::Client)return false;
  if(allocation(step)){
    if(R::get32(raw+84)!=parent(step)||R::get32(raw+88)!=object(step)||R::get32(raw+92)!=klass(step)||R::get32(raw+96)||
       R::get32(raw+100)!=params(step)||R::get32(raw+104)||R::get32(raw+108))return false;
  }else if(R::get32(raw+84)!=object(step)||R::get32(raw+88)!=control(step)||R::get32(raw+92)||R::get32(raw+96)!=params(step)||R::get32(raw+100))return false;
  if(step==SizeStep){if(!P::make(raw+104,1664,golden,out.privatePlan))return false;}
  else if(step==FifoStep){if(!W::fifo(raw,bytes,sequence,out.runlist,P::Client,P::Subdevice))return false;}
  else if(step==GraphicsStep){
    if(R::get32(raw+112)!=2||R::get32(raw+116)!=0||R::get32(raw+120)!=16)return false;
    out.graphicsCaps=R::get32(raw+124); // Device output only, never an address or permission.
  }
  else if(step==TokenStep){out.rawToken=R::get32(raw+104);if(out.rawToken!=P::HardwareChannelId)return false;}
  else if(step==ShareStep){
    ContextSharePlan::ReplyParameters share;
    if(!ContextSharePlan::replyParameters(ContextSharePlan::Operation::ShareAlloc,raw+112,12,share))return false;
    out.subcontextId=share.subcontext;
  }
  else if(params(step)){
    unsigned char canonical[560];if(!parameters(step,golden,predecessorId,plan,canonical,sizeof(canonical)))return false;
    // hPhysChannelGroup is an internal RM output, not an ownership/address input.
    for(unsigned i=0;i<params(step);++i){if(step==ChannelStep&&((i>=132&&i<140)||(i>=240&&i<244)))continue;if(raw[80+header(step)+i]!=canonical[i])return false;}
    if(step==ChannelStep){out.channelId=R::get32(raw+112+132);out.subdeviceMask=R::get32(raw+112+136);
      if(!P::sessionId(out.channelId,predecessorId)||out.subdeviceMask>1)return false;}
  }
  out.accepted=true;return true;
}
}
