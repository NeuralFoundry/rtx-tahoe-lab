#pragma once
#include "../gpu240/GraphicsCommands240.hpp"
#include "../ReusableRuntime.hpp"
#include "../driver/GSPDigest.hpp"

namespace RTXGraphicsSubmit241 {
namespace R=RTXGraphicsRequest240;namespace S=RTXSpans165;namespace C=RTXGraphicsCommands240;
namespace N=RtxReusable035;namespace B=RtxReusableBacking035;namespace P=RtxProgram164;
constexpr uint64_t Magic=0x5254584752463234ULL,BudgetNs=5000000000ULL;
constexpr unsigned Records=8192,RecordBytes=48;
enum class Phase:uint64_t {Cold,Preparing,Running,Complete,Retained};
enum class Error:uint64_t {None,Shape,Scope,State,Mapping,Clock,Timeout,Window,Read,Changed,Write,Publish,Notify,Restore,Guard};
struct Observation {uint32_t get=0,put=0,fence=0;uint64_t qmd=0,host=0;};
inline bool equal(const void*a,const void*b,size_t n){const auto*x=static_cast<const uint8_t*>(a),*y=static_cast<const uint8_t*>(b);for(size_t i=0;i<n;++i)if(x[i]!=y[i])return false;return true;}
inline void copy(void*a,const void*b,size_t n){auto*x=static_cast<uint8_t*>(a);const auto*y=static_cast<const uint8_t*>(b);for(size_t i=0;i<n;++i)x[i]=y[i];}
inline bool same(const Observation&a,const Observation&b){return a.get==b.get&&a.put==b.put&&a.qmd==b.qmd&&a.host==b.host&&a.fence==b.fence;}
class State {
 Phase phase_=Phase::Cold;Error error_=Error::None;uint64_t completed_=0,start_=0,elapsed_=0;
 R::Request request_{};S::Plan lease_{};S::Mapping maps_[5]{};C::Output output_{};
 uint8_t wire_[320]{},scratch_[4096]{},ring_[8]{},put_[4]{},zero_[16]{};
 uint8_t before_[B::DeviceBytes]{},staged_[B::DeviceBytes]{},after_[B::DeviceBytes]{},canonical_[B::DeviceBytes]{};
 uint8_t hashes_[128]{},journal_[Records*RecordBytes]{};
 Observation baseline_{};
 unsigned wireBytes_=256;unsigned observations_=0,writes_=0,bells_=0,captures_=0,polls_=0,index_=0,next_=0;
 bool canonicalReady_=false,windowAttempted_=false,restored_=false,submitted_=false;
 unsigned writeRole_=99,writeBytes_=0,controlStage_=0;uint64_t writeOffset_=0;const uint8_t*writeSource_=nullptr;
 State(const State&)=delete;State&operator=(const State&)=delete;
 bool fail(Error e){if(error_==Error::None)error_=e;return false;}
 template<class IO>bool tick(IO&io){if(!io.ready())return fail(Error::Scope);const auto now=io.nowNs();
  if(!start_||now<start_||now-start_<elapsed_)return fail(Error::Clock);elapsed_=now-start_;return elapsed_<BudgetNs||fail(Error::Timeout);}
 template<class IO>bool mappingProof(IO&io,S::Owner<64,16>&owner){
  for(unsigned n=0;n<5;++n){S::Mapping m{},live{};uint32_t refs=0;uint64_t scope=0;
   if(!tick(io)||!owner.inspect(request_.bindings[n].handle,m,refs)||refs!=1||!m.allocation||m.allocation>64||m.mapping!=m.allocation||
      !io.mapping(unsigned(m.allocation-1),scope,live)||scope!=request_.session)return fail(Error::Mapping);
   const auto&a=maps_[n];
   if(m.allocation!=a.allocation||m.mapping!=a.mapping||m.gpuVA!=a.gpuVA||m.logicalBytes!=a.logicalBytes||m.mappedBytes!=a.mappedBytes||m.access!=a.access||
      live.allocation!=a.allocation||live.mapping!=a.mapping||live.gpuVA!=a.gpuVA||live.logicalBytes!=a.logicalBytes||live.mappedBytes!=a.mappedBytes||live.access!=a.access||
      !io.stable(n,0,a.logicalBytes))return fail(Error::Mapping);
  }return tick(io);
 }
 template<class IO>bool observe(IO&io,Observation&o,unsigned stage){
  if(observations_==Records)return fail(Error::Timeout);
  if(!tick(io)||!io.importBuffer(R::Fence)||!io.readBuffer(R::Fence,request_.bindings[R::Fence].offset,scratch_,16))return fail(Error::Read);
  o.fence=P::get32(scratch_);
  if(!io.readControl(N::FencePhysical+16,scratch_,8))return fail(Error::Read);o.host=P::get64(scratch_);
  if(!io.readControl(N::FencePhysical,scratch_,8))return fail(Error::Read);o.qmd=P::get64(scratch_);
  if(!io.readControl(N::PutPhysical-4,scratch_,8))return fail(Error::Read);o.get=P::get32(scratch_);o.put=P::get32(scratch_+4);
  auto*p=journal_+observations_++*RecordBytes;
  const uint64_t words[]={stage,elapsed_,uint64_t(o.get)|(uint64_t(o.put)<<32),o.qmd,o.host,o.fence};
  for(unsigned n=0;n<6;++n)P::Q::put64(p+n*8,words[n]);return tick(io);
 }
 template<class IO>bool controlCapture(IO&io,uint8_t*out){
  if(!tick(io)||!io.rootStable())return fail(Error::Mapping);
  for(unsigned n=0;n<9;++n)if(!tick(io)||!io.readControl(RtxReusableRuntime035::Pages[n],out+n*4096,4096))return fail(Error::Read);
  ++captures_;return tick(io);
 }
 template<class IO>bool bufferWrite(IO&io,unsigned role,const uint8_t*p,unsigned bytes){
  for(unsigned off=0;off<bytes;off+=4096){const unsigned n=bytes-off<4096?bytes-off:4096;
   if(!tick(io)||!io.stable(role,request_.bindings[role].offset+off,n))return fail(Error::Mapping);
   writeRole_=role;writeOffset_=request_.bindings[role].offset+off;writeSource_=p+off;writeBytes_=n;++writes_;
   if(!io.writeBuffer(role,writeOffset_,writeSource_,n))return fail(Error::Write);
   if(!tick(io)||!io.publishBuffer(role))return fail(Error::Publish);
   if(!tick(io)||!io.importBuffer(role)||!io.readBuffer(role,writeOffset_,scratch_,n))return fail(Error::Read);
   if(!equal(scratch_,writeSource_,n))return fail(Error::Changed);writeRole_=99;writeSource_=nullptr;writeBytes_=0;
  }return tick(io);
 }
 template<class IO>bool colorCapture(IO&io,bool after){
  GSPDigest::SHA256 full,guards;const auto&b=request_.bindings[R::Color];
  const auto total=maps_[R::Color].logicalBytes,end=b.offset+b.bytes;
  if(!tick(io)||!io.importBuffer(R::Color))return fail(Error::Read);
  for(uint64_t off=0;off<total;off+=4096){const unsigned n=unsigned(total-off<4096?total-off:4096);
   if(!tick(io)||!io.readBuffer(R::Color,off,scratch_,n))return fail(Error::Read);full.update(scratch_,n);
   unsigned at=0;
   while(at<n){const uint64_t pos=off+at;uint64_t length=n-at;bool guard=false;
    if(pos<b.offset){guard=true;if(length>b.offset-pos)length=b.offset-pos;}
    else if(pos>=end)guard=true;
    else{const uint64_t column=(pos-b.offset)%request_.pitch,pixelBytes=uint64_t(request_.width)*4;
     guard=column>=pixelBytes;const auto run=guard?request_.pitch-column:pixelBytes-column;if(length>run)length=run;}
    if(!length)return fail(Error::Shape);if(guard)guards.update(scratch_+at,unsigned(length));at+=unsigned(length);
   }
  }
  full.finish(hashes_+(after?32:0));guards.finish(hashes_+(after?96:64));
  return tick(io)&&(!after||equal(hashes_+64,hashes_+96,32)||fail(Error::Guard));
 }
 template<class IO>bool materialCapture(IO&io){
  for(unsigned role=0;role<5;++role){if(role==R::Color)continue;
   if(!tick(io)||!io.importBuffer(role))return fail(Error::Read);
   const auto bytes=request_.bindings[role].bytes;const auto*p=role==R::Program?output_.program:role==R::Vertex?output_.vertices:role==R::Command?reinterpret_cast<const uint8_t*>(output_.commands):zero_;
   for(uint64_t off=0;off<bytes;off+=4096){const unsigned n=unsigned(bytes-off<4096?bytes-off:4096);
    if(!tick(io)||!io.readBuffer(role,request_.bindings[role].offset+off,scratch_,n))return fail(Error::Read);
    if(role==R::Fence){if(P::get32(scratch_)!=output_.token||!P::zero(scratch_,4,16))return fail(Error::Changed);}
    else if(!equal(scratch_,p+off,n))return fail(Error::Changed);
   }
  }return tick(io);
 }
 bool completedControl()const{
  const uint64_t end=lease_.addresses[R::Command]+uint64_t(output_.words)*4;
  if(P::get32(after_+0x888)!=next_||P::get32(after_+0x88c)!=next_||
     P::get32(after_+0x840)!=uint32_t(end)||P::get32(after_+0x844)!=uint32_t(end))return false;
  const unsigned highOffsets[2]={0x84c,0x860};
  for(unsigned i=0;i<2;++i){const unsigned offset=highOffsets[i];
   if((P::get32(after_+offset)&255)!=uint32_t(end>>32)||
      (P::get32(after_+offset)&~255u)!=(P::get32(staged_+offset)&~255u))return false;
  }
  for(unsigned n=0;n<B::DeviceBytes;++n){
   if((n>=0x840&&n<0x848)||(n>=0x84c&&n<0x850)||(n>=0x860&&n<0x864)||(n>=0x888&&n<0x890))continue;
   if(after_[n]!=staged_[n])return false;
  }return true;
 }
 template<class IO>bool work(IO&io,S::Owner<64,16>&owner){
  if(!mappingProof(io,owner))return false;windowAttempted_=true;if(!io.selectWindow())return fail(Error::Window);
  Observation a{},b{};if(!observe(io,a,1)||a.get>=32||a.get!=a.put||a.qmd||a.host)return fail(Error::Changed);
  baseline_=a;index_=a.get;next_=(index_+1)&31;
  if(!controlCapture(io,before_))return false;
  if(canonicalReady_){if(!equal(before_,canonical_,B::DeviceBytes))return fail(Error::Changed);}
  else if(!io.baseline(before_))return fail(Error::Changed);
  if(!observe(io,b,2)||!same(a,b)||!colorCapture(io,false))return fail(Error::Changed);
  if(!bufferWrite(io,R::Program,output_.program,4096)||!bufferWrite(io,R::Vertex,output_.vertices,48)||
     !bufferWrite(io,R::Fence,zero_,16)||!bufferWrite(io,R::Command,reinterpret_cast<const uint8_t*>(output_.commands),8192))return false;
  P::Q::put64(ring_,output_.ringEntry);P::Q::put32(put_,next_);controlStage_=1;++writes_;
  if(!tick(io)||!io.writeControl(N::RingPhysical+index_*8,ring_,8))return fail(Error::Write);
  if(!controlCapture(io,staged_))return false;
  for(unsigned n=0;n<B::DeviceBytes;++n){const auto expected=n>=index_*8&&n<index_*8+8?ring_[n-index_*8]:before_[n];if(staged_[n]!=expected)return fail(Error::Changed);}
  if(!observe(io,a,3)||a.get!=index_||a.put!=index_||a.qmd||a.host||a.fence||!mappingProof(io,owner))return fail(Error::Changed);
  if(!owner.markSubmitted(lease_.ticket))return fail(Error::State);submitted_=true;phase_=Phase::Running;controlStage_=2;++writes_;
  if(!io.writeControl(N::PutPhysical,put_,4)||!tick(io))return fail(Error::Write);
  controlStage_=3;++bells_;if(!io.notify())return fail(Error::Notify);
  while(tick(io)){++polls_;if(!observe(io,a,4))return false;
   if((a.get!=index_&&a.get!=next_)||a.put!=next_||a.qmd||a.host||(a.fence&&a.fence!=output_.token))return fail(Error::Changed);
   if(a.get==next_&&a.fence==output_.token){
    if(!controlCapture(io,after_)||!completedControl()||!mappingProof(io,owner)||!materialCapture(io)||!colorCapture(io,true))return fail(Error::Changed);
    return observe(io,b,5)&&same(a,b);
   }
   io.delayUs(100);
  }return false;
 }
public:
 State()=default;
 bool active()const{return phase_!=Phase::Cold;}bool busy()const{return phase_==Phase::Preparing||phase_==Phase::Running;}
 bool retained()const{return phase_==Phase::Retained;}bool idle()const{return phase_==Phase::Cold||phase_==Phase::Complete;}
 bool mayNotify()const{return phase_==Phase::Running&&submitted_&&controlStage_==3&&bells_==1;}
 uint64_t completed()const{return completed_;}Error error()const{return error_;}
 bool roleMapping(unsigned role,S::Mapping&out)const{if(role>=5||!active())return false;out=maps_[role];return true;}
 bool allowsData(unsigned role,uint64_t off,const void*p,unsigned n)const{return phase_==Phase::Preparing&&role==writeRole_&&off==writeOffset_&&n==writeBytes_&&p&&writeSource_&&equal(p,writeSource_,n);}
 bool allowsControl(unsigned address,const void*p,unsigned n)const{
  return p&&((phase_==Phase::Preparing&&controlStage_==1&&address==N::RingPhysical+index_*8&&n==8&&equal(p,ring_,8))||
   (phase_==Phase::Running&&controlStage_==2&&address==N::PutPhysical&&n==4&&equal(p,put_,4)));
 }
 void info(uint64_t generation,uint64_t*out)const{
  const uint64_t values[]={Magic,241,generation,uint64_t(phase_),uint64_t(error_),completed_,request_.serial,lease_.ticket,writes_,bells_,polls_,observations_,elapsed_,restored_,captures_,submitted_,
   output_.ringEntry,output_.fenceAddress,output_.words,output_.token,index_,next_,maps_[R::Color].logicalBytes,request_.width,request_.height,request_.pitch,5,8192,4804,request_.application?320u:0u,request_.application?249u:0u,0};
  for(unsigned n=0;n<32;++n)out[n]=values[n];
 }
 bool capture(unsigned part,uint64_t off,void*out,uint64_t bytes)const{
  if(!out||!bytes||bytes>4096||!P::separate(out,size_t(bytes),this,sizeof(*this)))return false;const void*p=nullptr;uint64_t n=0;
  switch(part){case 0:p=wire_;n=wireBytes_;break;case 1:p=output_.commands;n=8192;break;case 2:p=output_.program;n=4096;break;case 3:p=output_.vertices;n=48;break;
   case 4:p=before_;n=captures_>=1?B::DeviceBytes:0;break;case 5:p=staged_;n=captures_>=2?B::DeviceBytes:0;break;case 6:p=after_;n=captures_>=3?B::DeviceBytes:0;break;
   case 7:p=journal_;n=uint64_t(observations_)*RecordBytes;break;case 8:p=hashes_;n=128;break;default:return false;}
  if(!p||off>n||bytes>n-off)return false;copy(out,static_cast<const uint8_t*>(p)+off,size_t(bytes));return true;
 }
 template<class IO>Error submit(IO&io,S::Owner<64,16>&owner,uint64_t generation,const void*wire,size_t bytes){
  if(!idle())return Error::State;R::Request request;
  if(!P::separate(wire,bytes,this,sizeof(*this))||!R::decode(wire,bytes,request))return Error::Shape;
  if(request.session!=generation)return Error::Scope;if(completed_==UINT32_MAX||request.serial!=completed_+1)return Error::State;
  if(!io.ready())return Error::Scope;const auto now=io.nowNs();if(!now)return Error::Clock;
  S::Plan candidate{};S::Mapping candidateMaps[5]{};
  if(!owner.acquireGraphics(generation,request.bindings,5,candidate))return Error::Mapping;
  bool good=true;for(unsigned n=0;n<5;++n){uint32_t refs=0;good=good&&owner.inspect(request.bindings[n].handle,candidateMaps[n],refs)&&refs==1;}
  // The encoder validates all inputs before writing output. Rejected requests
  // must preserve the preceding completed frame's evidence and mapping record.
  if(!good||!C::build(request,candidateMaps,candidate,output_)){
   if(!owner.cancelPrepared(candidate.ticket)){phase_=Phase::Retained;lease_=candidate;error_=Error::Mapping;owner.retainGraphicsUncertain(candidate.ticket);}return Error::Mapping;
  }
  lease_=candidate;for(unsigned n=0;n<5;++n)maps_[n]=candidateMaps[n];
  request_=request;wireBytes_=unsigned(bytes);copy(wire_,wire,bytes);phase_=Phase::Preparing;error_=Error::None;start_=now;elapsed_=0;
  observations_=writes_=bells_=captures_=polls_=0;windowAttempted_=restored_=submitted_=false;controlStage_=0;writeRole_=99;writeSource_=nullptr;writeBytes_=0;
  const bool passed=work(io,owner);restored_=windowAttempted_&&io.restoreWindow();
  if(!restored_)fail(Error::Restore);
  if(passed&&restored_&&tick(io)&&owner.completeVerified(lease_.ticket)){
   copy(canonical_,after_,B::DeviceBytes);canonicalReady_=true;completed_=request_.serial;phase_=Phase::Complete;return Error::None;
  }
  owner.retainGraphicsUncertain(lease_.ticket);phase_=Phase::Retained;if(error_==Error::None)error_=Error::Changed;return error_;
 }
};
}
