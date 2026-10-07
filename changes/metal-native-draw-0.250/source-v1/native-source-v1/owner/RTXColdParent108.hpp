#pragma once
#include "RTXAcceleratorIdentity103.hpp"
namespace RTXColdParent108 {
struct Node {RTXAccelerator103::Node entry;bool numbersZero=false,flagsClear=false,dataABI=false,dispatchABI=false,rootABI=false,programABI=false,programInputABI=false;};
inline bool eligible(const Node &n){
 return RTXAccelerator103::eligibleParent(n.entry)&&!n.entry.isPCI&&!n.entry.isChild&&
  !n.entry.isAccelerator&&n.numbersZero&&n.flagsClear&&n.dataABI&&n.dispatchABI&&n.rootABI&&n.programABI&&n.programInputABI;
}
class Reader {
public:
 virtual ~Reader()=default;
 virtual bool read(uint32_t handle,Node &n)=0;
 // Any returned reference is owned, including error paths.
 virtual bool parent(uint32_t handle,uint32_t &owned)=0;
 virtual void release(uint32_t handle)=0;
};
class Owned {
 Reader &reader;uint32_t handle=0;
public:
 explicit Owned(Reader&r):reader(r){}
 Owned(const Owned&)=delete;Owned&operator=(const Owned&)=delete;
 ~Owned(){if(handle)reader.release(handle);}
 void adopt(uint32_t next){if(handle)reader.release(handle);handle=next;}
 uint32_t get()const{return handle;}
};
// Borrow the caller's service reference. Acquire/release only ancestor handles.
// This performs no open, queue access or firmware operation; callers still own
// the subsequent one-attempt connection/ABI validation transaction.
inline bool validate(Reader &reader,uint32_t service,uint64_t &generation){
 generation=0;if(!service)return false;Node current;
 if(!reader.read(service,current)||!eligible(current))return false;
 const uint64_t candidate=current.entry.registry;uint64_t seen[16]={};unsigned count=0;
 Owned cursor(reader);uint32_t handle=service;
 for(unsigned depth=0;depth<16;++depth){
  const auto &n=current.entry;if(!n.registry)return false;
  for(unsigned i=0;i<count;++i)if(seen[i]==n.registry)return false;
  seen[count++]=n.registry;
  if(n.isPCI){
   if(!n.validPCI||n.vendor!=0x10de||n.device!=0x2520||n.subvendor!=0x1043||n.subdevice!=0x104c)return false;
   generation=candidate;return true;
  }
  if(depth==15)return false;
  uint32_t next=0;const bool got=reader.parent(handle,next);cursor.adopt(next);
  if(!got||!cursor.get())return false;handle=cursor.get();
  if(!reader.read(handle,current))return false;
 }
 return false;
}
}
