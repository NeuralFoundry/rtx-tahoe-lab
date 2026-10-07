#pragma once
#include "ReusableProgram.hpp"

namespace RtxReusableBacking035 {
namespace N=RtxReusable035;namespace P=N::P;
constexpr unsigned RootBytes=12288,MaxChildren=45056,DeviceBytes=36864,ImageBytes=24576;
constexpr unsigned Image=12288,Data=Image+4096,Constant=Image+8192,Qmd=Image+12288,Fence=Image+20480;
constexpr uint64_t HostEntry=UINT64_C(0x1020001000)|(UINT64_C(1)<<41)|(UINT64_C(5)<<42);
constexpr uint32_t HostMarker=0x30602401;
inline bool equal(const void *a,const void *b,unsigned n){if(!a||!b)return false;const auto *x=static_cast<const uint8_t*>(a),*y=static_cast<const uint8_t*>(b);for(unsigned i=0;i<n;++i)if(x[i]!=y[i])return false;return true;}
inline void copy(uint8_t *a,const uint8_t *b,unsigned n){for(unsigned i=0;i<n;++i)a[i]=b[i];}
constexpr uint8_t guard(unsigned offset){return uint8_t((offset>=20480?0x5a:0xa5)^((offset*13+7)&255));}
inline uint8_t initialByte(unsigned i,const uint8_t *code){
 if(i<4096)return code[i];if(i>=8192&&i<16384)return 0;
 if(i>=20480&&i<20512)return 0;
 if(i>=20480&&i<20480+4*256&&(i-20480)%256<4)return 0;
 return guard(i);
}
inline bool image(const uint8_t *library,const uint8_t *code,uint8_t *out,size_t bytes){
 if(bytes!=ImageBytes||!P::separate(library,512,out,bytes)||!P::separate(code,4096,out,bytes))return false;
 P::Library lib;if(!P::decode(library,512,code,4096,lib))return false;
 for(unsigned i=0;i<ImageBytes;++i)out[i]=initialByte(i,code);return true;
}
struct Inputs {const uint8_t *root=nullptr,*children=nullptr,*hostRing=nullptr,*library=nullptr,*code=nullptr;unsigned childBytes=0;};
struct View {const uint8_t *root=nullptr,*children=nullptr,*device=nullptr;unsigned childBytes=0;};
enum class Phase:unsigned {Cold,Ready,Claimed,Published,Captured,Retained};
// Preallocated service-owned storage. The immutable input pointers are retained
// by the same native owner and may never reference caller or mutable MMIO bytes.
class Ledger {
 Inputs inputs_;uint64_t generation_=0,completed_=0;Phase phase_=Phase::Cold;
 unsigned writes_=0;bool staged_=false;
 N::Plan proof_;N::Request request_;uint8_t canonical_[DeviceBytes]={},candidate_[DeviceBytes]={};
 bool captureShape(const View &v)const{
  if(v.childBytes!=inputs_.childBytes)return false;
  const struct Span{const void *p;size_t n;} in[]={{this,sizeof(*this)},{inputs_.root,RootBytes},{inputs_.children,inputs_.childBytes},
   {inputs_.hostRing,4096},{inputs_.library,512},{inputs_.code,4096}},out[]={{v.root,RootBytes},{v.children,v.childBytes},{v.device,DeviceBytes}};
  for(unsigned i=0;i<3;++i){for(const auto &s:in)if(!P::separate(out[i].p,out[i].n,s.p,s.n))return false;
   for(unsigned j=0;j<i;++j)if(!P::separate(out[i].p,out[i].n,out[j].p,out[j].n))return false;}
  return true;
 }
 bool tables(const View &v)const{return captureShape(v)&&equal(v.root,inputs_.root,RootBytes)&&equal(v.children,inputs_.children,inputs_.childBytes);}
 static bool samePlan(const N::Plan &a,const N::Plan &b){
  const auto &x=a.launch,&y=b.launch;
  if(a.serial!=b.serial||a.entry!=b.entry||a.put!=b.put||x.program!=y.program||x.slot!=y.slot||x.invocations!=y.invocations||
   x.codeOffset!=y.codeOffset||x.codeBytes!=y.codeBytes||x.registers!=y.registers||x.parameters!=y.parameters||
   x.programVA!=y.programVA||x.constantVA!=y.constantVA||x.qmdVA!=y.qmdVA||x.fenceVA!=y.fenceVA||x.entry!=y.entry)return false;
  for(unsigned i=0;i<P::MaxBindings;++i)if(x.buffers[i]!=y.buffers[i])return false;
  return equal(a.data,b.data,2048)&&equal(a.constant,b.constant,4096)&&equal(a.qmd,b.qmd,256)&&equal(a.command,b.command,N::CommandBytes)&&equal(a.ringEntry,b.ringEntry,8);
 }
public:
 Ledger()=default;Ledger(const Ledger &)=delete;Ledger &operator=(const Ledger &)=delete;
 Phase phase()const{return phase_;}uint64_t completed()const{return completed_;}unsigned writes()const{return writes_;}
 bool seed(uint64_t generation,const Inputs &inputs,const View &readback){
  if(phase_!=Phase::Cold||!generation||inputs.childBytes<8192||inputs.childBytes>MaxChildren||inputs.childBytes%4096)return false;
  const struct Span{const void *p;size_t n;} spans[]={{inputs.root,RootBytes},{inputs.children,inputs.childBytes},{inputs.hostRing,4096},{inputs.library,512},{inputs.code,4096}};
  for(unsigned i=0;i<5;++i){if(!P::separate(spans[i].p,spans[i].n,this,sizeof(*this)))return false;
   for(unsigned j=0;j<i;++j)if(!P::separate(spans[i].p,spans[i].n,spans[j].p,spans[j].n))return false;}
  inputs_=inputs;if(!tables(readback))return false;
  P::Library lib;if(!P::decode(inputs.library,512,inputs.code,4096,lib))return false;
  if(P::get64(inputs.hostRing)!=HostEntry||!P::zero(inputs.hostRing,8,256)||P::get32(inputs.hostRing+0x888)!=1||P::get32(inputs.hostRing+0x88c)!=1||
     P::get32(inputs.hostRing+0x840)!=0x20001014||P::get32(inputs.hostRing+0x844)!=0x20001014)return false;
  if(!equal(readback.device,inputs.hostRing,4096))return false;
  const uint32_t host[]={0x20040004,0x10,0x20002000,HostMarker,0x01000002};
  for(unsigned i=0;i<5;++i)if(P::get32(readback.device+4096+i*4)!=host[i])return false;
  if(!P::zero(readback.device,4096+20,8192)||P::get32(readback.device+8192)!=HostMarker||!P::zero(readback.device,8196,12288))return false;
  for(unsigned i=0;i<ImageBytes;++i)if(readback.device[Image+i]!=initialByte(i,inputs.code))return false;
  copy(canonical_,readback.device,DeviceBytes);generation_=generation;phase_=Phase::Ready;return true;
 }
 bool begin(const N::Plan &plan,const N::Request &request,uint64_t completed,const View &before){
  if(phase_!=Phase::Ready||completed!=completed_||completed_==UINT64_MAX||plan.serial!=completed_+1||request.serial!=plan.serial||request.generation!=generation_||
    !P::separate(&plan,sizeof(plan),this,sizeof(*this))||!P::separate(&request,sizeof(request),this,sizeof(*this))||!tables(before)||!equal(before.device,canonical_,DeviceBytes))return false;
  if(!N::build(inputs_.library,inputs_.code,request,proof_)||!samePlan(plan,proof_))return false;
  request_=request;copy(candidate_,canonical_,DeviceBytes);writes_=0;staged_=false;phase_=Phase::Claimed;return true;
 }
 bool permitWrite(unsigned address,const uint8_t *data,unsigned n){
  if(phase_!=Phase::Claimed||writes_>=7||!P::separate(data,n,this,sizeof(*this)))return false;
  const unsigned addresses[]={N::DataPhysical,N::ConstantPhysical,N::QmdPhysical,N::FencePhysical,N::CommandPhysical,N::RingPhysical+proof_.entry*8,N::PutPhysical};
  const unsigned sizes[]={2048,1024,256,8,N::CommandBytes,8,4};
  const unsigned offsets[]={Data,Constant,Qmd,Fence,4096+64,proof_.entry*8,0x88c};
  uint8_t zero[8]={},put[4];P::Q::put32(put,proof_.put);
  const uint8_t *expected[]={proof_.data,proof_.constant,proof_.qmd,zero,proof_.command,proof_.ringEntry,put};
  if(address!=addresses[writes_]||n!=sizes[writes_]||(writes_==6&&!staged_)||!equal(data,expected[writes_],n))return false;
  const unsigned phase=writes_++; // Consume before the native write can take effect.
  copy(candidate_+offsets[phase],expected[phase],n);return true;
 }
 bool staged(const View &capture){
  if(phase_!=Phase::Claimed||writes_!=6||staged_||!tables(capture)||!equal(capture.device,candidate_,DeviceBytes))return false;
  staged_=true;return true;
 }
 bool permitNotify(){if(phase_!=Phase::Claimed||writes_!=7||!staged_)return false;phase_=Phase::Published;return true;}
 bool captured(const View &capture){
  if(phase_!=Phase::Published||!tables(capture))return false;const auto *actual=capture.device;
  if(P::get32(actual+0x888)!=proof_.put||P::get32(actual+0x88c)!=proof_.put||P::get32(actual+0x840)!=0x20001078||P::get32(actual+0x844)!=0x20001078||
     P::get64(actual+Fence)!=proof_.serial||P::get64(actual+Fence+16)!=proof_.serial)return false;
  P::Library lib;if(!P::decode(inputs_.library,512,inputs_.code,4096,lib))return false;const auto &program=lib.programs[request_.program];
  for(unsigned i=0;i<DeviceBytes;++i){
   bool allowed=(i>=Qmd&&i<Qmd+256)||(i>=0x840&&i<0x848)||(i>=0x888&&i<0x88c)||(i>=Fence&&i<Fence+8)||(i>=Fence+16&&i<Fence+24);
   if(i>=Data&&i<Data+2048){const unsigned local=i-Data,parameter=local/256;
    if(parameter<program.parameters&&(program.writeMask&(1u<<program.bindings[parameter]))&&local%256<proof_.launch.invocations*4)allowed=true;
   }
   if(!allowed&&actual[i]!=candidate_[i])return false;
  }
  // Freeze the observed QMD writeback and actual output. Future before-captures
  // compare these bytes exactly, rather than leaving old outputs mutable forever.
  copy(candidate_,actual,DeviceBytes);phase_=Phase::Captured;return true;
 }
 bool finish(const N::Result &result){
  if(phase_!=Phase::Captured||!result.passed||result.failure!=N::Failure::None||!result.attempted||!result.restored||result.serial!=proof_.serial||
     result.writes!=7||result.notifications!=1){phase_=Phase::Retained;return false;}
  copy(canonical_,candidate_,DeviceBytes);completed_=proof_.serial;phase_=Phase::Ready;return true;
 }
 // A replacement preserves the queue, data and all page tables. The owner
 // commits new metadata only after code readback and BAR window restoration.
 bool replacementBefore(uint64_t completed,const View &v)const{
  return phase_==Phase::Ready&&completed==completed_&&tables(v)&&equal(v.device,canonical_,DeviceBytes);
 }
 bool replacementStaged(uint64_t completed,const View &v,const uint8_t *code)const{
  if(phase_!=Phase::Ready||completed!=completed_||!tables(v)||!P::separate(code,4096,this,sizeof(*this)))return false;
  for(unsigned i=0;i<DeviceBytes;++i)if(v.device[i]!=(i>=Image&&i<Image+4096?code[i-Image]:canonical_[i]))return false;
  return true;
 }
 bool replacementCommit(uint64_t completed,const View &v,const uint8_t *code){
  if(!replacementStaged(completed,v,code))return false;copy(canonical_,v.device,DeviceBytes);return true;
 }
 void retain(){if(phase_!=Phase::Cold)phase_=Phase::Retained;}
};
}
