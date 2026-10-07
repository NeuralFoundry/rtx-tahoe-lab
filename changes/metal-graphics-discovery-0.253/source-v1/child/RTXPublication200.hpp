#pragma once
#include <stddef.h>
#include <stdint.h>
#include "RootReadiness200.hpp"

namespace RTXPublication200 {
constexpr size_t Bytes=64;
constexpr uint8_t ABI=3;
constexpr uint64_t MaxIdentity=0x7fffffffffffffffULL;
constexpr char RequestProperty[]="RTXMetalPublicationRequest253";
constexpr char PluginName[]="RTXMetalGraphics253-normal";
constexpr char PluginClass[]="RTXMetalApplicationDevice209";
enum class Operation:uint8_t {Publish=1,Withdraw=2};
enum class Phase:uint32_t {Cold,Publishing,Published,Failed,Withdrawn,Stopped};
enum class Error:uint32_t {Ok,Privilege,Encoding,Identity,NotReady,Busy,Stopped};
struct Request {uint64_t child=0,parent=0,epoch=0,session=0;Operation operation=Operation::Publish;};
struct Observation {
 uint64_t child=0,parent=0,epoch=0;
 bool eligible=false,providerOpen=false,programReady=false,hostFence=false;
 RTXRootReady200::State root;
};
inline bool valid(const Request&r){
 // The CFNumber reader intentionally accepts nonnegative signed-64 integers.
 return r.child&&r.parent&&r.child!=r.parent&&r.epoch&&r.session&&
  r.child<=MaxIdentity&&r.parent<=MaxIdentity&&r.epoch<=MaxIdentity&&r.session<=MaxIdentity&&
  (r.operation==Operation::Publish||r.operation==Operation::Withdraw);
}
inline uint64_t word(const uint8_t*p){uint64_t v=0;for(unsigned i=0;i<8;++i)v|=uint64_t(p[i])<<(8*i);return v;}
inline void put(uint8_t*p,uint64_t v){for(unsigned i=0;i<8;++i)p[i]=uint8_t(v>>(8*i));}
inline bool decode(const void*raw,size_t bytes,Request&result){
 result={};if(!raw||bytes!=Bytes)return false;const auto*p=static_cast<const uint8_t*>(raw);
 if(p[0]!='R'||p[1]!='T'||p[2]!='X'||p[3]!='P'||p[4]!=ABI||p[5]||p[7])return false;
 for(size_t i=40;i<Bytes;++i)if(p[i])return false;
 Request r;r.child=word(p+8);r.parent=word(p+16);r.epoch=word(p+24);r.session=word(p+32);r.operation=Operation(p[6]);
 if(!valid(r))return false;result=r;return true;
}
inline bool encode(const Request&r,void*raw,size_t bytes){
 if(!raw||bytes!=Bytes||!valid(r))return false;auto*p=static_cast<uint8_t*>(raw);
 for(size_t i=0;i<Bytes;++i)p[i]=0;
 p[0]='R';p[1]='T';p[2]='X';p[3]='P';p[4]=ABI;p[6]=uint8_t(r.operation);
 put(p+8,r.child);put(p+16,r.parent);put(p+24,r.epoch);put(p+32,r.session);return true;
}
inline bool sameIdentity(const Request&a,const Request&b){return a.child==b.child&&a.parent==b.parent&&a.epoch==b.epoch&&a.session==b.session;}
inline bool ready(const Request&r,const Observation&o){
 return r.child==o.child&&r.parent==o.parent&&o.eligible&&o.providerOpen&&o.programReady&&o.hostFence&&
  o.epoch&&r.epoch==o.epoch&&RTXRootReady200::valid(o.root);
}
// The caller holds one sleeping mutex for every method AND its Sink calls.
// Sink writes only preallocated property values through IORegistryEntry's base
// property methods. No registration, provider calls or callbacks under this
// mutex. Preparation and fresh provider observations happen outside it.
// This prevents a stopped/withdrawn publication from being rewritten by a
// previously admitted writer. The session is the root controller's attestation;
// the kernel cannot establish XPC liveness from this number alone.
class Controller {
 Phase phase_=Phase::Cold;Request admitted_;
public:
 Phase phase()const{return phase_;}
 Error begin(bool privileged,const Request&r,const Observation&o){
  if(!privileged)return Error::Privilege;
  if(!valid(r)||r.operation!=Operation::Publish)return Error::Encoding;
  if(phase_==Phase::Stopped)return Error::Stopped;
  if(phase_!=Phase::Cold)return Error::Busy;
  if(r.child!=o.child||r.parent!=o.parent||!o.eligible)return Error::Identity;
  if(!ready(r,o))return Error::NotReady;
  admitted_=r;phase_=Phase::Publishing;return Error::Ok;
 }
 template<class Sink>bool commit(Sink&sink,bool prepared,bool active,const Observation&now){
  if(phase_!=Phase::Publishing)return false;
  if(!prepared||!active||!ready(admitted_,now)){phase_=Phase::Failed;sink.hide();return false;}
  if(!sink.publish(admitted_)){phase_=Phase::Failed;sink.hide();return false;}
  phase_=Phase::Published;return true;
 }
 template<class Sink>Error withdraw(Sink&sink,bool privileged,const Request&r){
  if(!privileged)return Error::Privilege;
  if(!valid(r)||r.operation!=Operation::Withdraw)return Error::Encoding;
  if(phase_==Phase::Stopped)return Error::Stopped;
  if(phase_==Phase::Cold)return Error::NotReady;
  if(!sameIdentity(r,admitted_))return Error::Identity;
  if(phase_==Phase::Publishing)return Error::Busy;
  // Idempotent only for the exact admitted identity. Never rearms this child.
  if(phase_==Phase::Withdrawn)return Error::Ok;
  sink.hide();phase_=Phase::Withdrawn;return Error::Ok;
 }
 template<class Sink>void stop(Sink&sink){phase_=Phase::Stopped;sink.hide();}
};
}
