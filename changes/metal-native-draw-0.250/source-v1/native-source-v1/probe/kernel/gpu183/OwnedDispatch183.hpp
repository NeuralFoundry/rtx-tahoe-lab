#pragma once
#include "../root179/OwnedBufferSpans165.hpp"
#include "../ReusableRuntime.hpp"
#include "Program205.hpp"

// Serialized service-owned executor. IO performs the actual native MMIO,
// mapping checks and synchronization. No caller supplies a GPU address or a
// completion callback. Program decoding is not a proof of shader robustness.
namespace RTXOwnedDispatch183 {
namespace P=RtxProgram164;namespace Q=P::Q;namespace S=RTXSpans165;
namespace N=RtxReusable035;namespace B=RtxReusableBacking035;
constexpr unsigned RequestBytes=384,InfoBytes=128,MaxObservations=4096;
constexpr uint64_t Magic=0x5254584744503138ULL,BudgetNs=5000000000ULL;
enum class Phase:uint64_t {Cold,Loading,Ready,Running,Retained};
enum class Error:uint64_t {None,Shape,Scope,State,Program,Mapping,Clock,Timeout,Window,Read,Changed,Write,Notify,Sync,Restore};
inline void copy(void*out,const void*in,size_t n){auto*d=static_cast<uint8_t*>(out);const auto*s=static_cast<const uint8_t*>(in);for(size_t i=0;i<n;++i)d[i]=s[i];}
inline bool equal(const void*a,const void*b,size_t n){const auto*x=static_cast<const uint8_t*>(a);const auto*y=static_cast<const uint8_t*>(b);for(size_t i=0;i<n;++i)if(x[i]!=y[i])return false;return true;}
struct Request {uint64_t session=0,serial=0;uint32_t program=0,count=0;RTXGeometry164::Size groups{},threads{};S::Binding bindings[8]{};};
inline bool decode(const uint8_t*p,size_t n,Request&r){
 if(n!=RequestBytes||!P::separate(p,n,&r,sizeof(r))||P::get64(p)!=Magic||P::get32(p+8)!=183||P::get32(p+12)!=n||!P::zero(p,40,48)||!P::zero(p,96,128))return false;
 Request v;v.session=P::get64(p+16);v.serial=P::get64(p+24);v.program=P::get32(p+32);v.count=P::get32(p+36);
 if(!v.session||!v.serial||!v.count||v.count>8)return false;
 v.groups={P::get64(p+48),P::get64(p+56),P::get64(p+64)};v.threads={P::get64(p+72),P::get64(p+80),P::get64(p+88)};
 for(unsigned i=0;i<8;++i){const auto*q=p+128+i*32;if(i>=v.count){if(!P::zero(q,0,32))return false;continue;}
  if(P::get32(q+28))return false;v.bindings[i]={P::get64(q),P::get64(q+8),P::get64(q+16),P::get32(q+24)};
  if(!v.bindings[i].handle||!v.bindings[i].bytes||(v.bindings[i].offset&3)||v.bindings[i].index>=32)return false;
 }
 r=v;return true;
}
class State {
 Phase phase_=Phase::Cold;Error error_=Error::None;uint64_t session_=0,completed_=0,started_=0,elapsed_=0;
 unsigned cursor_=0,writes_=0,bells_=0,polls_=0,captures_=0,observations_=0;
 bool canonicalReady_=false,windowAttempted_=false,restored_=false,sealed_=false;
 uint8_t library_[512]{},code_[4096]{},wire_[RequestBytes]{},qmd_[256]{},constants_[4096]{},command_[56]{},ring_[8]{};
 uint8_t before_[B::DeviceBytes]{},staged_[B::DeviceBytes]{},after_[B::DeviceBytes]{},canonical_[B::DeviceBytes]{},candidate_[B::DeviceBytes]{};
 uint8_t scratch_[4096]{},journal_[MaxObservations*40]{};
 RTXProgram205::Library program205_{};uint64_t revision205_=0;uint8_t selection205_[4608]{};
 Request request_{};S::Plan plan_{};
 State(const State&)=delete;State&operator=(const State&)=delete;
 bool fail(Error e){if(error_==Error::None)error_=e;return false;}
 template<class IO>bool tick(IO&io){
  if(!io.ready())return fail(Error::Scope);const auto now=io.nowNs();
  if(!started_||now<started_||now-started_<elapsed_)return fail(Error::Clock);
  elapsed_=now-started_;return elapsed_<BudgetNs||fail(Error::Timeout);
 }
 bool build(S::Owner<64,16>&owner){
  const auto&p=program205_.programs.programs[request_.program];
  if(!Q::build(qmd_,sizeof(qmd_),constants_,sizeof(constants_),command_,32))return false;
  const uint64_t va=Q::ProgramVA+p.offset,serial=request_.serial;
  const struct Update{Q::B::Field field;uint32_t value;}fields[]={
   {{256,32},uint32_t(va>>8)},{{1632,9},uint32_t(va>>40)},{{1536,32},uint32_t(va)},{{1568,17},uint32_t(va>>32)},
   {{1641,9},(p.bytes+255)/256},{{648,9},p.registers},{{1075,13},(p.constantBytes+15)/16},
   {{829,1},1},{{830,2},2},{{832,32},uint32_t(serial)},{{864,32},uint32_t(serial>>32)}
  };
  for(const auto&f:fields)if(!Q::B::put(qmd_,256,f.field,f.value))return false;
  if(!owner.patchPrepared(plan_.ticket,qmd_,256,constants_,4096))return false;
  const uint32_t tail[]={0x20050017,uint32_t(N::TimelineVA),uint32_t(N::TimelineVA>>32),uint32_t(serial),uint32_t(serial>>32),N::ReleaseWfi64};
  for(unsigned i=0;i<6;++i)Q::put32(command_+32+i*4,tail[i]);
  Q::put64(ring_,RtxProgram033::commandVA(0)|(uint64_t(1)<<41)|(uint64_t(14)<<42));return true;
 }
 static unsigned address(unsigned i,unsigned entry){const unsigned a[]={Q::ProgramPhysical,N::ConstantPhysical,N::QmdPhysical,N::FencePhysical,N::CommandPhysical,N::RingPhysical+entry*8,N::PutPhysical};return i<7?a[i]:0;}
 static unsigned size(unsigned i){const unsigned n[]={4096,1024,256,8,56,8,4};return i<7?n[i]:0;}
 const uint8_t*payload(unsigned i)const{return i==0?code_:i==1?constants_:i==2?qmd_:i==4?command_:i==5?ring_:nullptr;}
 unsigned offset(unsigned i)const{const unsigned a[]={B::Image,B::Constant,B::Qmd,B::Fence,4096+64,N::entryIndex(request_.serial)*8,0x88c};return a[i];}
 template<class IO>bool write(IO&io,unsigned i){
  uint8_t small[8]{};if(i==6)Q::put32(small,N::nextIndex(request_.serial));const auto*p=payload(i);if(!p)p=small;
  if(!tick(io)||writes_!=i)return fail(Error::State);++writes_;
  copy(candidate_+offset(i),p,size(i));
  const unsigned a=address(i,N::entryIndex(request_.serial));
  if(!io.write(a,p,size(i)))return fail(Error::Write);
  if(!tick(io)||!io.read(a,scratch_,size(i)))return fail(Error::Read);
  if(!equal(p,scratch_,size(i)))return fail(Error::Changed);return tick(io);
 }
 template<class IO>bool observe(IO&io,N::Observation&o,unsigned stage){
  if(observations_==MaxObservations)return fail(Error::Timeout);if(!tick(io))return false;
  // Ordered reads: later HOST release before QMD release before queue indices.
  if(!io.read(N::FencePhysical+16,scratch_,8))return fail(Error::Read);o.timeline=P::get64(scratch_);
  if(!io.read(N::FencePhysical,scratch_,8))return fail(Error::Read);o.qmd=P::get64(scratch_);
  if(!io.read(N::PutPhysical-4,scratch_,8))return fail(Error::Read);o.get=P::get32(scratch_);o.put=P::get32(scratch_+4);
  auto*p=journal_+observations_++*40;Q::put64(p,stage);Q::put64(p+8,elapsed_);Q::put64(p+16,uint64_t(o.get)|(uint64_t(o.put)<<32));Q::put64(p+24,o.qmd);Q::put64(p+32,o.timeline);
  return tick(io);
 }
 template<class IO>bool capture(IO&io,uint8_t*out){
  if(!tick(io)||!io.rootStable())return fail(Error::Mapping);
  for(unsigned i=0;i<9;++i)if(!tick(io)||!io.read(RtxReusableRuntime035::Pages[i],out+i*4096,4096))return fail(Error::Read);
  ++captures_;return tick(io);
 }
 template<class IO>bool mappingProof(IO&io,S::Owner<64,16>&owner,bool importing){
  for(unsigned i=0;i<request_.count;++i){const auto&b=request_.bindings[i];S::Mapping m{},expected{};uint32_t refs=0;uint64_t scope=0;
   if(!owner.inspect(b.handle,m,refs)||!refs||!m.allocation||m.allocation>64||m.mapping!=m.allocation||
      !io.mapping(unsigned(m.allocation-1),scope,expected)||scope!=session_||
      m.allocation!=expected.allocation||m.mapping!=expected.mapping||m.gpuVA!=expected.gpuVA||m.logicalBytes!=expected.logicalBytes||m.mappedBytes!=expected.mappedBytes||m.access!=expected.access)return fail(Error::Mapping);
   if(!tick(io)||!io.stable(unsigned(m.allocation-1),b.offset,b.bytes))return fail(Error::Mapping);
   if(importing&&!io.import(unsigned(m.allocation-1)))return fail(Error::Sync);
   if(!tick(io)||!io.stable(unsigned(m.allocation-1),b.offset,b.bytes))return fail(Error::Mapping);
  }
  return true;
 }
 bool completedImage()const{
  if(P::get32(after_+0x888)!=N::nextIndex(request_.serial)||P::get32(after_+0x88c)!=N::nextIndex(request_.serial)||
     P::get32(after_+0x840)!=0x20001078||P::get32(after_+0x844)!=0x20001078||
     P::get64(after_+B::Fence)!=request_.serial||P::get64(after_+B::Fence+16)!=request_.serial)return false;
  for(unsigned i=0;i<B::DeviceBytes;++i){const bool allowed=(i>=B::Qmd&&i<B::Qmd+256)||(i>=0x840&&i<0x848)||(i>=0x888&&i<0x88c)||
    (i>=B::Fence&&i<B::Fence+8)||(i>=B::Fence+16&&i<B::Fence+24);
   if(!allowed&&after_[i]!=candidate_[i])return false;
  }
  return true;
 }
 template<class IO>bool work(IO&io,S::Owner<64,16>&owner){
  if(!tick(io)||!mappingProof(io,owner,false))return false;windowAttempted_=true;
  if(!io.selectWindow())return fail(Error::Window);
  N::Observation a{},b{};const N::Observation expected{N::entryIndex(request_.serial),N::entryIndex(request_.serial),completed_,completed_};
  if(!observe(io,a,1)||!N::same(a,expected))return fail(Error::Changed);
  if(!capture(io,before_))return false;
  if(canonicalReady_){if(!equal(before_,canonical_,B::DeviceBytes))return fail(Error::Changed);}
  else if(!io.baseline(before_))return fail(Error::Changed);
  copy(candidate_,before_,B::DeviceBytes);
  if(!observe(io,b,2)||!N::same(a,b))return fail(Error::Changed);
  for(unsigned i=0;i<6;++i)if(!write(io,i))return false;
  if(!capture(io,staged_)||!equal(staged_,candidate_,B::DeviceBytes))return fail(Error::Changed);
  const N::Observation stagedExpected{expected.get,expected.put,0,completed_};
  if(!observe(io,a,3)||!N::same(a,stagedExpected))return fail(Error::Changed);
  if(!mappingProof(io,owner,false)||!write(io,6)||!tick(io))return false;
  ++bells_;if(!io.notify())return fail(Error::Notify);
  while(tick(io)){
   ++polls_;if(!observe(io,a,4))return false;
   if((a.get!=expected.get&&a.get!=N::nextIndex(request_.serial))||a.put!=N::nextIndex(request_.serial)||
      (a.qmd&&a.qmd!=request_.serial)||(a.timeline!=completed_&&a.timeline!=request_.serial)||
      (a.timeline==request_.serial&&a.qmd!=request_.serial))return fail(Error::Changed);
   if(a.get==a.put&&a.qmd==request_.serial&&a.timeline==request_.serial){
    if(!capture(io,after_)||!completedImage())return fail(Error::Changed);
    if(!observe(io,b,5)||!N::same(a,b))return fail(Error::Changed);return true;
   }
   io.delayUs(100);
  }
  return false;
 }
public:
 State()=default;
 bool active()const{return phase_!=Phase::Cold;}
 bool retained()const{return phase_==Phase::Retained;}
 bool idle()const{return phase_==Phase::Cold||phase_==Phase::Loading||phase_==Phase::Ready;}
 bool running()const{return phase_==Phase::Running;}
 bool mayNotify()const{return running()&&writes_==7&&bells_==1;}
 uint64_t completed()const{return completed_;}
 Error error()const{return error_;}
 bool allowedWrite(unsigned a,const uint8_t*p,unsigned n)const{
  if(!running()||!writes_||writes_>7||!p)return false;const unsigned i=writes_-1;
  return a==address(i,N::entryIndex(request_.serial))&&n==size(i)&&equal(p,candidate_+offset(i),n);
 }
 void info(uint64_t registry,uint64_t*out)const{
  const uint64_t v[]={Magic,183,session_?session_:registry,uint64_t(phase_),uint64_t(error_),completed_,plan_.ticket,writes_,bells_,polls_,elapsed_,restored_,captures_,sealed_,cursor_,observations_};
  for(unsigned i=0;i<16;++i)out[i]=v[i];
 }
 bool captureBytes(unsigned part,uint64_t off,void*out,uint64_t n)const{
  if(!out||!n||n>4096||!P::separate(out,size_t(n),this,sizeof(*this)))return false;
  const uint8_t*p=nullptr;uint64_t bytes=0;
  switch(part){case 0:p=wire_;bytes=RequestBytes;break;case 1:p=qmd_;bytes=256;break;case 2:p=constants_;bytes=4096;break;
   case 3:p=command_;bytes=56;break;case 4:p=ring_;bytes=8;break;case 5:p=before_;bytes=captures_>=1?B::DeviceBytes:0;break;
   case 6:p=staged_;bytes=captures_>=2?B::DeviceBytes:0;break;case 7:p=after_;bytes=captures_>=3?B::DeviceBytes:0;break;
   case 8:p=journal_;bytes=uint64_t(observations_)*40;break;case 9:p=library_;bytes=512;break;case 10:p=code_;bytes=cursor_;break;default:return false;}
  if(off>bytes||n>bytes-off)return false;copy(out,p+off,size_t(n));return true;
 }
 Error begin(uint64_t session,const void*wire,size_t n){
  if(phase_!=Phase::Cold)return Error::State;if(!session||n!=512||!P::separate(wire,n,this,sizeof(*this)))return Error::Shape;
  copy(library_,wire,n);session_=session;phase_=Phase::Loading;return Error::None;
 }
 Error upload(uint64_t session,uint64_t off,const void*p,size_t n){
  if(phase_!=Phase::Loading)return Error::State;if(session!=session_)return Error::Scope;
  if(off!=cursor_||!n||n>4096-cursor_||!P::separate(p,n,this,sizeof(*this)))return Error::Shape;
  copy(code_+cursor_,p,n);cursor_+=unsigned(n);return Error::None;
 }
 Error seal(uint64_t session){
  if(phase_!=Phase::Loading)return Error::State;if(session!=session_)return Error::Scope;
  if(cursor_!=4096||!RTXProgram205::decode(library_,512,code_,4096,program205_)){error_=Error::Program;phase_=Phase::Retained;return error_;}
  sealed_=true;revision205_=1;phase_=Phase::Ready;return Error::None;
 }
 // Selection changes host-side program state only. The next submit still
 // proves the exact previous GPU image and ordered completion before writes.
 // The service workloop serializes this with submit, transfers and shutdown.
 void programInfo205(uint64_t registry,uint64_t*out)const{
  const uint64_t values[]={RTXProgram205::Magic,205,64,session_?session_:registry,revision205_,completed_,uint64_t(phase_),program205_.abi};
  for(unsigned i=0;i<8;++i)out[i]=values[i];
 }
 Error select205(uint64_t session,uint64_t completed,uint64_t revision,const void*image,size_t n){
  if(phase_!=Phase::Ready||!sealed_||!revision205_||revision205_==UINT64_MAX)return Error::State;
  if(session!=session_)return Error::Scope;
  if(completed!=completed_||revision!=revision205_)return Error::State;
  if(n!=RTXProgram205::PayloadBytes||!P::separate(image,n,this,sizeof(*this)))return Error::Shape;
  copy(selection205_,image,n);const auto*p=selection205_;RTXProgram205::Library next;
  if(!RTXProgram205::decode(p,512,p+512,4096,next))return Error::Program;
  // Validation and commit consume our preallocated snapshot, even if the
  // external-method input changes. Prior captures and counters survive;
  // capture parts9/10 now describe the selected revision, not the last job.
  copy(library_,p,512);copy(code_,p+512,4096);program205_=next;++revision205_;
  return Error::None;
 }
 template<class IO>Error submit(IO&io,S::Owner<64,16>&owner,const void*input,size_t n){
  if(phase_!=Phase::Ready||!sealed_||completed_==UINT64_MAX)return Error::State;
  if(n!=RequestBytes||!P::separate(input,n,this,sizeof(*this)))return Error::Shape;
  uint8_t snapshot[RequestBytes];copy(snapshot,input,n);Request checked;S::Plan next;
  if(!decode(snapshot,n,checked))return Error::Shape;
  if(checked.session!=session_)return Error::Scope;if(checked.serial!=completed_+1)return Error::State;
  if(!RTXProgram205::geometry(program205_,checked.program,checked.groups,checked.threads)||!io.ready())return Error::Program;
  if(!owner.acquire(session_,program205_.programs.programs[checked.program],checked.bindings,checked.count,checked.groups,checked.threads,next))return Error::Mapping;
  copy(wire_,snapshot,n);request_=checked;plan_=next;
  error_=Error::None;writes_=bells_=polls_=captures_=observations_=0;windowAttempted_=restored_=false;elapsed_=0;
  if(!build(owner)){owner.cancelPrepared(plan_.ticket);phase_=Phase::Retained;return error_=Error::Program;}
  if(!owner.markSubmitted(plan_.ticket)){phase_=Phase::Retained;return error_=Error::State;}
  // From here any uncertain native operation retains the pinned span job.
  phase_=Phase::Running;started_=io.nowNs();const bool done=work(io,owner);
  if(windowAttempted_){restored_=io.restoreWindow();if(!restored_)fail(Error::Restore);}
  if(done&&restored_&&error_==Error::None&&tick(io)&&mappingProof(io,owner,true)&&tick(io)){
   if(owner.completeVerified(plan_.ticket)){copy(canonical_,after_,B::DeviceBytes);canonicalReady_=true;completed_=request_.serial;phase_=Phase::Ready;return Error::None;}
   fail(Error::State);
  }
  owner.markFaulted(plan_.ticket);phase_=Phase::Retained;if(error_==Error::None)error_=Error::State;return error_;
 }
};
}
