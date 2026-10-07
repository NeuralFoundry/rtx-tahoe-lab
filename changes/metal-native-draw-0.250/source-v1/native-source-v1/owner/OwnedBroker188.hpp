#pragma once
#include "OwnedBatch187.hpp"
// Process boundary for whole owned buffers. Core.receive is serialized across
// all peers. Only the root backend admits the immutable compiled catalog.
namespace RTXOwnedBroker188 {
namespace P=RtxProgram164;namespace B=RTXBatch187;using Bytes=B::Bytes;
constexpr uint64_t Magic=0x525458424b523138ULL;
constexpr size_t Header=64,MaxFrame=Header+B::Header+B::MaxPayload;
enum class Op:uint32_t {Hello=1,Execute=2,Close=3};
enum class Status:uint32_t {OK=0,NotReady=1,BadRequest=2,Denied=3,Closed=4,Backend=5,Capacity=6,Stale=7};
struct Identity {Op op=Op::Hello;Status status=Status::OK;bool reply=false;uint64_t session=0,sequence=0,generation=0,nativeSerial=0;};
struct Frame {Bytes bytes;size_t size()const{return bytes.size();}};
inline bool read(const uint8_t*p,size_t n,Identity&out){
 if(!p||n<Header||n>MaxFrame||P::get64(p)!=Magic||P::get32(p+8)!=188||P::get32(p+16)!=n||!P::zero(p,56,64))return false;
 const auto kind=P::get32(p+12),op=kind&0x7fffffff,status=P::get32(p+20);if(op<1||op>3||status>7)return false;
 Identity next{Op(op),Status(status),bool(kind&0x80000000),P::get64(p+24),P::get64(p+32),P::get64(p+40),P::get64(p+48)};
 if(!next.reply&&(status||next.nativeSerial))return false;out=next;return true;
}
inline Frame write(Identity h,const void*body=nullptr,size_t n=0){
 Frame f;if(n>MaxFrame-Header||bool(body)!=bool(n))return f;f.bytes.resize(Header+n);auto*p=f.bytes.data();
 P::Q::put64(p,Magic);P::Q::put32(p+8,188);P::Q::put32(p+12,uint32_t(h.op)|(h.reply?0x80000000:0));P::Q::put32(p+16,uint32_t(f.size()));P::Q::put32(p+20,uint32_t(h.status));
 P::Q::put64(p+24,h.session);P::Q::put64(p+32,h.sequence);P::Q::put64(p+40,h.generation);P::Q::put64(p+48,h.nativeSerial);if(n)std::memcpy(p+Header,body,n);return f;
}
struct Peer {uint64_t session=0,sequence=0,serial=0;bool closed=false;};
class Core {
 bool ready_=false,failed_=false,claimAttempted_=false;uint64_t generation_=0,completed_=0,nextSession_=1,executions_=0;
 std::array<uint8_t,RTXCatalog187::Bytes>container_{};RTXCatalog187::Catalog catalog_{};
 Frame reject(Peer&peer,Identity h,Status status){peer.closed=true;h.reply=true;h.status=status;h.nativeSerial=0;return write(h);}
 template<class Backend>void retire(Backend&backend)noexcept{failed_=true;ready_=false;try{backend.retire();}catch(...){}}
public:
 bool ready()const{return ready_&&!failed_;}bool failed()const{return failed_;}uint64_t executions()const{return executions_;}uint64_t completed()const{return completed_;}
 template<class Backend>void stop(Backend&backend)noexcept{if(!failed_)retire(backend);}
 template<class Backend>Frame receive(Peer&peer,const uint8_t*bytes,size_t n,bool authorized,Backend&backend){
  try{
   Identity h;if(!read(bytes,n,h)||h.reply)return reject(peer,{},Status::BadRequest);
   if(!authorized)return reject(peer,h,Status::Denied);if(peer.closed||failed_)return reject(peer,h,Status::Closed);
   if(h.op==Op::Hello){
    if(n!=Header||h.session||h.sequence!=1||peer.session||!h.generation)return reject(peer,h,Status::BadRequest);
    if(!ready_){
     if(claimAttempted_)return reject(peer,h,Status::Closed);claimAttempted_=true;
     decltype(container_)image{};uint64_t generation=0,completed=0;RTXCatalog187::Catalog catalog;
     if(!backend.claim(image,generation,completed)||!generation||completed||!RTXCatalog187::decode(image.data(),image.size(),catalog)||catalog.abi!=2){retire(backend);return reject(peer,h,Status::NotReady);}
     container_=image;catalog_=catalog;generation_=generation;ready_=true;
    }
    if(h.generation!=generation_)return reject(peer,h,Status::Stale);if(!nextSession_)return reject(peer,h,Status::Capacity);
    peer.session=nextSession_++;peer.sequence=1;h.session=peer.session;h.nativeSerial=completed_;h.reply=true;return write(h,container_.data(),container_.size());
   }
   if(!ready_||!peer.session||h.session!=peer.session||h.generation!=generation_||peer.sequence==UINT64_MAX||h.sequence!=peer.sequence+1)return reject(peer,h,Status::BadRequest);
   if(h.op==Op::Close){if(n!=Header)return reject(peer,h,Status::BadRequest);peer.closed=true;peer.sequence=h.sequence;h.reply=true;h.nativeSerial=completed_;return write(h);}
   if(peer.serial==UINT64_MAX||completed_==UINT64_MAX)return reject(peer,h,Status::Capacity);
   // Deep copy once before decoding/translation. A peer serial is not a
   // hardware serial; only this serialized owner allocates the latter.
   Bytes request(bytes+Header,bytes+n);B::Plan plan;
   if(!B::decode(request.data(),request.size(),catalog_.library,generation_,peer.serial+1,plan))return reject(peer,h,Status::BadRequest);
   const uint64_t local=plan.serial,native=completed_+1;P::Q::put64(request.data()+24,native);
   B::Plan nativePlan;if(!B::decode(request.data(),request.size(),catalog_.library,generation_,native,nativePlan))return reject(peer,h,Status::BadRequest);
   // Admission is consumed before possibly effective I/O, including an
   // exception or lost reply. There is no retry of an admitted native serial.
   peer.sequence=h.sequence;peer.serial=local;++executions_;Bytes result;uint64_t completion=0;
   if(!backend.execute(request,result,completion)||completion!=native||!B::readback(nativePlan,request.data(),request.size(),result.data(),result.size())){retire(backend);return reject(peer,h,Status::Backend);}
   completed_=completion;h.reply=true;h.nativeSerial=native;return write(h,result.data(),result.size());
  }catch(...){peer.closed=true;retire(backend);return {};}
 }
};
class Client {
public:enum class Phase {Invalid,Cold,Waiting,Ready,Closed,Failed};
private:
 Phase phase_=Phase::Invalid;uint64_t generation_=0,session_=0,sequence_=0,local_=0,native_=0;int64_t pid_=0;
 std::array<uint8_t,RTXCatalog187::Bytes>container_{};RTXCatalog187::Catalog catalog_{};Identity pending_{};Bytes request_;B::Plan plan_{};
public:
 Client(const uint8_t*image,size_t n,uint64_t generation){if(generation&&RTXCatalog187::decode(image,n,catalog_)&&catalog_.abi==2){std::memcpy(container_.data(),image,n);generation_=generation;phase_=Phase::Cold;}}
 Phase phase()const{return phase_;}uint64_t generation()const{return generation_;}uint64_t session()const{return session_;}uint64_t completed()const{return local_;}uint64_t nativeSerial()const{return native_;}int64_t serverPID()const{return pid_;}
 void fail(){phase_=Phase::Failed;request_.clear();}
 bool hello(Frame&out){if(phase_!=Phase::Cold)return false;try{pending_={Op::Hello,Status::OK,false,0,1,generation_,0};auto f=write(pending_);out=std::move(f);sequence_=1;phase_=Phase::Waiting;return true;}catch(...){fail();return false;}}
 bool execute(const uint8_t*request,size_t n,const uint8_t*payload,size_t bytes,Frame&out){
  if(phase_!=Phase::Ready||local_==UINT64_MAX||sequence_==UINT64_MAX)return false;
  try{
   if(!payload||bytes!=4608||std::memcmp(payload,container_.data()+640,4608)||n<B::Header||n>B::Header+B::MaxPayload||!request){fail();return false;}
   Bytes copy(request,request+n);B::Plan plan;if(!B::decode(copy.data(),copy.size(),catalog_.library,generation_,local_+1,plan)){fail();return false;}
   Identity pending{Op::Execute,Status::OK,false,session_,sequence_+1,generation_,0};auto frame=write(pending,copy.data(),copy.size());request_=std::move(copy);plan_=plan;pending_=pending;++sequence_;out=std::move(frame);phase_=Phase::Waiting;return true;
  }catch(...){fail();return false;}
 }
 bool close(Frame&out){if(phase_!=Phase::Ready||sequence_==UINT64_MAX)return false;try{pending_={Op::Close,Status::OK,false,session_,sequence_+1,generation_,0};auto f=write(pending_);out=std::move(f);++sequence_;phase_=Phase::Waiting;return true;}catch(...){fail();return false;}}
 bool accept(const uint8_t*bytes,size_t n,uint32_t uid,int64_t pid,Bytes&out,uint64_t&completion){
  completion=0;Identity h;
  if(phase_!=Phase::Waiting||uid||pid<=0||(pid_&&pid!=pid_)||!read(bytes,n,h)||!h.reply||h.status!=Status::OK||h.op!=pending_.op||h.sequence!=pending_.sequence||h.generation!=generation_){fail();return false;}
  try{
   if(h.op==Op::Hello){if(n!=Header+container_.size()||!h.session||std::memcmp(bytes+Header,container_.data(),container_.size())){fail();return false;}session_=h.session;pid_=pid;native_=h.nativeSerial;phase_=Phase::Ready;return true;}
   if(h.session!=session_){fail();return false;}
   if(h.op==Op::Close){if(n!=Header||h.nativeSerial<native_){fail();return false;}native_=h.nativeSerial;phase_=Phase::Closed;return true;}
   if(h.nativeSerial<=native_||n!=Header+plan_.payloadBytes||!B::readback(plan_,request_.data(),request_.size(),bytes+Header,n-Header)){fail();return false;}
   Bytes result(bytes+Header,bytes+n);out.swap(result);local_=plan_.serial;native_=h.nativeSerial;completion=local_;request_.clear();phase_=Phase::Ready;return true;
  }catch(...){fail();return false;}
 }
};
}
