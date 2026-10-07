#pragma once
#include "RTXLibraryContainer.hpp"
#include "ReusableProgram.hpp"
#include <array>
#include <cstdint>
#include <cstring>

namespace RTXBroker040 {
namespace P=RtxProgram033;
constexpr uint64_t Magic=UINT64_C(0x5254584252503430);
constexpr unsigned Header=64,MaxFrame=Header+RTXLibrary036::Bytes;
constexpr uint32_t ReplyBit=0x80000000;
enum class Op:uint32_t {Hello=1,Execute=2,Close=3};
enum class Status:uint32_t {OK=0,NotReady=1,BadRequest=2,Denied=3,Closed=4,Backend=5,Capacity=6,Stale=7};
struct HeaderView {Op op=Op::Hello;Status status=Status::OK;bool reply=false;uint64_t session=0,sequence=0,generation=0,nativeSerial=0;};
struct Frame {std::array<uint8_t,MaxFrame> bytes{};size_t size=0;};
inline bool read(const uint8_t *b,size_t n,HeaderView &h){
 if(!b||n<Header||n>MaxFrame||P::get64(b)!=Magic||P::get32(b+8)!=1||P::get32(b+16)!=n||!P::zero(b,56,64))return false;
 uint32_t kind=P::get32(b+12),op=kind&~ReplyBit,status=P::get32(b+20);
 if(op<1||op>3||status>7)return false;
 HeaderView r{Op(op),Status(status),bool(kind&ReplyBit),P::get64(b+24),P::get64(b+32),P::get64(b+40),P::get64(b+48)};
 if(!r.reply&&(status||r.nativeSerial))return false;
 h=r;return true;
}
inline Frame write(HeaderView h,const void *body=nullptr,size_t n=0){
 Frame f;if(n>MaxFrame-Header||bool(body)!=bool(n))return f;f.size=Header+n;
 P::Q::put64(f.bytes.data(),Magic);P::Q::put32(f.bytes.data()+8,1);P::Q::put32(f.bytes.data()+12,uint32_t(h.op)|(h.reply?ReplyBit:0));
 P::Q::put32(f.bytes.data()+16,unsigned(f.size));P::Q::put32(f.bytes.data()+20,uint32_t(h.status));
 P::Q::put64(f.bytes.data()+24,h.session);P::Q::put64(f.bytes.data()+32,h.sequence);P::Q::put64(f.bytes.data()+40,h.generation);P::Q::put64(f.bytes.data()+48,h.nativeSerial);
 if(n)std::memcpy(f.bytes.data()+Header,body,n);return f;
}
struct Peer {uint64_t session=0,lastSequence=0,lastMetalSerial=0;bool closed=false;};
// One Core belongs to one native owner. The XPC adapter serializes every call.
// Backend.claim must return the already armed, exclusively claimed native owner;
// it must never accept firmware, file paths, selectors or executable bytes here.
class Core {
 bool ready_=false,failed_=false;uint64_t generation_=0,completed_=0,nextSession_=1,executions_=0;
 std::array<uint8_t,RTXLibrary036::Bytes> container_{};RTXLibrary036::Catalog catalog_{};
 Frame reject(Peer &p,const HeaderView &h,Status status){p.closed=true;HeaderView r=h;r.reply=true;r.status=status;r.nativeSerial=0;return write(r);}
public:
 bool ready()const{return ready_;}bool failed()const{return failed_;}uint64_t executions()const{return executions_;}uint64_t completed()const{return completed_;}
 template<class Backend>Frame receive(Peer &peer,const uint8_t *bytes,size_t length,bool authorized,Backend &backend){
  HeaderView h;
  if(!read(bytes,length,h)||h.reply)return reject(peer,HeaderView{},Status::BadRequest);
  if(!authorized)return reject(peer,h,Status::Denied);
  if(peer.closed||failed_)return reject(peer,h,Status::Closed);
  if(h.op==Op::Hello){
   if(length!=Header||h.session||h.sequence!=1||peer.session)return reject(peer,h,Status::BadRequest);
   if(!ready_){
    uint64_t generation=0,completed=0;std::array<uint8_t,RTXLibrary036::Bytes> image{};
    if(!backend.claim(image,generation,completed))return reject(peer,h,Status::NotReady);
    RTXLibrary036::Catalog parsed;
    if(!generation||completed==UINT64_MAX||!RTXLibrary036::decode(image.data(),image.size(),parsed)){failed_=true;return reject(peer,h,Status::Backend);}
    container_=image;catalog_=parsed;generation_=generation;completed_=completed;ready_=true;
   }
   if(h.generation&&h.generation!=generation_)return reject(peer,h,Status::Stale);
   if(!nextSession_)return reject(peer,h,Status::Capacity);
   peer.session=nextSession_++;peer.lastSequence=1;h.session=peer.session;h.generation=generation_;h.reply=true;
   return write(h,container_.data(),container_.size());
  }
  if(!ready_||!peer.session||h.session!=peer.session||h.generation!=generation_||peer.lastSequence==UINT64_MAX||h.sequence!=peer.lastSequence+1)
   return reject(peer,h,Status::BadRequest);
  if(h.op==Op::Close){if(length!=Header)return reject(peer,h,Status::BadRequest);peer.closed=true;peer.lastSequence=h.sequence;h.reply=true;return write(h);}
  RtxReusable035::Request request;
  if(length!=Header+RtxReusable035::WireBytes||!RtxReusable035::decode(bytes+Header,length-Header,catalog_.library,request)||
     request.generation!=generation_||peer.lastMetalSerial==UINT64_MAX||request.serial!=peer.lastMetalSerial+1)return reject(peer,h,Status::BadRequest);
  if(completed_==UINT64_MAX)return reject(peer,h,Status::Capacity);
  const uint64_t nativeSerial=completed_+1,clientSerial=request.serial;request.serial=nativeSerial;
  std::array<uint8_t,RtxReusable035::WireBytes> native{};
  if(!RtxReusable035::encode(request,catalog_.library,native.data(),native.size()))return reject(peer,h,Status::BadRequest);
  // Consume request identity before entering a possibly uncertain GPU call.
  peer.lastSequence=h.sequence;peer.lastMetalSerial=clientSerial;++executions_;
  std::array<uint8_t,2048> result{};uint64_t completion=0;
  const bool ok=backend.execute(native,result,completion);h.reply=true;h.nativeSerial=nativeSerial;
  if(!ok||completion!=nativeSerial){failed_=true;peer.closed=true;h.status=Status::Backend;return write(h);}
  completed_=completion;return write(h,result.data(),result.size());
 }
};
}
