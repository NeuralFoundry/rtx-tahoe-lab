#pragma once
#include "RTXAcceleratorIdentity103.hpp"
namespace RTXApplication209 {
constexpr uint64_t ABI=209,AllowedUID=501,OwnedProtocol=208,ReadyPhase=3;
struct Ready {
 uint64_t abi=0,protocol=0,allowedUID=0,rootPID=0,child=0,generation=0,epoch=0,publicationSession=0;
 uint64_t rootABI=0,hostABI=0,dataABI=0,dispatchABI=0,publicationABI=0;
 bool bootMatches=false,catalogMatches=false,memoryMatches=false;
};
struct Client {uint64_t pid=0,uid=0;};
struct Peer {
 uint64_t phase=0,generation=0,session=0,completed=0,nativeSerial=0,serverPID=0,processID=0,uid=0,exchanges=0;
};
struct Plan {RTXAccelerator103::Binding binding;Client client;uint64_t rootPID=0;};
inline bool identity(const RTXAccelerator103::Binding&b){
 return b.childRegistry&&b.parentGeneration&&b.childRegistry!=b.parentGeneration&&b.epoch&&b.session&&
  b.memory.generation==b.parentGeneration&&RTXMemory107::valid(b.memory);
}
inline bool same(const RTXAccelerator103::Binding&a,const RTXAccelerator103::Binding&b){
 // All valid memory-record fields except generation are fixed by valid().
 return identity(a)&&identity(b)&&a.childRegistry==b.childRegistry&&a.parentGeneration==b.parentGeneration&&a.epoch==b.epoch&&a.session==b.session;
}
inline bool prepare(const RTXAccelerator103::Binding&b,const Ready&r,const Client&c,Plan&out){
 out={};
 if(!identity(b)||!c.pid||c.pid>0x7fffffff||c.uid!=AllowedUID||r.abi!=ABI||r.protocol!=OwnedProtocol||r.allowedUID!=c.uid||
  !r.rootPID||r.rootPID>0x7fffffff||r.rootPID==c.pid||r.child!=b.childRegistry||r.generation!=b.parentGeneration||r.epoch!=b.epoch||r.publicationSession!=b.session||
  r.rootABI!=RTXRootReady200::RootABI||r.hostABI!=RTXRootReady200::HostABI||r.dataABI!=RTXRootReady200::DataABI||r.dispatchABI!=RTXRootReady200::DispatchABI||r.publicationABI!=RTXPublication200::ABI||
  !r.bootMatches||!r.catalogMatches||!r.memoryMatches)return false;
 out.binding=b;out.client=c;out.rootPID=r.rootPID;return true;
}
inline bool connect(const Plan&p,const Peer&peer,const Client&current,const RTXAccelerator103::Binding&now,bool sameReadinessRecord){
 if(!same(p.binding,now)||!sameReadinessRecord||!p.client.pid||p.client.uid!=AllowedUID||!p.rootPID||p.rootPID==p.client.pid||
  current.pid!=p.client.pid||current.uid!=p.client.uid)return false;
 // Publication session identifies the controller's ownership of the child.
 // Hello session identifies this individual connection. They are independent.
 // Native serial is the shared owner's current floor, which may be nonzero
 // for a new application; local completed remains zero after its Hello.
 return peer.phase==ReadyPhase&&peer.generation==p.binding.parentGeneration&&peer.session&&peer.completed==0&&
  peer.serverPID==p.rootPID&&peer.processID==current.pid&&peer.uid==current.uid&&peer.exchanges==1;
}
}
