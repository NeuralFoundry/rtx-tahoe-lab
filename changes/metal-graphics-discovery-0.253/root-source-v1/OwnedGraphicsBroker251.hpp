#pragma once
#include "RTXDrawTransfer248.hpp"
#include "RTXGraphicsCatalog247.hpp"
#include <utility>
// Serialized graphics owner protocol. No addresses, code uploads, or GPU
// commands cross this boundary. Only the reviewed caller-triangle subset does.
namespace RTXOwnedGraphicsBroker251 {
namespace D=RTXDrawTransfer248;namespace C=RTXGraphicsCatalog247;
using Bytes=D::Bytes;using Digest=D::Digest;
constexpr uint64_t Magic=0x5254584742523235ULL;
constexpr size_t Header=96,MaxFrame=Header+D::MaxPacket;
enum class Op:uint32_t{Hello=1,Draw=2,Close=3};
enum class Status:uint32_t{OK=0,NotReady=1,BadRequest=2,Denied=3,Closed=4,Backend=5,Capacity=6,Stale=7};
struct Identity{Op op=Op::Hello;Status status=Status::OK;bool reply=false;uint64_t session=0,sequence=0,generation=0,nativeSerial=0;};
struct Frame{Bytes bytes;size_t size()const{return bytes.size();}};
inline Digest digest(){Digest v{};std::memcpy(v.data(),C::Image+16,32);return v;}
inline bool read(const void*input,size_t n,Identity&out){
 if(!input||n<Header||n>MaxFrame)return false;const auto*p=static_cast<const uint8_t*>(input);
 if(D::u64(p)!=Magic||D::u32(p+8)!=251||D::u32(p+16)!=n||!D::zero(p+56,8)||std::memcmp(p+64,C::Image+16,32))return false;
 const auto kind=D::u32(p+12),op=kind&0x7fffffff,status=D::u32(p+20);if(op<1||op>3||status>7)return false;
 Identity v{Op(op),Status(status),bool(kind&0x80000000),D::u64(p+24),D::u64(p+32),D::u64(p+40),D::u64(p+48)};
 if(!v.reply&&(status||v.nativeSerial))return false;out=v;return true;
}
inline Frame write(Identity h,const void*body=nullptr,size_t n=0){
 Frame f;if(n>MaxFrame-Header||bool(body)!=bool(n))return f;f.bytes.resize(Header+n);auto*p=f.bytes.data();
 D::put64(p,Magic);D::put32(p+8,251);D::put32(p+12,uint32_t(h.op)|(h.reply?0x80000000:0));D::put32(p+16,uint32_t(f.size()));D::put32(p+20,uint32_t(h.status));
 D::put64(p+24,h.session);D::put64(p+32,h.sequence);D::put64(p+40,h.generation);D::put64(p+48,h.nativeSerial);std::memcpy(p+64,C::Image+16,32);
 if(n)std::memcpy(p+Header,body,n);return f;
}
inline bool decode(const Bytes&request,uint64_t generation,uint64_t serial,D::Plan&out){
 D::Plan p;if(!D::decode(request.data(),request.size(),generation,serial,digest(),p)||p.vertexCount!=3||p.width>64||p.height>64)return false;
 // Match native250's reviewed NDC subset, checked before any effective call.
 for(uint64_t at=p.vertexBegin;at<p.vertexEnd;at+=16)for(unsigned k=0;k<2;++k)
  if((D::u32(request.data()+D::Header+at+k*4)&0x7fffffff)>0x3f800000)return false;
 out=p;return true;
}
struct Peer{uint64_t session=0,sequence=0,serial=0;bool closed=false;};
class Core{
 bool ready_=false,failed_=false,claimAttempted_=false;uint64_t generation_=0,completed_=0,nextSession_=1,executions_=0;
 Frame reject(Peer&peer,Identity h,Status s){peer.closed=true;h.reply=true;h.status=s;h.nativeSerial=0;return write(h);}
 template<class Backend>void retire(Backend&b)noexcept{failed_=true;ready_=false;try{b.retire();}catch(...){}}
public:
 bool ready()const{return ready_&&!failed_;}bool failed()const{return failed_;}uint64_t executions()const{return executions_;}uint64_t completed()const{return completed_;}
 template<class Backend>void stop(Backend&b)noexcept{if(!failed_)retire(b);}
 template<class Backend>bool prepare(Backend&b)noexcept{
  if(failed_)return false;
  try{
   const uint64_t expected=b.generation();
   if(ready_){if(expected==generation_)return true;retire(b);return false;}
   if(claimAttempted_)return false;claimAttempted_=true;
   uint64_t generation=0,completed=0;
   if(!expected||!b.claim(generation,completed)||generation!=expected||b.generation()!=expected||completed){retire(b);return false;}
   generation_=generation;ready_=true;return true;
  }catch(...){retire(b);return false;}
 }
 template<class Backend>Frame receive(Peer&peer,const void*input,size_t n,bool authorized,Backend&backend){
  try{
   Identity h;if(!read(input,n,h)||h.reply)return reject(peer,{},Status::BadRequest);
   if(!authorized)return reject(peer,h,Status::Denied);if(peer.closed||failed_)return reject(peer,h,Status::Closed);
   if(h.op==Op::Hello){
    if(n!=Header||h.session||h.sequence!=1||peer.session||!h.generation)return reject(peer,h,Status::BadRequest);
    // Inspect the generation before claiming so a stale client cannot start IO.
    if(h.generation!=backend.generation())return reject(peer,h,Status::Stale);
    if(!prepare(backend))return reject(peer,h,Status::NotReady);
    if(h.generation!=generation_)return reject(peer,h,Status::Stale);if(!nextSession_)return reject(peer,h,Status::Capacity);
    peer.session=nextSession_++;peer.sequence=1;h.session=peer.session;h.nativeSerial=completed_;h.reply=true;return write(h,C::Image,C::Bytes);
   }
   if(!ready_||!peer.session||h.session!=peer.session||h.generation!=generation_||peer.sequence==UINT64_MAX||h.sequence!=peer.sequence+1)return reject(peer,h,Status::BadRequest);
   if(h.op==Op::Close){if(n!=Header)return reject(peer,h,Status::BadRequest);peer.closed=true;peer.sequence=h.sequence;h.reply=true;h.nativeSerial=completed_;return write(h);}
   if(peer.serial==UINT64_MAX||completed_==UINT64_MAX)return reject(peer,h,Status::Capacity);
   if(n<Header+D::Header)return reject(peer,h,Status::BadRequest);
   const auto*p=static_cast<const uint8_t*>(input);Bytes request(p+Header,p+n);D::Plan plan;
   if(!decode(request,generation_,peer.serial+1,plan))return reject(peer,h,Status::BadRequest);
   const uint64_t native=completed_+1,local=plan.serial;D::put64(request.data()+24,native);
   D::Plan nativePlan;if(!decode(request,generation_,native,nativePlan))return reject(peer,h,Status::BadRequest);
   peer.sequence=h.sequence;peer.serial=local;++executions_;Bytes result;uint64_t completion=0;
   if(!backend.execute(request,result,completion)||completion!=native||!D::readback(nativePlan,request,result.data(),result.size(),completion)){retire(backend);return reject(peer,h,Status::Backend);}
   completed_=completion;h.reply=true;h.nativeSerial=native;return write(h,result.data(),result.size());
  }catch(...){peer.closed=true;retire(backend);return {};}
 }
};
class Client{
public:enum class Phase{Invalid,Cold,Waiting,Ready,Closed,Failed};
private:
 Phase phase_=Phase::Invalid;uint64_t generation_=0,session_=0,sequence_=0,local_=0,native_=0;int64_t pid_=0;Identity pending_{};Bytes request_;D::Plan plan_{};
public:
 Client(const void*image,size_t n,uint64_t generation){if(generation&&C::valid(image,n)){generation_=generation;phase_=Phase::Cold;}}
 Phase phase()const{return phase_;}uint64_t generation()const{return generation_;}uint64_t session()const{return session_;}uint64_t completed()const{return local_;}uint64_t nativeSerial()const{return native_;}int64_t serverPID()const{return pid_;}
 void fail(){phase_=Phase::Failed;request_.clear();}
 bool hello(Frame&out){if(phase_!=Phase::Cold)return false;try{pending_={Op::Hello,Status::OK,false,0,1,generation_,0};auto f=write(pending_);out=std::move(f);sequence_=1;phase_=Phase::Waiting;return true;}catch(...){fail();return false;}}
 bool execute(const void*input,size_t n,const void*catalog,size_t catalogBytes,Frame&out){
  if(phase_!=Phase::Ready||local_==UINT64_MAX||sequence_==UINT64_MAX)return false;
  try{if(!input||n<D::Header||n>D::MaxPacket||!C::valid(catalog,catalogBytes)){fail();return false;}
   const auto*p=static_cast<const uint8_t*>(input);Bytes request(p,p+n);D::Plan plan;if(!decode(request,generation_,local_+1,plan)){fail();return false;}
   pending_={Op::Draw,Status::OK,false,session_,sequence_+1,generation_,0};auto f=write(pending_,request.data(),request.size());request_=std::move(request);plan_=plan;++sequence_;out=std::move(f);phase_=Phase::Waiting;return true;
  }catch(...){fail();return false;}
 }
 bool close(Frame&out){if(phase_!=Phase::Ready||sequence_==UINT64_MAX)return false;try{pending_={Op::Close,Status::OK,false,session_,sequence_+1,generation_,0};out=write(pending_);++sequence_;phase_=Phase::Waiting;return true;}catch(...){fail();return false;}}
 bool accept(const void*input,size_t n,uint32_t uid,int64_t pid,Bytes&out,uint64_t&completion){
  completion=0;Identity h;if(phase_!=Phase::Waiting||uid||pid<=0||(pid_&&pid!=pid_)||!read(input,n,h)||!h.reply||h.status!=Status::OK||h.op!=pending_.op||h.sequence!=pending_.sequence||h.generation!=generation_){fail();return false;}
  try{const auto*p=static_cast<const uint8_t*>(input);
   if(h.op==Op::Hello){if(n!=Header+C::Bytes||!h.session||!C::valid(p+Header,n-Header)){fail();return false;}session_=h.session;pid_=pid;native_=h.nativeSerial;phase_=Phase::Ready;return true;}
   if(h.session!=session_){fail();return false;}
   if(h.op==Op::Close){if(n!=Header||h.nativeSerial<native_){fail();return false;}native_=h.nativeSerial;phase_=Phase::Closed;return true;}
   if(h.nativeSerial<=native_||n!=Header+plan_.payloadBytes||!D::readback(plan_,request_,p+Header,n-Header,plan_.serial)){fail();return false;}
   Bytes result(p+Header,p+n);out.swap(result);local_=plan_.serial;native_=h.nativeSerial;completion=local_;request_.clear();phase_=Phase::Ready;return true;
  }catch(...){fail();return false;}
 }
};
}
