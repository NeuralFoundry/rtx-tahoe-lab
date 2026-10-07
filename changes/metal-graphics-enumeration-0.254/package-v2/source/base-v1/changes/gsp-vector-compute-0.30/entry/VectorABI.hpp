#pragma once
#include "VectorCapture.hpp"
#include "../../gsp-channel-0.23/native/ChannelABI.hpp"
namespace VectorABI {
using U64=unsigned long long;
constexpr U64 MemoryMagic=0x525458564d4d3330ULL,SubmitMagic=0x52545856534d3330ULL,CaptureMagic=0x5254585643503330ULL;
constexpr unsigned SubmitWords=192,SubmitBytes=SubmitWords*8;
inline void memory(const VectorMemory::Result &r,const ChannelABI::Owner &o,unsigned writePhase,unsigned registerPhase,U64 *out){
 ChannelABI::memory(r.memory,2,o,out);out[0]=MemoryMagic;
 out[57]=r.hostVerified;out[58]=r.imagesPrepared;out[59]=r.claimed;out[60]=writePhase;out[61]=registerPhase;out[62]=VectorMemory::Base;out[63]=VectorMemory::Bytes;
}
inline void submit(const VectorSubmit::Result &r,const ChannelABI::Owner &o,bool claimed,bool notified,unsigned phase,bool memoryReady,U64 *out){
 for(unsigned i=0;i<SubmitWords;++i)out[i]=0;out[0]=SubmitMagic;out[1]=1;out[2]=o.generation;
 out[3]=r.claimed;out[4]=r.passed;out[5]=r.failure;out[6]=r.commandAttempted;out[7]=r.entryAttempted;out[8]=r.putAttempted;out[9]=r.bellAttempted;
 out[10]=r.immutableVerified;out[11]=r.guardsVerified;out[12]=r.stable;out[13]=r.operations;out[14]=r.reads;out[15]=r.writes;out[16]=r.polls;out[17]=r.token;
 out[18]=r.lastAddress;out[19]=r.initialGet;out[20]=r.initialPut;out[21]=r.initialCompletion;
 out[22]=r.get;out[23]=r.put;out[24]=r.hostFence;out[25]=r.completion;out[26]=r.completedElements;out[27]=RtxVector030::DefaultCount;out[28]=r.started;out[29]=r.elapsed;
 out[30]=VectorSubmit::Command;out[31]=VectorSubmit::Entry;out[32]=HostFence::Get;out[33]=HostFence::Put;out[34]=QmdProfile::OutputPhysical;out[35]=QmdProfile::FencePhysical;
 out[36]=VectorSubmit::CommandVA;out[37]=QmdProfile::OutputVA;out[38]=QmdProfile::FenceVA;out[39]=RtxVector030::InputAVA;out[40]=RtxVector030::InputBVA;
 out[41]=HostFence::Doorbell;out[42]=VectorSubmit::BudgetNs;out[43]=VectorSubmit::MaxOperations;
 out[44]=o.phase;out[45]=o.pinned;out[46]=o.owned;out[47]=o.command;out[48]=claimed;out[49]=notified;out[50]=phase;out[51]=32;out[52]=8;
 for(unsigned i=0;i<8;++i)out[53+i]=GSPComputePrep::get32(r.command+i*4);out[61]=GMMULeaves::read64(r.entry);out[62]=o.lease;out[63]=memoryReady;
 for(unsigned i=0;i<64;++i){out[64+i]=r.initialOutput[i];out[128+i]=r.output[i];}
}
inline void capture(const VectorCapture::Result &r,const ChannelABI::Owner &o,U64 *out){
 for(unsigned i=0;i<64;++i)out[i]=0;out[0]=CaptureMagic;out[1]=1;out[2]=o.generation;
 out[3]=r.attempted;out[4]=r.passed;out[5]=r.failure;out[6]=r.rootBytes;out[7]=r.childBytes;out[8]=r.deviceBytes;out[9]=r.requested;
 out[10]=r.reads;out[11]=r.lastAddress;out[12]=r.lastValue;out[13]=r.elapsed;out[14]=o.phase;out[15]=o.pinned;out[16]=o.owned;out[17]=o.command;
 out[18]=GMMULeaves::OldBase;out[19]=GMMULeaves::NewBase;out[20]=VectorCapture::RootBytes;out[21]=VectorCapture::MaxChildren;out[22]=VectorCapture::DeviceBytes;out[23]=VectorCapture::BudgetNs;
 for(unsigned i=0;i<9;++i)out[24+i]=VectorCapture::Addresses[i];
}
}
