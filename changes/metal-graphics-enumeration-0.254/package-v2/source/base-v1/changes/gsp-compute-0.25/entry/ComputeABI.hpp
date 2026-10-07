#pragma once
#include "ComputeCapture.hpp"
#include "../../gsp-channel-0.23/native/ChannelABI.hpp"
namespace ComputeABI {
using U64=unsigned long long;
constexpr U64 MemoryMagic=0x525458434d4d3235ULL,SubmitMagic=0x525458434d533235ULL,CaptureMagic=0x525458434d433235ULL;
inline void memory(const ComputeMemory::Result &r,const ChannelABI::Owner &o,unsigned writePhase,unsigned registerPhase,U64 *out){
 ChannelABI::memory(r.memory,2,o,out);out[0]=MemoryMagic;
 out[57]=r.hostVerified;out[58]=r.imagesPrepared;out[59]=r.claimed;out[60]=writePhase;out[61]=registerPhase;out[62]=ComputeMemory::Base;out[63]=ComputeMemory::Bytes;
}
inline void submit(const ComputeSubmit::Result &r,const ChannelABI::Owner &o,bool claimed,bool notified,unsigned phase,bool memoryReady,U64 *out){
 for(unsigned i=0;i<80;++i)out[i]=0;out[0]=SubmitMagic;out[1]=1;out[2]=o.generation;
 out[3]=r.claimed;out[4]=r.passed;out[5]=r.failure;out[6]=r.commandAttempted;out[7]=r.entryAttempted;out[8]=r.putAttempted;out[9]=r.bellAttempted;
 out[10]=r.immutableVerified;out[11]=r.guardsVerified;out[12]=r.stable;out[13]=r.operations;out[14]=r.reads;out[15]=r.writes;out[16]=r.polls;out[17]=r.token;
 out[18]=r.lastAddress;out[19]=r.initialGet;out[20]=r.initialPut;out[21]=r.initialOutput;out[22]=r.initialCompletion;
 out[23]=r.get;out[24]=r.put;out[25]=r.hostFence;out[26]=r.output;out[27]=r.completion;out[28]=r.started;out[29]=r.elapsed;
 out[30]=ComputeSubmit::Command;out[31]=ComputeSubmit::Entry;out[32]=HostFence::Get;out[33]=HostFence::Put;out[34]=QmdProfile::OutputPhysical;out[35]=QmdProfile::FencePhysical;
 out[36]=ComputeSubmit::CommandVA;out[37]=QmdProfile::OutputVA;out[38]=QmdProfile::FenceVA;out[39]=QmdProfile::OutputValue;out[40]=QmdProfile::FenceValue;
 out[41]=HostFence::Doorbell;out[42]=ComputeSubmit::BudgetNs;out[43]=ComputeSubmit::MaxOperations;
 out[44]=o.phase;out[45]=o.pinned;out[46]=o.owned;out[47]=o.command;out[48]=claimed;out[49]=notified;out[50]=phase;out[51]=32;out[52]=8;
 for(unsigned i=0;i<8;++i)out[53+i]=GSPComputePrep::get32(r.command+i*4);out[61]=GMMULeaves::read64(r.entry);out[62]=o.lease;out[63]=memoryReady;
}
inline void capture(const ComputeCapture::Result &r,const ChannelABI::Owner &o,U64 *out){
 for(unsigned i=0;i<64;++i)out[i]=0;out[0]=CaptureMagic;out[1]=1;out[2]=o.generation;
 out[3]=r.attempted;out[4]=r.passed;out[5]=r.failure;out[6]=r.rootBytes;out[7]=r.childBytes;out[8]=r.deviceBytes;out[9]=r.requested;
 out[10]=r.reads;out[11]=r.lastAddress;out[12]=r.lastValue;out[13]=r.elapsed;out[14]=o.phase;out[15]=o.pinned;out[16]=o.owned;out[17]=o.command;
 out[18]=GMMULeaves::OldBase;out[19]=GMMULeaves::NewBase;out[20]=ComputeCapture::RootBytes;out[21]=ComputeCapture::MaxChildren;
 out[22]=ComputeCapture::DeviceBytes;out[23]=ComputeCapture::BudgetNs;
 for(unsigned i=0;i<9;++i)out[24+i]=ComputeCapture::Addresses[i];
}
}
