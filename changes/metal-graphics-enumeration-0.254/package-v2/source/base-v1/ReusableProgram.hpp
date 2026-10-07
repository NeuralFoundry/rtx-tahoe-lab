#pragma once
#include "changes/gsp-program-library-0.33/ProgramLibrary.hpp"

// Portable protocol proposal. A native adapter must prove actual ownership,
// backing and queue bytes; this core never authorizes unmapping GPU resources.
namespace RtxReusable035 {
namespace P=RtxProgram033;
constexpr unsigned WireBytes=2112,DataBytes=2048,RingEntries=32,CommandBytes=56;
constexpr uint64_t Magic=UINT64_C(0x5254585245513335);
constexpr unsigned DataPhysical=0x0340a000,ConstantPhysical=0x0340b000,QmdPhysical=0x0340c000;
constexpr unsigned FencePhysical=0x0340e000,CommandPhysical=0x03402040,RingPhysical=0x03400000,PutPhysical=0x0340088c;
constexpr uint64_t TimelineVA=P::Q::FenceVA+16;
constexpr unsigned ReleaseWfi64=0x01100001; // SEM_EXECUTE: RELEASE, WFI_EN, 64BIT, no timestamp.
constexpr uint64_t BudgetNs=UINT64_C(5000000000);
constexpr unsigned MaxOperations=65536;
struct Request {uint64_t generation=0,serial=0;unsigned program=0,groups=0;uint8_t data[DataBytes]={};};
inline bool valid(const Request &r,const P::Library &lib){
 if(!r.generation||!r.serial||!lib.count||lib.count>P::MaxPrograms||r.program>=lib.count||!r.groups)return false;
 const auto &p=lib.programs[r.program];
 return p.parameters&&p.parameters<=P::MaxBindings&&p.localX&&p.localX<=64&&r.groups<=64/p.localX&&P::zero(r.data,p.parameters*256,DataBytes);
}
inline bool encode(const Request &r,const P::Library &lib,uint8_t *out,size_t n){
 if(n!=WireBytes||!P::separate(&r,sizeof(r),out,n)||!P::separate(&lib,sizeof(lib),out,n)||!valid(r,lib))return false;
 for(unsigned i=0;i<WireBytes;++i)out[i]=0;
 P::Q::put64(out,Magic);P::Q::put32(out+8,1);P::Q::put32(out+12,WireBytes);
 P::Q::put64(out+16,r.generation);P::Q::put64(out+24,r.serial);P::Q::put32(out+32,r.program);P::Q::put32(out+36,r.groups);
 for(unsigned i=0;i<DataBytes;++i)out[64+i]=r.data[i];return true;
}
inline bool decode(const uint8_t *wire,size_t n,const P::Library &lib,Request &out){
 if(n!=WireBytes||!P::separate(wire,n,&out,sizeof(out))||!P::separate(&lib,sizeof(lib),&out,sizeof(out))||
    P::get64(wire)!=Magic||P::get32(wire+8)!=1||P::get32(wire+12)!=WireBytes||!P::zero(wire,40,64))return false;
 const auto gen=P::get64(wire+16),serial=P::get64(wire+24);const auto program=P::get32(wire+32),groups=P::get32(wire+36);
 if(!gen||!serial||!lib.count||lib.count>P::MaxPrograms||program>=lib.count||!groups)return false;
 const auto &p=lib.programs[program];
 if(!p.parameters||p.parameters>P::MaxBindings||!p.localX||p.localX>64||groups>64/p.localX||!P::zero(wire,64+p.parameters*256,WireBytes))return false;
 out.generation=gen;out.serial=serial;out.program=program;out.groups=groups;
 for(unsigned i=0;i<DataBytes;++i)out.data[i]=wire[64+i];return true;
}
constexpr unsigned entryIndex(uint64_t serial){return unsigned(serial&31);}
constexpr unsigned nextIndex(uint64_t serial){return (entryIndex(serial)+1)&31;}
inline bool nextSerial(uint64_t completed,uint64_t &next){if(completed==UINT64_MAX)return false;next=completed+1;return true;}
struct Plan {
 uint64_t serial=0;unsigned entry=0,put=0;P::Launch launch;
 uint8_t data[2048]={},constant[4096]={},qmd[256]={},command[CommandBytes]={},ringEntry[8]={};
};
inline bool build(const uint8_t *library,const uint8_t *code,const Request &r,Plan &out){
 if(!P::separate(library,512,&out,sizeof(out))||!P::separate(code,4096,&out,sizeof(out))||!P::separate(&r,sizeof(r),&out,sizeof(out)))return false;
 P::Library lib;if(!P::decode(library,512,code,4096,lib)||!valid(r,lib))return false;
 const P::Dispatch dispatch={r.program,0,r.groups};
 if(!P::build(library,512,code,4096,dispatch,out.qmd,256,out.constant,4096,out.command,32,out.launch))return false;
 // Reuse slot zero, with a full 64-bit QMD release value and two-word structure.
 if(!P::Q::B::put(out.qmd,256,{829,1},1)||!P::Q::B::put(out.qmd,256,{830,2},2)||
    !P::Q::B::put(out.qmd,256,{832,32},uint32_t(r.serial))||!P::Q::B::put(out.qmd,256,{864,32},uint32_t(r.serial>>32)))return false;
 for(unsigned i=0;i<2048;++i)out.data[i]=r.data[i];
 // HOST semaphore after PCAS waits for the channel's earlier engine work.
 // It is additional retirement evidence, not permission to free GSP mappings.
 const uint32_t tail[]={0x20050017,uint32_t(TimelineVA&UINT64_C(0xffffffff)),uint32_t(TimelineVA>>32),uint32_t(r.serial),uint32_t(r.serial>>32),ReleaseWfi64};
 for(unsigned i=0;i<6;++i)P::Q::put32(out.command+32+i*4,tail[i]);
 out.serial=r.serial;out.entry=entryIndex(r.serial);out.put=nextIndex(r.serial);
 out.launch.entry=P::commandVA(0)|(UINT64_C(1)<<41)|(uint64_t(CommandBytes/4)<<42);
 P::Q::put64(out.ringEntry,out.launch.entry);return true;
}

enum class Phase:unsigned {Cold,Ready,Exposed,Retained,Exhausted};
enum class Failure:unsigned {None,Identity,State,Shape,Order,Owner,Clock,Timeout,Window,Read,Changed,Write,Notify,Capture,Restore};
struct Observation {unsigned get=0,put=0;uint64_t qmd=0,timeline=0;};
inline bool same(const Observation &a,const Observation &b){return a.get==b.get&&a.put==b.put&&a.qmd==b.qmd&&a.timeline==b.timeline;}
struct Result {
 Failure failure=Failure::None;bool attempted=false,windowAttempted=false,restored=false,passed=false;
 unsigned operations=0,writes=0,notifications=0,polls=0;uint64_t started=0,elapsed=0,serial=0;
};
// Service-owned and allocated before firmware. All entry calls need the native
// service's existing sleepable mutex. There is no allocation in dispatch.
class State {
 Phase phase_=Phase::Cold;uint64_t generation_=0,client_=0,completed_=0;
 uint8_t snapshot_[WireBytes]={};Request request_;Plan plan_;Result result_;
public:
 State()=default;State(const State &)=delete;State &operator=(const State &)=delete;
 Phase phase()const{return phase_;}uint64_t completed()const{return completed_;}
 const Plan &plan()const{return plan_;}const Request &request()const{return request_;}const Result &result()const{return result_;}
 bool open(uint64_t generation,uint64_t client,bool verifiedBootstrap){
  if(phase_!=Phase::Cold||!generation||!client||!verifiedBootstrap)return false;
  generation_=generation;client_=client;phase_=Phase::Ready;return true;
 }
 bool close(uint64_t caller){if(phase_==Phase::Cold||caller!=client_)return false;phase_=Phase::Retained;return true;}
 void ownershipLost(){if(phase_!=Phase::Cold)phase_=Phase::Retained;}
private:
 bool fail(Failure why){if(result_.failure==Failure::None)result_.failure=why;return false;}
 template<class IO>bool tick(IO &io){
  if(!io.ready(generation_,client_))return fail(Failure::Owner);
  const auto now=io.nowNs();if(now<result_.started||now-result_.started<result_.elapsed)return fail(Failure::Clock);
  result_.elapsed=now-result_.started;
  if(result_.elapsed>=BudgetNs||result_.operations>=MaxOperations)return fail(Failure::Timeout);
  ++result_.operations;return true;
 }
 template<class IO>bool observe(IO &io,Observation &v){if(!tick(io))return false;if(!io.observe(v))return fail(Failure::Read);return tick(io);}
 bool before(const Observation &o,bool staged)const{
  return o.get==plan_.entry&&o.put==plan_.entry&&o.qmd==(staged?0:completed_)&&o.timeline==completed_;
 }
 template<class IO>bool write(IO &io,unsigned address,const uint8_t *data,unsigned n){
  if(!tick(io))return false;
  // Exposure is already recorded and the counter advances before a write that
  // may succeed even if the adapter reports failure. No retry is possible.
  ++result_.writes;if(!io.write(address,data,n))return fail(Failure::Write);
  if(!tick(io))return false;if(!io.match(address,data,n))return fail(Failure::Changed);return tick(io);
 }
 template<class IO>bool work(IO &io){
  if(!tick(io))return false;result_.windowAttempted=true;
  if(!io.selectWindow())return fail(Failure::Window);
  Observation first,second;
  if(!observe(io,first)||!before(first,false))return fail(Failure::Changed);
  if(!tick(io)||!io.verifyBefore(plan_,completed_)||!tick(io))return fail(Failure::Capture);
  if(!observe(io,second)||!same(first,second))return fail(Failure::Changed);
  const uint8_t zero[8]={};
  if(!write(io,DataPhysical,plan_.data,2048)||!write(io,ConstantPhysical,plan_.constant,1024)||
     !write(io,QmdPhysical,plan_.qmd,256)||!write(io,FencePhysical,zero,8)||
     !write(io,CommandPhysical,plan_.command,CommandBytes)||!write(io,RingPhysical+plan_.entry*8,plan_.ringEntry,8))return false;
  if(!tick(io)||!io.verifyStaged(plan_,completed_)||!tick(io))return fail(Failure::Capture);
  if(!observe(io,first)||!before(first,true))return fail(Failure::Changed);
  uint8_t put[4];P::Q::put32(put,plan_.put);if(!write(io,PutPhysical,put,4)||!tick(io))return false;
  ++result_.notifications;if(!io.notify())return fail(Failure::Notify);
  while(tick(io)){
   ++result_.polls;if(!observe(io,first))return false;
   if((first.get!=plan_.entry&&first.get!=plan_.put)||first.put!=plan_.put||
      (first.qmd&&first.qmd!=plan_.serial)||(first.timeline!=completed_&&first.timeline!=plan_.serial)||
      (first.timeline==plan_.serial&&first.qmd!=plan_.serial))return fail(Failure::Changed);
   if(first.get==plan_.put&&first.qmd==plan_.serial&&first.timeline==plan_.serial){
    if(!tick(io)||!io.verifyCompleted(plan_,request_)||!tick(io))return fail(Failure::Capture);
    if(!observe(io,second)||!same(first,second))return fail(Failure::Changed);
    return true;
   }
   io.delayUs(100);
  }
  return false;
 }
public:
 template<class IO>Failure dispatch(IO &io,uint64_t caller,const uint8_t *wire,size_t bytes,const uint8_t *library,const uint8_t *code){
  if(phase_==Phase::Cold||caller!=client_)return Failure::Identity;
  if(phase_!=Phase::Ready)return Failure::State;
  if(bytes!=WireBytes||!P::separate(wire,bytes,this,sizeof(*this))||!P::separate(library,512,this,sizeof(*this))||
     !P::separate(code,4096,this,sizeof(*this)))return Failure::Shape;
  for(unsigned i=0;i<WireBytes;++i)snapshot_[i]=wire[i];
  if(P::get64(snapshot_+16)!=generation_)return Failure::Identity;
  uint64_t next;if(!nextSerial(completed_,next)){phase_=Phase::Exhausted;return Failure::State;}
  if(P::get64(snapshot_+24)!=next)return Failure::Order;
  P::Library lib;if(!P::decode(library,512,code,4096,lib)||!decode(snapshot_,WireBytes,lib,request_)||!build(library,code,request_,plan_))return Failure::Shape;
  result_={};result_.serial=next;result_.attempted=true;phase_=Phase::Exposed;
  result_.started=io.nowNs();const bool done=work(io);
  if(result_.windowAttempted){
   // The adapter must use a separate bounded restoration budget and recheck
   // ownership before MMIO. It must not restore another owner's window.
   result_.restored=io.restoreWindow();if(!result_.restored)fail(Failure::Restore);
  }
  if(done&&result_.restored&&tick(io)&&result_.failure==Failure::None){
   completed_=next;result_.passed=true;phase_=completed_==UINT64_MAX?Phase::Exhausted:Phase::Ready;
  }else phase_=Phase::Retained;
  return result_.failure;
 }
};
}
