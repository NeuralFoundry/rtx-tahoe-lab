#pragma once
#include "ProgramRequest.hpp"
namespace RtxProgramSession033 {
namespace P=RtxProgram033;namespace R=RtxProgramRequest033;
enum class Phase:unsigned{Empty,Ready,Accepted,Exposed,Submitted,Exhausted,Retained};
enum class Error:unsigned{Ok,Identity,State,Shape,Order};
struct Bootstrap {uint64_t generation=0,client=0;bool owner=false,firmware=false,host=false,library=false,storage=false,windowRestored=false;};
struct Completion {uint64_t generation=0,id=0;unsigned get=0,put=0,marker=0;bool owner=false,capture=false,windowRestored=false;};
// Preallocate this service-owned object before firmware. The snapshot avoids
// reading caller bytes twice without putting multiple 2KiB records on stack.
class Session {
 Phase phase_=Phase::Empty;uint64_t generation_=0,client_=0;unsigned completed_=0;
 R::Request active_;uint8_t snapshot_[R::WireBytes]={};
public:
 Session()=default;Session(const Session &)=delete;Session &operator=(const Session &)=delete;
 Session(Session &&)=delete;Session &operator=(Session &&)=delete;
 Phase phase()const{return phase_;}unsigned completed()const{return completed_;}
 uint64_t generation()const{return generation_;}uint64_t client()const{return client_;}
 const R::Request &active()const{return active_;}
 bool open(const Bootstrap &e){
  if(phase_!=Phase::Empty||!e.generation||!e.client||!e.owner||!e.firmware||!e.host||!e.library||!e.storage||!e.windowRestored)return false;
  generation_=e.generation;client_=e.client;phase_=Phase::Ready;return true;
 }
 Error accept(uint64_t caller,const uint8_t *wire,size_t bytes,const P::Library &lib){
  if(phase_==Phase::Empty||caller!=client_)return Error::Identity;
  if(phase_!=Phase::Ready)return Error::State;
  if(bytes!=R::WireBytes||!P::separate(wire,bytes,this,sizeof(*this))||!P::separate(&lib,sizeof(lib),this,sizeof(*this)))return Error::Shape;
  for(unsigned i=0;i<R::WireBytes;++i)snapshot_[i]=wire[i];
  // Identity/order are checked before decoding into the protected active record.
  if(P::get64(snapshot_+16)!=generation_)return Error::Identity;
  if(P::get64(snapshot_+24)!=uint64_t(completed_)+1)return Error::Order;
  if(!R::decode(snapshot_,sizeof(snapshot_),lib,active_))return Error::Shape;
  phase_=Phase::Accepted;return Error::Ok;
 }
 bool beginMutation(uint64_t caller){if(caller!=client_||phase_!=Phase::Accepted)return false;phase_=Phase::Exposed;return true;}
 bool submitted(uint64_t caller){if(caller!=client_||phase_!=Phase::Exposed)return false;phase_=Phase::Submitted;return true;}
 bool finish(uint64_t caller,const Completion &e){
  if(caller!=client_||phase_!=Phase::Submitted)return false;
  if(e.generation!=generation_||e.id!=active_.id||e.get!=completed_+2||e.put!=completed_+2||e.marker!=P::completion(completed_)||
     !e.owner||!e.capture||!e.windowRestored){phase_=Phase::Retained;return false;}
  ++completed_;phase_=completed_==P::Slots?Phase::Exhausted:Phase::Ready;return true;
 }
 bool close(uint64_t caller){if(phase_==Phase::Empty||caller!=client_)return false;phase_=Phase::Retained;return true;}
 void ownershipLost(){if(phase_!=Phase::Empty)phase_=Phase::Retained;}
};
}
