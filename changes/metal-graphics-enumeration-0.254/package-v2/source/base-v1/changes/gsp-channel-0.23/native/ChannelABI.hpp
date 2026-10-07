#pragma once
#include "ChannelSnapshot.hpp"
#include "../transactions/ChannelTransactions.hpp"
namespace ChannelABI {
using U64=unsigned long long;
constexpr U64 MemoryMagic=0x5254584d454d3233ULL,RMMagic=0x52545843484e3233ULL,PlanMagic=0x525458504c4e3233ULL,SnapshotMagic=0x525458534e503233ULL;
struct Owner {U64 generation=0,phase=0,pinned=0,owned=0,command=0,lease=0,mapped=0,barBase=0,physical=0,ringClaimed=0,contextsClaimed=0,windowObserved=0,physicalMode=0,excludedNs=0,queueClaimed=0;};
inline void memory(const ChannelMemory::Result &r,unsigned which,const Owner &o,U64 *out){
  for(unsigned i=0;i<64;++i)out[i]=0;
  out[0]=MemoryMagic;out[1]=1;out[2]=o.generation;out[3]=which;
  out[4]=r.attempted;out[5]=r.passed;out[6]=unsigned(r.failure);out[7]=r.modified;out[8]=r.parentAttempted;
  out[9]=r.backingVerified;out[10]=r.childrenVerified;out[11]=r.windowSaved;out[12]=r.windowRestored;
  out[13]=r.operations;out[14]=r.reads;out[15]=r.writes;out[16]=r.inspectedBytes;out[17]=r.zeroedBytes;out[18]=r.verifiedBackingBytes;
  out[19]=r.childBytes;out[20]=r.verifiedChildBytes;out[21]=r.linksPublished;out[22]=r.lastAddress;out[23]=r.lastValue;
  out[24]=r.windowBefore;out[25]=r.windowAfter;out[26]=r.start;out[27]=r.elapsed;out[28]=r.cleanupElapsed;
  const auto &a=r.invalidation;out[29]=a.passed;out[30]=a.completed;out[31]=a.commandAttempted;out[32]=unsigned(a.failure);
  out[33]=a.operations;out[34]=a.reads;out[35]=a.writes;out[36]=a.lastAddress;out[37]=a.lastValue;out[38]=a.start;out[39]=a.lastTime;out[40]=a.elapsed;
  out[41]=o.lease;out[42]=o.mapped;out[43]=o.barBase;out[44]=o.physical;out[45]=o.phase;out[46]=o.pinned;out[47]=o.owned;out[48]=o.command;
  out[49]=o.ringClaimed;out[50]=o.contextsClaimed;out[51]=o.windowObserved;out[52]=o.physicalMode;out[53]=o.excludedNs;
  out[54]=ChannelMemory::BudgetNs;out[55]=ChannelMemory::CleanupNs;out[56]=ChannelMemory::MaxOperations;
}
inline void plan(const ChannelCodec::Plan &p,U64 generation,U64 *out){
  for(unsigned i=0;i<128;++i)out[i]=0;
  out[0]=PlanMagic;out[1]=1;out[2]=generation;out[3]=ChannelCodec::planValid(p);
  if(!out[3])return;out[4]=p.physicalEnd;out[5]=p.virtualEnd;out[6]=p.backingBytes;out[7]=9;
  for(unsigned i=0;i<9;++i){const auto &b=p.buffers[i];auto *e=out+8+i*9;
    e[0]=b.id;e[1]=b.kind;e[2]=b.physical;e[3]=b.virtualAddress;e[4]=b.bytes;e[5]=b.allocated;e[6]=b.alignment;e[7]=b.usePhysical;e[8]=b.useVirtual;
  }
}
inline void snapshot(const ChannelSnapshot::Result &r,const Owner &o,U64 *out){
  for(unsigned i=0;i<64;++i)out[i]=0;
  out[0]=SnapshotMagic;out[1]=1;out[2]=o.generation;out[3]=r.attempted;out[4]=r.passed;out[5]=r.failure;out[6]=r.rootBytes;out[7]=r.childBytes;
  out[8]=r.requested;out[9]=r.reads;out[10]=r.lastAddress;out[11]=r.lastValue;out[12]=r.elapsed;out[13]=o.phase;out[14]=o.pinned;out[15]=o.owned;out[16]=o.command;
}
inline void rm(const ChannelTransactions::Result &result,const Owner &o,bool ringPassed,bool contextPassed,bool snapshotPassed,U64 *out){
  for(unsigned i=0;i<64;++i)out[i]=0;const auto &r=result.rpc;
  out[0]=RMMagic;out[1]=1;out[2]=o.generation;out[3]=r.validated;out[4]=r.attempted;out[5]=r.passed;out[6]=r.failure;out[7]=r.step;
  out[8]=r.completed;out[9]=r.sent;out[10]=r.doorbells;out[11]=r.count;out[12]=r.pages;out[13]=r.bytes;
  out[14]=r.txWriter;out[15]=r.txReader;out[16]=r.rxReader;out[17]=r.rxProducer;out[18]=r.rxSequence;
  out[19]=r.ticks;out[20]=r.polls;out[21]=r.imports;out[22]=r.reads;out[23]=r.writes;out[24]=r.publishes;
  out[25]=r.elapsedNs;out[26]=GSPComputePrep::BudgetNs;out[27]=GSPComputePrep::MaxTicks;
  out[28]=r.initialReader;out[29]=r.initialSequence;out[30]=r.consumerWrites;out[31]=r.prefixConsumed;
  out[32]=r.lastFunction;out[33]=r.lastResult;out[34]=r.lastParamStatus;out[35]=r.lastAddress;out[36]=r.lastValue;
  out[37]=GSPComputePrep::Client;out[38]=ChannelCodec::Channel;out[39]=ChannelCodec::Compute;out[40]=ChannelCodec::Copy;
  out[41]=4096;out[42]=GSPComputePrep::MaxRecords;out[43]=GSPComputePrep::MaxPages;out[44]=o.queueClaimed;out[45]=o.owned;out[46]=o.lease;out[47]=o.pinned;out[48]=o.command;out[49]=r.startNs;
  out[50]=result.context.valid;out[51]=result.contextPreparationAttempted;out[52]=result.contextPrepared;out[53]=result.channelId;out[54]=result.subdeviceMask;
  out[55]=o.excludedNs;out[56]=ringPassed;out[57]=contextPassed;out[58]=snapshotPassed;out[59]=5*4096;out[60]=o.phase;
  out[61]=r.passed&&ringPassed&&contextPassed&&snapshotPassed&&(o.phase==17||o.phase==18);
}
}
