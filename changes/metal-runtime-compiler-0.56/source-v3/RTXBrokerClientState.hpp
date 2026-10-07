#pragma once
#include "RTXBrokerWire.hpp"

namespace RTXClient041 {
using namespace RTXBroker040;
enum class Phase {Fresh,Waiting,Ready,Closed,Failed};
// Serialized by the Objective-C transport. An outbound identity is consumed
// before the channel call; interruption/timeout is terminal, never retryable.
class State {
 Phase phase_=Phase::Fresh;HeaderView pending_{};uint64_t generation_=0,session_=0,sequence_=0,local_=0,native_=0,pendingLocal_=0;
 int64_t serverPID_=0;Status status_=Status::OK;
 std::array<uint8_t,RTXLibrary036::Bytes> container_{};RTXLibrary036::Catalog catalog_{};
 bool valid_=false;
public:
 State(const uint8_t *container,size_t length,uint64_t generation):generation_(generation){
  valid_=container&&generation&&length==container_.size()&&RTXLibrary036::decode(container,length,catalog_);
  if(valid_)std::memcpy(container_.data(),container,length);else phase_=Phase::Failed;
 }
 Phase phase()const{return phase_;}uint64_t generation()const{return generation_;}uint64_t session()const{return session_;}
 uint64_t completed()const{return local_;}uint64_t nativeSerial()const{return native_;}int64_t serverPID()const{return serverPID_;}Status status()const{return status_;}
 void fail(){phase_=Phase::Failed;}
 bool beginHello(Frame &out){
  if(!valid_||phase_!=Phase::Fresh)return false;
  pending_={Op::Hello,Status::OK,false,0,1,generation_,0};sequence_=1;phase_=Phase::Waiting;out=write(pending_);return true;
 }
 bool beginExecute(const uint8_t *bytes,size_t n,const uint8_t *payload,size_t payloadBytes,Frame &out){
  if(phase_!=Phase::Ready)return false;
  RtxReusable035::Request request;
  if(!bytes||!payload||payloadBytes!=4608||std::memcmp(payload,container_.data()+640,4608)||
     !RtxReusable035::decode(bytes,n,catalog_.library,request)||request.generation!=generation_||
     local_==UINT64_MAX||sequence_==UINT64_MAX||request.serial!=local_+1){fail();return false;}
  pendingLocal_=request.serial;pending_={Op::Execute,Status::OK,false,session_,++sequence_,generation_,0};
  phase_=Phase::Waiting;out=write(pending_,bytes,n);return out.size!=0;
 }
 bool beginClose(Frame &out){
  if(phase_!=Phase::Ready||sequence_==UINT64_MAX)return false;
  pending_={Op::Close,Status::OK,false,session_,++sequence_,generation_,0};phase_=Phase::Waiting;out=write(pending_);return true;
 }
 bool accept(const uint8_t *bytes,size_t n,uint32_t serverUID,int64_t serverPID,std::array<uint8_t,2048> &result,uint64_t &completion){
  result.fill(0);completion=0;HeaderView h;
  if(phase_!=Phase::Waiting||serverUID!=0||serverPID<=0||(serverPID_&&serverPID_!=serverPID)||
     !read(bytes,n,h)||!h.reply||h.op!=pending_.op||h.sequence!=pending_.sequence||h.generation!=generation_){fail();return false;}
  if(h.status!=Status::OK){status_=h.status;fail();return false;}
  if(h.op==Op::Hello){
   if(n!=MaxFrame||!h.session||h.nativeSerial||std::memcmp(bytes+Header,container_.data(),container_.size())){fail();return false;}
   session_=h.session;serverPID_=serverPID;phase_=Phase::Ready;return true;
  }
  if(h.session!=session_){fail();return false;}
  if(h.op==Op::Close){if(n!=Header||h.nativeSerial){fail();return false;}phase_=Phase::Closed;return true;}
  // Other clients may occupy intervening global native serials. Require a
  // strictly increasing native serial, but publish this client's local serial.
  if(n!=Header+result.size()||!h.nativeSerial||h.nativeSerial<=native_){fail();return false;}
  std::memcpy(result.data(),bytes+Header,result.size());native_=h.nativeSerial;local_=pendingLocal_;completion=local_;phase_=Phase::Ready;return true;
 }
};
}
