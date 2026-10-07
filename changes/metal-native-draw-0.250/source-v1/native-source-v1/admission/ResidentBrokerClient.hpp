#pragma once
#include "ResidentBrokerWire.hpp"
namespace RTXResidentClient058 {
using namespace RTXResidentBroker058;
enum class Phase {Fresh,Waiting,Ready,Closed,Failed};
class State {
 Phase phase_=Phase::Fresh;Identity pending_{};uint64_t generation_=0,session_=0,sequence_=0,local_=0,native_=0,epoch_=0,pendingLocal_=0;
 int64_t serverPID_=0;Status status_=Status::OK;
 std::array<uint8_t,RTXLibrary036::Bytes> initial_{};std::array<uint8_t,Payload> previous_{},pendingPayload_{};bool submitted_=false;
public:
 State(const uint8_t *image,size_t n,uint64_t generation):generation_(generation){
  RTXLibrary036::Catalog parsed;if(!generation||n!=initial_.size()||!RTXLibrary036::decode(image,n,parsed))phase_=Phase::Failed;
  else std::memcpy(initial_.data(),image,n);
 }
 Phase phase()const{return phase_;}uint64_t generation()const{return generation_;}uint64_t session()const{return session_;}uint64_t completed()const{return local_;}uint64_t epoch()const{return epoch_;}uint64_t nativeSerial()const{return native_;}
 int64_t serverPID()const{return serverPID_;}Status status()const{return status_;}void fail(){phase_=Phase::Failed;}
 bool beginHello(Frame &out){
  if(phase_!=Phase::Fresh)return false;sequence_=1;pending_={Op::Hello,Status::OK,false,0,sequence_,generation_,0,0};out=write(pending_);phase_=Phase::Waiting;return true;
 }
 bool beginExecute(const uint8_t *wire,size_t n,const uint8_t *payload,size_t pn,Frame &out){
  if(phase_!=Phase::Ready)return false;P::Library lib;N::Request request;
  if(pn!=Payload||!library(payload,lib)||n!=Request||!N::decode(wire,n,lib,request)||request.generation!=generation_||local_==UINT64_MAX||sequence_==UINT64_MAX||request.serial!=local_+1){fail();return false;}
  std::array<uint8_t,Payload+Request> body{};std::memcpy(body.data(),payload,Payload);std::memcpy(body.data()+Payload,wire,Request);pendingPayload_=bodyPayload(body);
  pendingLocal_=request.serial;pending_={Op::Execute,Status::OK,false,session_,++sequence_,generation_,0,0};out=write(pending_,body.data(),body.size());phase_=Phase::Waiting;return out.size==MaxFrame;
 }
 bool beginClose(Frame &out){
  if(phase_!=Phase::Ready||sequence_==UINT64_MAX)return false;pending_={Op::Close,Status::OK,false,session_,++sequence_,generation_,0,0};out=write(pending_);phase_=Phase::Waiting;return true;
 }
 bool accept(const uint8_t *bytes,size_t n,uint32_t serverUID,int64_t serverPID,std::array<uint8_t,2048> &result,uint64_t &completion){
  result.fill(0);completion=0;Identity h;
  if(phase_!=Phase::Waiting||serverUID!=0||serverPID<=0||(serverPID_&&serverPID_!=serverPID)||!read(bytes,n,h)||!h.reply||h.op!=pending_.op||h.sequence!=pending_.sequence||h.generation!=generation_){fail();return false;}
  if(h.status!=Status::OK){status_=h.status;fail();return false;}
  if(!h.epoch||h.epoch<epoch_){fail();return false;}
  if(h.op==Op::Hello){
   if(n!=HelloBytes||!h.session||h.nativeSerial||std::memcmp(bytes+Header,initial_.data(),initial_.size())){fail();return false;}
   session_=h.session;serverPID_=serverPID;epoch_=h.epoch;phase_=Phase::Ready;return true;
  }
  if(h.session!=session_){fail();return false;}
  if(h.op==Op::Close){if(n!=Header||h.nativeSerial){fail();return false;}epoch_=h.epoch;phase_=Phase::Closed;return true;}
  if(n!=Header+result.size()||!h.nativeSerial||h.nativeSerial<=native_||(submitted_&&previous_!=pendingPayload_&&h.epoch<=epoch_)){fail();return false;}
  std::memcpy(result.data(),bytes+Header,result.size());previous_=pendingPayload_;submitted_=true;native_=h.nativeSerial;epoch_=h.epoch;local_=pendingLocal_;completion=local_;phase_=Phase::Ready;return true;
 }
private:
 static std::array<uint8_t,Payload> bodyPayload(const std::array<uint8_t,Payload+Request> &body){std::array<uint8_t,Payload> p{};std::memcpy(p.data(),body.data(),Payload);return p;}
};
}
