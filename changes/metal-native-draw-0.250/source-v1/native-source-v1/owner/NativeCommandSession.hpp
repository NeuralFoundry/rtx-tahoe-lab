#pragma once
#include "NativeCommandEvidence.hpp"
#include "NativeCompletionObservation.hpp"
#include "NativeResidentEvidence.hpp"
#include <cstdio>

namespace RTXNativeSession036 {
namespace E=RTXNativeEvidence036;namespace P=E::P;
constexpr uint64_t BudgetNs=UINT64_C(30000000000);constexpr unsigned MaxCalls=128;
enum class Failure:unsigned {None,State,Library,Clock,Timeout,Call,Save,Bootstrap,Request,Completion,Observation};
// Owns copied captures and a persistent completion ledger, not device mappings.
// The native adapter holds its connection mutex for each arm/execute operation.
// IO.call must verify exact output/scalar counts and not retain caller pointers.
class Session {
 E::Proof proof_;std::array<uint8_t,4608> payload_{};P::Library library_;uint64_t generation_=0,started_=0,elapsed_=0,epoch_=1;
 std::array<uint8_t,256> residentInfo_{};
 bool configured_=false,armAttempted_=false,attempted_=false;unsigned calls_=0;Failure failure_=Failure::None;
 std::array<uint8_t,512> runtime_{},observation_{};std::array<uint8_t,1024> job_{};
 std::array<uint8_t,12288> root_{};std::array<uint8_t,45056> children_{};std::array<uint8_t,36864> device_{};
 std::array<uint8_t,2112> wire_{};std::array<uint8_t,4096> plan_{};std::array<uint8_t,4608> readPayload_{};
 bool fail(Failure f){if(failure_==Failure::None)failure_=f;return false;}
 template<class IO>bool tick(IO &io){auto now=io.nowNs();if(now<started_||now-started_<elapsed_)return fail(Failure::Clock);elapsed_=now-started_;return elapsed_<BudgetNs||fail(Failure::Timeout);}
 template<class IO>bool call(IO &io,unsigned selector,const uint64_t *scalars,unsigned count,const uint8_t *input,size_t n,uint8_t *output,size_t capacity){
  if(!tick(io))return false;if(calls_>=MaxCalls)return fail(Failure::Timeout);++calls_;
  const bool result=io.call(selector,scalars,count,input,n,output,capacity);
  if(!tick(io))return false;return result||fail(Failure::Call);
 }
 template<class Sink>bool save(Sink &sink,const char *name,const uint8_t *data,size_t n){return sink.save(name,data,n)||fail(Failure::Save);}
 template<class IO,class Sink>bool structure(IO &io,Sink &sink,unsigned selector,const char *name,uint8_t *out,size_t n,const uint64_t *scalars=nullptr,unsigned count=0){
  return call(io,selector,scalars,count,nullptr,0,out,n)&&save(sink,name,out,n);
 }
 template<class IO,class Sink>bool chunks(IO &io,Sink &sink,unsigned selector,uint64_t serial,unsigned part,const char *name,uint8_t *out,unsigned n){
  for(unsigned offset=0;offset<n;offset+=4096){unsigned length=n-offset>4096?4096:n-offset;
   uint64_t scalars[]={serial,part,offset,length};
   if(selector==54){scalars[0]=offset;scalars[1]=length;if(!call(io,selector,scalars,2,nullptr,0,out+offset,length))return false;}
   else if(!call(io,selector,scalars+((selector==71||selector==83)?0:1),(selector==71||selector==83)?4:3,nullptr,0,out+offset,length))return false;
  }
  return save(sink,name,out,n);
 }
 template<class IO>void start(IO &io){failure_=Failure::None;calls_=0;elapsed_=0;attempted_=false;started_=io.nowNs();}
public:
 Session()=default;Session(const Session&)=delete;Session&operator=(const Session&)=delete;
 bool ready()const{return proof_.ready();}bool attempted()const{return attempted_;}uint64_t completed()const{return proof_.completed();}
 uint64_t epoch()const{return epoch_;}
 Failure failure()const{return failure_;}unsigned calls()const{return calls_;}uint64_t elapsed()const{return elapsed_;}
 void retire(){proof_.retire();}
 bool copyPristineDevice(std::array<uint8_t,36864>&out)const{
  if(!ready()||completed()||epoch_!=1||attempted_||failure_!=Failure::None)return false;out=device_;return true;
 }
 bool configure(uint64_t generation,E::Bytes payload){
  if(configured_||armAttempted_||!generation||!E::shape(payload,4608))return false;
  P::Library library;if(!P::decode(payload.data,512,payload.data+512,4096,library)||!RtxLibraryUpload036::profile(library))return false;
  generation_=generation;library_=library;std::memcpy(payload_.data(),payload.data,4608);configured_=true;return true;
 }
 template<class IO,class Sink>bool arm(IO &io,Sink &sink){
  if(!configured_||armAttempted_)return fail(Failure::State);armAttempted_=true;start(io);
  auto work=[&](){
   if(!structure(io,sink,68,"runtime-before.bin",runtime_.data(),512))return false;
   unsigned childBytes=0;if(!E::runtime({runtime_.data(),512},generation_,0,library_,childBytes))return fail(Failure::Bootstrap);
   std::array<uint8_t,256> uploaded{};
   if(!structure(io,sink,72,"upload-info.bin",uploaded.data(),256)||!E::upload({uploaded.data(),256},generation_,{payload_.data(),4608},3))return fail(Failure::Library);
   if(!chunks(io,sink,71,0,5,"library.bin",readPayload_.data(),512)||!chunks(io,sink,71,0,6,"code.bin",readPayload_.data()+512,4096)||!E::equal(payload_.data(),readPayload_.data(),4608))return fail(Failure::Library);
   if(!chunks(io,sink,67,0,0,"root.bin",root_.data(),12288)||!chunks(io,sink,67,0,1,"children.bin",children_.data(),childBytes)||!chunks(io,sink,67,0,2,"device.bin",device_.data(),36864))return false;
   // Same connection's earlier HOST/table snapshots, not caller-provided data.
   std::array<uint8_t,12288> executionRoot{},executionDevice{};std::array<uint8_t,45056> executionChildren{};
   if(!chunks(io,sink,51,0,0,"execution-root.bin",executionRoot.data(),12288)||!chunks(io,sink,51,0,1,"execution-children.bin",executionChildren.data(),childBytes)||!chunks(io,sink,54,0,0,"execution-device.bin",executionDevice.data(),12288))return false;
   std::array<uint8_t,512> after{};if(!structure(io,sink,68,"runtime-after.bin",after.data(),512)||after!=runtime_)return fail(Failure::Bootstrap);
   if(!structure(io,sink,82,"resident-initial.bin",residentInfo_.data(),256)||!RTXResidentEvidence058::initial({residentInfo_.data(),256},generation_))return fail(Failure::Library);
   return proof_.seed(generation_,{payload_.data(),4608},{runtime_.data(),512},{root_.data(),12288},{children_.data(),childBytes},{device_.data(),36864},
    {executionRoot.data(),12288},{executionChildren.data(),childBytes},{executionDevice.data(),12288})||fail(Failure::Bootstrap);
  };
  bool success=work();if(!success)proof_.retire();return success;
 }
 template<class IO,class Sink>bool replace(IO &io,Sink &sink,E::Bytes payload){
  if(!ready()||epoch_==UINT64_MAX)return fail(Failure::State);start(io);
  std::array<uint8_t,4608> candidate{};std::array<uint8_t,128> header{};
  std::array<uint8_t,256> before{},sealed{},after{},stable{};
  auto work=[&](){
   if(!E::shape(payload,4608))return fail(Failure::Library);
   std::memcpy(candidate.data(),payload.data,4608);
   if(!RTXResidentEvidence058::header(header,generation_,epoch_,completed(),{candidate.data(),4608})||
      !save(sink,"candidate-payload.bin",candidate.data(),4608)||!save(sink,"replacement-header.bin",header.data(),128))return fail(Failure::Library);
   if(!structure(io,sink,68,"runtime-before.bin",runtime_.data(),512)||!proof_.checkRuntime({runtime_.data(),512})||
      !structure(io,sink,82,"resident-before.bin",before.data(),256)||before!=residentInfo_)return fail(Failure::Completion);
   if(!call(io,78,nullptr,0,header.data(),128,nullptr,0))return false;
   for(uint64_t offset=0;offset<4608;offset+=1024){
    const size_t n=4608-offset<1024?4608-offset:1024;
    if(!call(io,79,&offset,1,candidate.data()+offset,n,nullptr,0))return false;
   }
   if(!call(io,80,nullptr,0,nullptr,0,nullptr,0)||!structure(io,sink,82,"resident-sealed.bin",sealed.data(),256)||
      !RTXResidentEvidence058::sealed({sealed.data(),256},{header.data(),128}))return fail(Failure::Library);
   if(!chunks(io,sink,83,epoch_,2,"pending-library.bin",readPayload_.data(),512)||
      !chunks(io,sink,83,epoch_,3,"pending-code.bin",readPayload_.data()+512,4096)||candidate!=readPayload_)return fail(Failure::Library);
   attempted_=true;const bool applied=call(io,81,&epoch_,1,nullptr,0,nullptr,0);
   // Even an uncertain apply is never replayed. Retained CPU snapshots remain
   // readable, and their complete status is saved before any result is accepted.
   bool collected=structure(io,sink,82,"resident-after.bin",after.data(),256);
   const uint64_t observed=P::get64(after.data()+24);
   if(observed!=epoch_&&observed!=epoch_+1)return fail(Failure::Completion);
   const unsigned cb=proof_.childrenBytes();
   collected &= chunks(io,sink,83,observed,0,"library.bin",readPayload_.data(),512);
   collected &= chunks(io,sink,83,observed,1,"code.bin",readPayload_.data()+512,4096);
   collected &= chunks(io,sink,83,observed,4,"root.bin",root_.data(),12288);
   collected &= chunks(io,sink,83,observed,5,"children.bin",children_.data(),cb);
   collected &= chunks(io,sink,83,observed,6,"device.bin",device_.data(),36864);
   collected &= structure(io,sink,68,"runtime-after.bin",runtime_.data(),512);
   collected &= structure(io,sink,82,"resident-stable.bin",stable.data(),256);
   if(!applied||!collected)return false;
   if(after!=stable||!RTXResidentEvidence058::replaced({after.data(),256},{header.data(),128},cb)||candidate!=readPayload_||
      !proof_.acceptReplacement({runtime_.data(),512},{root_.data(),12288},{children_.data(),cb},{device_.data(),36864},{readPayload_.data(),4608}))return fail(Failure::Completion);
   payload_=candidate;library_=proof_.library();residentInfo_=after;++epoch_;return true;
  };
  const bool success=work();if(!success)proof_.retire();return success;
 }
 template<class IO,class Sink>bool execute(IO &io,Sink &sink,E::Bytes request,E::Bytes payload,std::array<uint8_t,2048> &result,uint64_t &completion){
  completion=0;if(!ready())return fail(Failure::State);start(io);
  auto work=[&](){
   if(!E::shape(payload,4608)||!E::equal(payload.data,payload_.data(),4608))return fail(Failure::Library);
   if(!E::shape(request,2112)||!save(sink,"request.bin",request.data,request.size))return fail(Failure::Request);
   std::array<uint8_t,256> residentBefore{};
   if(!structure(io,sink,82,"resident-before.bin",residentBefore.data(),256)||residentBefore!=residentInfo_)return fail(Failure::Library);
   if(!structure(io,sink,68,"runtime-before.bin",runtime_.data(),512))return false;
   if(!proof_.begin(request,{runtime_.data(),512}))return fail(Failure::Request);
   attempted_=true;bool submitted=call(io,84,&epoch_,1,request.data,request.size,nullptr,0);
   // Collect diagnostics even if the submit method reports an uncertain error.
   // Neither the submit call nor publication is retried.
   uint64_t serial=P::get64(request.data+24);unsigned cb=proof_.childrenBytes();bool collected=true;
   collected &= structure(io,sink,68,"runtime-after.bin",runtime_.data(),512);
   collected &= structure(io,sink,70,"job-info.bin",job_.data(),1024,&serial,1);
   collected &= structure(io,sink,77,"completion-observation.bin",observation_.data(),512,&serial,1);
   collected &= chunks(io,sink,71,serial,0,"root.bin",root_.data(),12288);
   collected &= chunks(io,sink,71,serial,1,"children.bin",children_.data(),cb);
   collected &= chunks(io,sink,71,serial,2,"device.bin",device_.data(),36864);
   collected &= chunks(io,sink,71,serial,3,"request-capture.bin",wire_.data(),2112);
   collected &= chunks(io,sink,71,serial,4,"plan.bin",plan_.data(),4096);
   collected &= chunks(io,sink,71,0,5,"library.bin",readPayload_.data(),512);
   collected &= chunks(io,sink,71,0,6,"code.bin",readPayload_.data()+512,4096);
   std::array<uint8_t,256> residentAfter{};collected &= structure(io,sink,82,"resident-after.bin",residentAfter.data(),256);
   if(!submitted||!collected)return false;
   if(residentAfter!=residentInfo_)return fail(Failure::Library);
   if(!RTXNativeObservation037::completed({observation_.data(),512},{runtime_.data(),512},{job_.data(),1024},generation_,serial,cb))return fail(Failure::Observation);
   if(!proof_.accept({runtime_.data(),512},{job_.data(),1024},{root_.data(),12288},{children_.data(),cb},{device_.data(),36864},
      {wire_.data(),2112},{plan_.data(),4096},{readPayload_.data(),4608},result))return fail(Failure::Completion);
   completion=serial;return true;
  };
  const bool success=work();if(!success)proof_.retire();return success;
 }
};
}
