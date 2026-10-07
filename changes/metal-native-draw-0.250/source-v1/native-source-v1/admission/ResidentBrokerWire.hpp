#pragma once
#include "RTXLibraryContainer.hpp"
#include "ReusableProgram.hpp"
#include <array>
#include <cstring>

// New wire identity: old immutable-catalog clients cannot accidentally use this
// endpoint. One serialized request carries owned code AND its dependent job.
namespace RTXResidentBroker058 {
namespace P=RtxProgram033;namespace N=RtxReusable035;
constexpr uint64_t Magic=UINT64_C(0x5254584252503538);
constexpr unsigned Header=64,Payload=4608,Request=2112,MaxFrame=Header+Payload+Request,HelloBytes=Header+RTXLibrary036::Bytes;
enum class Op:uint32_t {Hello=1,Execute=2,Close=3};
enum class Status:uint32_t {OK=0,NotReady=1,BadRequest=2,Denied=3,Closed=4,Backend=5,Capacity=6,Stale=7};
struct Identity {Op op=Op::Hello;Status status=Status::OK;bool reply=false;uint64_t session=0,sequence=0,generation=0,nativeSerial=0,epoch=0;};
struct Frame {std::array<uint8_t,MaxFrame> bytes{};size_t size=0;};
inline bool read(const uint8_t *p,size_t n,Identity &out){
 if(!p||n<Header||n>MaxFrame||P::get64(p)!=Magic||P::get32(p+8)!=1||P::get32(p+16)!=n)return false;
 auto op=P::get32(p+12),status=P::get32(p+20);bool reply=bool(op&0x80000000);op&=0x7fffffff;
 if(op<1||op>3||status>7)return false;
 Identity h{Op(op),Status(status),reply,P::get64(p+24),P::get64(p+32),P::get64(p+40),P::get64(p+48),P::get64(p+56)};
 if(!reply&&(status||h.nativeSerial||h.epoch))return false;out=h;return true;
}
inline Frame write(Identity h,const void *body=nullptr,size_t n=0){
 Frame out;if(n>MaxFrame-Header||bool(body)!=bool(n))return out;out.size=Header+n;auto *p=out.bytes.data();
 P::Q::put64(p,Magic);P::Q::put32(p+8,1);P::Q::put32(p+12,uint32_t(h.op)|(h.reply?0x80000000:0));P::Q::put32(p+16,unsigned(out.size));P::Q::put32(p+20,uint32_t(h.status));
 P::Q::put64(p+24,h.session);P::Q::put64(p+32,h.sequence);P::Q::put64(p+40,h.generation);P::Q::put64(p+48,h.nativeSerial);P::Q::put64(p+56,h.epoch);if(n)std::memcpy(p+Header,body,n);return out;
}
inline bool library(const uint8_t *payload,P::Library &lib){
 if(!payload||!P::decode(payload,512,payload+512,4096,lib))return false;
 for(unsigned i=0;i<lib.count;++i)if(lib.programs[i].localX>64)return false;return true;
}
struct Peer {uint64_t session=0,sequence=0,serial=0;bool closed=false;};
// Caller serializes receive for this entire Core, including other peers. The
// root backend must independently admit compiler provenance for every NEW code
// payload before replacement. Metadata decoding alone does not admit SASS.
class Core {
 bool ready_=false,failed_=false;uint64_t generation_=0,completed_=0,epoch_=1,nextSession_=1,executions_=0,replacements_=0;
 std::array<uint8_t,RTXLibrary036::Bytes> initial_{};std::array<uint8_t,Payload> active_{};
 Frame reject(Peer &peer,Identity h,Status status){peer.closed=true;h.reply=true;h.status=status;h.nativeSerial=h.epoch=0;return write(h);}
public:
 bool ready()const{return ready_&&!failed_;}bool failed()const{return failed_;}uint64_t completed()const{return completed_;}uint64_t epoch()const{return epoch_;}
 uint64_t executions()const{return executions_;}uint64_t replacements()const{return replacements_;}
 template<class Backend>Frame receive(Peer &peer,const uint8_t *bytes,size_t n,bool authorized,Backend &backend){
  Identity h;if(!read(bytes,n,h)||h.reply)return reject(peer,{},Status::BadRequest);
  if(!authorized)return reject(peer,h,Status::Denied);
  if(peer.closed||failed_)return reject(peer,h,Status::Closed);
  if(h.op==Op::Hello){
   if(n!=Header||h.session||h.sequence!=1||peer.session)return reject(peer,h,Status::BadRequest);
   if(!ready_){
    uint64_t generation=0,completed=0,epoch=0;decltype(initial_) image{};RTXLibrary036::Catalog parsed;
    if(!backend.claim(image,generation,completed,epoch))return reject(peer,h,Status::NotReady);
    if(!generation||completed||epoch!=1||!RTXLibrary036::decode(image.data(),image.size(),parsed)){failed_=true;return reject(peer,h,Status::Backend);}
    initial_=image;std::memcpy(active_.data(),image.data()+640,Payload);generation_=generation;ready_=true;
   }
   if(h.generation&&h.generation!=generation_)return reject(peer,h,Status::Stale);
   if(!nextSession_)return reject(peer,h,Status::Capacity);
   peer.session=nextSession_++;peer.sequence=1;h.reply=true;h.session=peer.session;h.generation=generation_;h.epoch=epoch_;
   return write(h,initial_.data(),initial_.size());
  }
  if(!ready_||!peer.session||h.session!=peer.session||h.generation!=generation_||peer.sequence==UINT64_MAX||h.sequence!=peer.sequence+1)return reject(peer,h,Status::BadRequest);
  if(h.op==Op::Close){if(n!=Header)return reject(peer,h,Status::BadRequest);peer.closed=true;peer.sequence=h.sequence;h.reply=true;h.epoch=epoch_;return write(h);}
  if(n!=MaxFrame)return reject(peer,h,Status::BadRequest);
  // Own one immutable snapshot across admission, replacement and encoding.
  std::array<uint8_t,Payload> candidate{};std::memcpy(candidate.data(),bytes+Header,Payload);P::Library lib;N::Request request;
  if(!library(candidate.data(),lib)||!N::decode(bytes+Header+Payload,Request,lib,request)||request.generation!=generation_||peer.serial==UINT64_MAX||request.serial!=peer.serial+1)return reject(peer,h,Status::BadRequest);
  const bool different=candidate!=active_;
  if(completed_==UINT64_MAX||(different&&epoch_==UINT64_MAX))return reject(peer,h,Status::Capacity);
  if(different&&!backend.admit(candidate))return reject(peer,h,Status::Denied);
  const uint64_t local=request.serial,native=completed_+1;request.serial=native;std::array<uint8_t,Request> wire{};
  if(!N::encode(request,lib,wire.data(),wire.size()))return reject(peer,h,Status::BadRequest);
  peer.sequence=h.sequence;peer.serial=local;h.reply=true;
  if(different){
   ++replacements_;uint64_t next=0;const bool replaced=backend.replace(candidate,epoch_,completed_,next);
   if(!replaced||next!=epoch_+1){failed_=true;return reject(peer,h,Status::Backend);}
   active_=candidate;epoch_=next;
  }
  ++executions_;std::array<uint8_t,2048> result{};uint64_t completion=0;h.nativeSerial=native;h.epoch=epoch_;
  if(!backend.execute(wire,active_,epoch_,result,completion)||completion!=native){failed_=true;peer.closed=true;h.status=Status::Backend;return write(h);}
  completed_=completion;return write(h,result.data(),result.size());
 }
};
}
