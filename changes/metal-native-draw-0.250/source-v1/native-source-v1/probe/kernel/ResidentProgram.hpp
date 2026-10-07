#pragma once
#include "LibraryUpload.hpp"

// Native-owner storage, initialized before firmware and kept until reset.
// One resident code page can be replaced between completed jobs. This is not
// a growable GPU heap; caller-supplied physical addresses are never accepted.
namespace RtxResident058 {
namespace P=RtxProgram033;namespace U=RtxLibraryUpload036;
constexpr unsigned PayloadBytes=4608,HeaderBytes=128,InfoBytes=256;
constexpr uint64_t Magic=UINT64_C(0x5254585253443538),InfoMagic=UINT64_C(0x5254585253493538);
enum class Phase:unsigned {Cold,Idle,Uploading,Sealed,Applying,Retained};
enum class Error:unsigned {None,Identity,State,Shape,Stale,Order,Digest,Library,Runtime};
struct Scope {uint64_t generation=0,client=0,completed=0;bool ready=false;};
struct ApplyResult {
 bool attempted=false,windowAttempted=false,saved=false,exposed=false,restoreAttempted=false,restored=false,passed=false;
 unsigned windowBefore=0,windowAfter=0,reads=0,writes=0;uint64_t started=0,elapsed=0;
};
class State {
 uint8_t active_[PayloadBytes]={},pending_[PayloadBytes]={},header_[HeaderBytes]={},digest_[32]={};
 uint64_t generation_=0,client_=0,epoch_=0,completed_=0;unsigned written_=0;
 Phase phase_=Phase::Cold;Error error_=Error::None;ApplyResult result_;
 bool owner(const Scope &s)const{return s.generation&&s.generation==generation_&&s.client&&s.client==client_;}
 bool scope(const Scope &s)const{return owner(s)&&s.ready&&s.completed==completed_;}
public:
 State()=default;State(const State &)=delete;State &operator=(const State &)=delete;
 Phase phase()const{return phase_;}uint64_t epoch()const{return epoch_;}uint64_t generation()const{return generation_;}
 const uint8_t *library()const{return active_;}const uint8_t *code()const{return active_+512;}
 const uint8_t *candidateLibrary()const{return pending_;}const uint8_t *candidateCode()const{return pending_+512;}
 unsigned written()const{return written_;}const ApplyResult &result()const{return result_;}
 ApplyResult &applyingResult(){return result_;}
 bool seed(uint64_t generation,uint64_t client,const uint8_t *library,const uint8_t *code){
  if(phase_!=Phase::Cold||!generation||!client||!P::separate(library,512,this,sizeof(*this))||!P::separate(code,4096,this,sizeof(*this)))return false;
  P::Library parsed;if(!P::decode(library,512,code,4096,parsed)||!U::profile(parsed))return false;
  U::copy(active_,library,512);U::copy(active_+512,code,4096);generation_=generation;client_=client;epoch_=1;phase_=Phase::Idle;return true;
 }
 Error begin(const Scope &s,const uint8_t *header,size_t n){
  if(!owner(s))return Error::Identity;
  if(!s.ready||(phase_!=Phase::Idle&&phase_!=Phase::Uploading&&phase_!=Phase::Sealed))return Error::State;
  if(n!=HeaderBytes||!P::separate(header,n,this,sizeof(*this))||!P::separate(&s,sizeof(s),this,sizeof(*this)))return Error::Shape;
  if(P::get64(header)!=Magic||P::get32(header+8)!=1||P::get32(header+12)!=HeaderBytes||P::get64(header+16)!=generation_||
     P::get32(header+40)!=PayloadBytes||P::get32(header+44)!=0x86||!P::zero(header,80,HeaderBytes))return Error::Shape;
  if(epoch_==UINT64_MAX||P::get64(header+24)!=epoch_||P::get64(header+32)!=s.completed)return Error::Stale;
  // Restarting a CPU-only upload is allowed. No GPU work or borrowed storage
  // has been touched; malformed input never erases earlier evidence.
  U::copy(header_,header,HeaderBytes);for(auto &v:pending_)v=0;for(auto &v:digest_)v=0;
  completed_=s.completed;written_=0;error_=Error::None;result_={};phase_=Phase::Uploading;return Error::None;
 }
 Error append(const Scope &s,uint64_t offset,const uint8_t *data,size_t n){
  if(!owner(s))return Error::Identity;if(phase_!=Phase::Uploading)return Error::State;if(!scope(s))return Error::Stale;
  if(offset!=written_)return Error::Order;const unsigned remaining=PayloadBytes-written_,want=remaining<1024?remaining:1024;
  if(!want||n!=want||!P::separate(data,n,this,sizeof(*this)))return Error::Shape;
  U::copy(pending_+written_,data,unsigned(n));written_+=unsigned(n);return Error::None;
 }
 Error seal(const Scope &s){
  if(!owner(s))return Error::Identity;if(phase_!=Phase::Uploading||written_!=PayloadBytes)return Error::State;if(!scope(s))return Error::Stale;
  GSPDigest::SHA256 hash;hash.update(pending_,PayloadBytes);hash.finish(digest_);
  if(!U::equal(digest_,header_+48,32)){error_=Error::Digest;return error_;}
  P::Library parsed;if(!P::decode(pending_,512,pending_+512,4096,parsed)||!U::profile(parsed)){error_=Error::Library;return error_;}
  phase_=Phase::Sealed;return Error::None;
 }
 bool enter(const Scope &s){if(phase_!=Phase::Sealed||!scope(s)||epoch_==UINT64_MAX)return false;phase_=Phase::Applying;result_={};result_.attempted=true;return true;}
 bool commit(const Scope &s){
  if(phase_!=Phase::Applying||!scope(s)||!result_.exposed||!result_.restored||result_.writes!=1||epoch_==UINT64_MAX)return false;
  U::copy(active_,pending_,PayloadBytes);++epoch_;result_.passed=true;phase_=Phase::Idle;return true;
 }
 void retain(){if(phase_!=Phase::Cold){phase_=Phase::Retained;error_=Error::Runtime;}}
 bool ownsSnapshot(const Scope &s,uint64_t epoch)const{return owner(s)&&epoch&&epoch==epoch_&&phase_!=Phase::Cold;}
 bool accepts(const Scope &s,uint64_t epoch)const{return ownsSnapshot(s,epoch)&&s.ready&&phase_!=Phase::Applying&&phase_!=Phase::Retained;}
 bool info(uint8_t *out,size_t n)const{
  if(n!=InfoBytes||!P::separate(out,n,this,sizeof(*this)))return false;for(unsigned i=0;i<InfoBytes;++i)out[i]=0;
  const uint64_t words[]={InfoMagic,1,generation_,epoch_,unsigned(phase_),unsigned(error_),written_,completed_,
   result_.attempted,result_.windowAttempted,result_.saved,result_.exposed,result_.restored,result_.passed,
   result_.windowBefore,result_.windowAfter,result_.reads,result_.writes,result_.started,result_.elapsed};
  for(unsigned i=0;i<20;++i)P::Q::put64(out+i*8,words[i]);U::copy(out+160,header_+48,32);U::copy(out+192,digest_,32);P::Q::put64(out+224,result_.restoreAttempted);return true;
 }
};
}
