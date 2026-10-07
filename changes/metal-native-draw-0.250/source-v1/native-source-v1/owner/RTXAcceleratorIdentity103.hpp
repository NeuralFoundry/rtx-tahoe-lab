#pragma once
#include <stdint.h>
#include <stddef.h>
#include "RTXMemoryEvidence107.hpp"

// Shared publication/connection contract. No IOKit open or GPU operation.
namespace RTXAccelerator103 {
static constexpr char ChildClass[]="RTXMetalAccelerator107";
static constexpr char ChildVersion[]="0.107.0";
#if defined(RTX_GRAPHICS242)
static constexpr char ParentVersion[]="0.83.1";
#else
static constexpr char ParentVersion[]="0.81.0";
#endif
static constexpr char BrokerService[]="local.emre.RTXMetalBroker059.port107.v1";
static constexpr uint64_t Identity=0x252010deULL,Subsystem=0x104c1043ULL;
struct Node {
 RTXMemory107::Evidence memory;bool validMemory=false;
 uint64_t registry=0,parentRegistry=0,targetIdentity=0,targetSubsystem=0;
 uint32_t vendor=0,device=0,subvendor=0,subdevice=0;
 bool isChild=false,isAccelerator=false,isProbe=false,isPCI=false;
 bool childVersion=false,advertisedParentVersion=false,parentVersion=false;
 bool complete=false,passed=false,validParentRegistry=false,validTargets=false,validPCI=false;
};
inline bool eligibleParent(const Node &n){
 return n.validMemory&&RTXMemory107::valid(n.memory)&&n.memory.generation==n.registry&&n.registry&&n.isProbe&&n.parentVersion&&n.complete&&n.passed&&n.validTargets&&n.targetIdentity==Identity&&n.targetSubsystem==Subsystem;
}
struct Binding {uint64_t childRegistry=0,parentGeneration=0;RTXMemory107::Evidence memory;};
class Reader {
public:
 virtual ~Reader()=default;
 virtual bool read(uint32_t handle,Node &node)=0;
 // Any returned nonzero reference is owned, including error paths.
 virtual bool parent(uint32_t handle,uint32_t &owned)=0;
 virtual void release(uint32_t handle)=0;
};
class Owned {
 Reader &reader;uint32_t handle=0;
public:
 explicit Owned(Reader &r):reader(r){}
 Owned(const Owned &)=delete;Owned &operator=(const Owned &)=delete;
 ~Owned(){if(handle)reader.release(handle);}
 void adopt(uint32_t h){if(handle)reader.release(handle);handle=h;}
 uint32_t get()const{return handle;}
};
inline bool binding(Reader &reader,uint32_t port,Binding &result){
 result={};Node child;
 if(!port||!reader.read(port,child)||!child.registry||!child.isChild||!child.isAccelerator||!child.childVersion||!child.advertisedParentVersion||!child.validParentRegistry||!child.parentRegistry||child.parentRegistry==child.registry)return false;
 uint32_t next=0;bool got=reader.parent(port,next);Owned cursor(reader);cursor.adopt(next);
 if(!got||!cursor.get())return false;
 Node current;if(!reader.read(cursor.get(),current)||!eligibleParent(current)||current.registry!=child.parentRegistry)return false;
 const auto memory=current.memory;const uint64_t parent=current.registry;uint64_t seen[17]={child.registry};size_t count=1;
 for(unsigned depth=0;depth<16;++depth){
  if(!current.registry)return false;
  for(size_t i=0;i<count;++i)if(seen[i]==current.registry)return false;
  seen[count++]=current.registry;
  if(current.isPCI){
   if(!current.validPCI||current.vendor!=0x10de||current.device!=0x2520||current.subvendor!=0x1043||current.subdevice!=0x104c)return false;
   result.childRegistry=child.registry;result.parentGeneration=parent;result.memory=memory;return true;
  }
  if(depth==15)return false;
  next=0;got=reader.parent(cursor.get(),next);cursor.adopt(next);
  if(!got||!cursor.get()||!reader.read(cursor.get(),current))return false;
 }
 return false;
}
}
