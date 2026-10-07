#pragma once
#include "../../gsp-channel-0.23/native/ChannelABI.hpp"
#include "ExecutionCapture.hpp"
namespace ExecutionABI {
using U64=unsigned long long;
constexpr U64 RMMagic=0x52545845584e3234ULL,MemoryMagic=0x52545845584d3234ULL,PlanMagic=0x5254584558503234ULL;
constexpr U64 FenceMagic=0x525458464e433234ULL,CaptureMagic=0x5254584341503234ULL,SnapshotMagic=0x5254584558533234ULL;
inline void memory(const ChannelMemory::Result &r,unsigned which,const ChannelABI::Owner &o,U64 *out){ChannelABI::memory(r,which,o,out);out[0]=MemoryMagic;}
inline void snapshot(const ChannelSnapshot::Result &r,const ChannelABI::Owner &o,U64 *out){ChannelABI::snapshot(r,o,out);out[0]=SnapshotMagic;}
inline void plan(const ExecutionPlan::Plan &p,const ChannelCodec::Plan &golden,U64 generation,U64 *out){
  for(unsigned i=0;i<64;++i)out[i]=0;out[0]=PlanMagic;out[1]=1;out[2]=generation;out[3]=ExecutionPlan::valid(p,golden);
  if(!out[3])return;out[4]=p.physicalEnd;out[5]=p.virtualEnd;out[6]=p.backingBytes;out[7]=3;
  for(unsigned i=0;i<3;++i){const auto &b=p.buffers[i];auto *e=out+8+i*7;e[0]=b.id;e[1]=b.kind;e[2]=b.physical;e[3]=b.va;e[4]=b.bytes;e[5]=b.allocated;e[6]=b.alignment;}
}
inline void rm(const ExecutionTransactions::Result &e,const ChannelABI::Owner &o,unsigned consumed,U64 *out){
  for(unsigned i=0;i<80;++i)out[i]=0;const auto &r=e.rpc;
  out[0]=RMMagic;out[1]=1;out[2]=o.generation;out[3]=r.validated;out[4]=r.attempted;out[5]=r.passed;out[6]=r.failure;out[7]=r.step;
  out[8]=r.completed;out[9]=r.sent;out[10]=r.doorbells;out[11]=r.count;out[12]=r.pages;out[13]=r.bytes;
  out[14]=r.txWriter;out[15]=r.txReader;out[16]=r.rxReader;out[17]=r.rxProducer;out[18]=r.rxSequence;
  out[19]=r.ticks;out[20]=r.polls;out[21]=r.imports;out[22]=r.reads;out[23]=r.writes;out[24]=r.publishes;
  out[25]=r.elapsedNs;out[26]=GSPComputePrep::BudgetNs;out[27]=GSPComputePrep::MaxTicks;out[28]=r.initialReader;out[29]=r.initialSequence;
  out[30]=r.consumerWrites;out[31]=r.prefixConsumed;out[32]=r.lastFunction;out[33]=r.lastResult;out[34]=r.lastParamStatus;out[35]=r.lastAddress;out[36]=r.lastValue;
  out[37]=ExecutionPlan::Client;out[38]=ExecutionPlan::Channel;out[39]=ExecutionPlan::Compute;out[40]=ExecutionPlan::Copy;
  out[41]=4096;out[42]=GSPComputePrep::MaxRecords;out[43]=GSPComputePrep::MaxPages;out[44]=o.queueClaimed;out[45]=o.owned;out[46]=o.lease;out[47]=o.pinned;out[48]=o.command;out[49]=r.startNs;
  out[50]=e.fixedPreparationAttempted;out[51]=e.fixedPrepared;out[52]=e.contextPreparationAttempted;out[53]=e.contextPrepared;out[54]=e.channelId;out[55]=e.subdeviceMask;
  out[56]=e.rawToken;out[57]=e.candidate;out[58]=o.excludedNs;out[59]=ExecutionCodec::RequestBytes;out[60]=o.phase;out[61]=consumed;
  out[62]=e.runlist.valid;out[63]=e.runlist.sequence;out[64]=e.runlist.entry;out[65]=e.runlist.id;out[66]=e.runlist.pbdmas;
  out[67]=e.runlist.pbdma[0];out[68]=e.runlist.pbdma[1];out[69]=e.runlist.fault[0];out[70]=e.runlist.fault[1];
}
constexpr U64 ExternalMagic=0x5254584556413237ULL;
inline void external(const ExternalSetup::Result &e,const ChannelABI::Owner &o,unsigned consumed,bool complete,U64 *out){
  for(unsigned i=0;i<64;++i)out[i]=0;const auto &r=e.rpc;
  out[0]=ExternalMagic;out[1]=1;out[2]=o.generation;out[3]=r.validated;out[4]=r.attempted;out[5]=r.passed;out[6]=r.failure;out[7]=r.step;
  out[8]=r.completed;out[9]=r.sent;out[10]=r.doorbells;out[11]=r.count;out[12]=r.pages;out[13]=r.bytes;
  out[14]=r.txWriter;out[15]=r.txReader;out[16]=r.rxReader;out[17]=r.rxProducer;out[18]=r.rxSequence;
  out[19]=r.ticks;out[20]=r.polls;out[21]=r.imports;out[22]=r.reads;out[23]=r.writes;out[24]=r.publishes;
  out[25]=r.elapsedNs;out[26]=GSPComputePrep::BudgetNs;out[27]=GSPComputePrep::MaxTicks;out[28]=r.initialReader;out[29]=r.initialSequence;
  out[30]=r.consumerWrites;out[31]=r.prefixConsumed;out[32]=r.lastFunction;out[33]=r.lastResult;out[34]=r.lastParamStatus;out[35]=r.lastAddress;out[36]=r.lastValue;
  out[37]=ExternalVAS::Client;out[38]=ExternalVAS::Device;out[39]=ExternalVAS::Subdevice;out[40]=ExternalVAS::Vaspace;
  out[41]=4096;out[42]=GSPComputePrep::MaxRecords;out[43]=GSPComputePrep::MaxPages;out[44]=o.queueClaimed;out[45]=o.owned;out[46]=o.lease;out[47]=o.pinned;out[48]=o.command;out[49]=r.startNs;
  out[50]=e.directoryChecks;out[51]=e.directoryAcknowledged;out[52]=consumed;out[53]=complete;
  out[54]=e.vas.base;out[55]=e.vas.size;out[56]=e.vas.internalLo;out[57]=e.vas.internalHi;out[58]=e.vas.bigPage;
  out[59]=ExternalVAS::RootPhysical;out[60]=ExternalVAS::RootEntries;out[61]=ExternalVAS::DirectoryFlags;out[62]=ExternalVAS::RequestBytes;out[63]=o.phase;
}
inline void fence(const HostFence::Result &r,const ChannelABI::Owner &o,bool claimed,bool notified,unsigned nativePhase,U64 *out){
  for(unsigned i=0;i<64;++i)out[i]=0;out[0]=FenceMagic;out[1]=1;out[2]=o.generation;
  out[3]=r.claimed;out[4]=r.passed;out[5]=unsigned(r.failure);out[6]=r.commandAttempted;out[7]=r.entryAttempted;out[8]=r.putAttempted;out[9]=r.bellAttempted;
  out[10]=r.operations;out[11]=r.polls;out[12]=r.reads;out[13]=r.writes;out[14]=r.initialGet;out[15]=r.initialPut;out[16]=r.initialFence;
  out[17]=r.lastGet;out[18]=r.lastPut;out[19]=r.lastFence;out[20]=r.token;out[21]=r.started;out[22]=r.elapsed;
  out[23]=HostFence::Doorbell;out[24]=HostFence::BudgetNs;out[25]=HostFence::MaxOperations;out[26]=HostFence::Ring;out[27]=HostFence::Command;out[28]=HostFence::Fence;
  out[29]=HostFence::Get;out[30]=HostFence::Put;out[31]=SubmitCodec::CommandVA;out[32]=SubmitCodec::FenceVA;out[33]=SubmitCodec::FenceValue;
  out[34]=o.phase;out[35]=o.pinned;out[36]=o.owned;out[37]=o.command;out[38]=claimed;out[39]=notified;out[40]=nativePhase;
  for(unsigned i=0;i<5;++i)out[41+i]=GSPComputePrep::get32(r.command+4*i);
  out[46]=GMMULeaves::read64(r.entry);
}
inline void capture(const ExecutionCapture::Result &r,const ChannelABI::Owner &o,U64 *out){
  for(unsigned i=0;i<32;++i)out[i]=0;out[0]=CaptureMagic;out[1]=1;out[2]=o.generation;
  out[3]=r.attempted;out[4]=r.passed;out[5]=r.failure;out[6]=r.bytes;out[7]=r.reads;out[8]=r.lastAddress;out[9]=r.elapsed;
  out[10]=o.phase;out[11]=o.pinned;out[12]=o.owned;out[13]=o.command;
  for(unsigned i=0;i<3;++i)out[14+i]=ExecutionCapture::Addresses[i];
}
}
