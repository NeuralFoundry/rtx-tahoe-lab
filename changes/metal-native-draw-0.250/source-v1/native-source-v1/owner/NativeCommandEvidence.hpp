#pragma once
#include "ReusableProgram.hpp"
#include "ReusableBacking.hpp"
#include "LibraryUpload.hpp"
#include "changes/gsp-channel-0.23/GMMULeaves.hpp"
#include <array>
#include <cstring>

// Host-side verification of same-connection snapshots, with no shader output
// evaluator. Output values are independently checked by the application test.
// Completion and capture verification do not authorize releasing kernel DMA.
namespace RTXNativeEvidence036 {
namespace N=RtxReusable035;namespace P=N::P;namespace B=RtxReusableBacking035;
struct Bytes {const uint8_t *data=nullptr;size_t size=0;};
inline bool shape(Bytes b,size_t n){return b.data&&b.size==n;}
inline bool equal(const uint8_t *a,const uint8_t *b,size_t n){return a&&b&&!std::memcmp(a,b,n);}
inline uint64_t word(Bytes b,unsigned i){return P::get64(b.data+i*8);}
inline bool childSize(uint64_t n){return n>=8192&&n<=45056&&n%4096==0;}
inline bool runtime(Bytes raw,uint64_t generation,uint64_t completed,const P::Library &lib,unsigned &children,unsigned capture=0){
 if(!shape(raw,512)||!generation||!lib.count||lib.count>4)return false;
 const auto v=[&](unsigned i){return word(raw,i);};const auto child=v(35);
 if(!childSize(child))return false;
 const unsigned phase=completed==UINT64_MAX?4:1,ready=completed==UINT64_MAX?0:1;
 const struct Field{unsigned index;uint64_t value;} fields[]={
  {0,UINT64_C(0x5254585254493335)},{1,1},{2,generation},{3,phase},{4,completed},{5,32},{6,2112},
  {7,1},{8,1},{9,1},{10,18},{11,1},{12,1},{13,6},{14,1},{15,ready},{16,completed},{17,!ready},
  {18,0},{19,lib.count},{20,512},{21,4096},{22,24576},{23,4096},{24,0},{25,1},{26,1},
  {27,12+child/4096},{28,49152+child},{31,1},{32,capture?capture:(completed?4u:1u)},{33,completed},{34,1}};
 for(auto f:fields)if(v(f.index)!=f.value)return false;
 if(v(30)>=N::BudgetNs)return false;
 for(unsigned i=36;i<64;++i)if(v(i))return false;
 children=unsigned(child);return true;
}
inline bool upload(Bytes raw,uint64_t generation,Bytes payload,unsigned phase){
 if(!shape(raw,256)||!shape(payload,4608)||(phase!=2&&phase!=3))return false;
 P::Library lib;if(!P::decode(payload.data,512,payload.data+512,4096,lib)||!RtxLibraryUpload036::profile(lib))return false;
 if(P::get64(raw.data)!=RtxLibraryUpload036::InfoMagic||P::get32(raw.data+8)!=1||P::get32(raw.data+12)!=256||P::get64(raw.data+16)!=generation||!generation)return false;
 const uint32_t fields[]={phase,4608,5,0,4608,1024,5,64,1,0};
 for(unsigned i=0;i<10;++i)if(P::get32(raw.data+24+i*4)!=fields[i])return false;
 uint8_t digest[32];GSPDigest::SHA256 hash;hash.update(payload.data,4608);hash.finish(digest);
 return equal(digest,raw.data+64,32)&&equal(digest,raw.data+96,32)&&P::get32(raw.data+128)==lib.count&&P::get32(raw.data+132)==lib.usedCodeBytes&&P::zero(raw.data,136,256);
}
inline bool job(Bytes raw,uint64_t generation,const N::Request &request,unsigned children){
 if(!shape(raw,1024)||!childSize(children))return false;
 const auto v=[&](unsigned i){return word(raw,i);};const auto serial=request.serial;
 const struct Field{unsigned index;uint64_t value;} fields[]={
  {0,UINT64_C(0x52545852544a3335)},{1,1},{2,generation},{3,serial},{4,serial==UINT64_MAX?4u:1u},
  {5,serial},{6,serial},{7,0},{8,1},{9,1},{10,0},{11,7},{12,1},{17,1},
  {20,1},{21,1},{22,1},{23,1},{24,1},{25,1},{26,1},{28,0},
  {40,4},{41,serial},{42,1},{43,1},{44,12+children/4096},{45,49152+children},{46,0x0340e000},
  {50,1},{51,1},{52,1},{60,request.program},{61,request.groups},{62,serial&31},{63,((serial&31)+1)&31}};
 bool present[128]={};for(auto f:fields){present[f.index]=true;if(v(f.index)!=f.value)return false;}
 const unsigned varying[]={13,14,15,16,27,29,30,31,32,33,47,48};for(auto i:varying)present[i]=true;
 for(unsigned i=0;i<128;++i)if(!present[i]&&v(i))return false;
 return v(13)>0&&v(13)<=N::MaxOperations&&v(14)>0&&v(14)<=v(13)&&v(15)<N::BudgetNs&&
  v(27)<=UINT32_MAX&&v(27)==v(29)&&v(31)<N::BudgetNs&&v(33)<N::BudgetNs&&v(48)<N::BudgetNs;
}
inline bool plan(const N::Request &request,Bytes payload,std::array<uint8_t,4096> &out){
 if(!shape(payload,4608))return false;N::Plan p;if(!N::build(payload.data,payload.data+512,request,p))return false;
 out.fill(0);auto *b=out.data();P::Q::put64(b,UINT64_C(0x52545852504c3335));P::Q::put32(b+8,1);P::Q::put32(b+12,4096);P::Q::put64(b+16,request.generation);P::Q::put64(b+24,request.serial);
 const unsigned fields[]={request.program,request.groups,p.entry,p.put,p.launch.codeOffset,p.launch.codeBytes,p.launch.invocations,p.launch.parameters};
 for(unsigned i=0;i<8;++i)P::Q::put32(b+32+i*4,fields[i]);
 std::memcpy(b+64,p.data,2048);std::memcpy(b+2112,p.constant,1024);std::memcpy(b+3136,p.qmd,256);std::memcpy(b+3392,p.command,56);std::memcpy(b+3448,p.ringEntry,8);return true;
}
class Proof {
 std::array<uint8_t,4608> payload_{};P::Library library_;uint64_t generation_=0,completed_=0;
 unsigned childBytes_=0;bool ready_=false,pending_=false,retired_=false,residentCapture_=false;
 std::array<uint8_t,12288> root_{};std::array<uint8_t,45056> children_{};
 std::array<uint8_t,36864> previous_{},candidate_{};
 std::array<uint8_t,2112> wire_{};std::array<uint8_t,4096> plan_{};N::Request request_;
public:
 Proof()=default;Proof(const Proof&)=delete;Proof&operator=(const Proof&)=delete;
 bool ready()const{return ready_&&!retired_&&!pending_;}bool pending()const{return pending_;}
 uint64_t completed()const{return completed_;}uint64_t generation()const{return generation_;}unsigned childrenBytes()const{return childBytes_;}
 const P::Library &library()const{return library_;}
 const std::array<uint8_t,4096> &expectedPlan()const{return plan_;}
 bool checkRuntime(Bytes info)const{
  unsigned count=0;return ready()&&runtime(info,generation_,completed_,library_,count,residentCapture_?5:0)&&count==childBytes_;
 }
 // Replacement is admitted only between retired jobs. The complete preceding
 // physical ledger is preserved except for the exact owned code page.
 bool validateReplacement(Bytes info,Bytes root,Bytes children,Bytes device,Bytes payload)const{
  if(!ready()||!shape(root,12288)||!shape(children,childBytes_)||!shape(device,36864)||!shape(payload,4608))return false;
  P::Library next;unsigned count=0;
  if(!P::decode(payload.data,512,payload.data+512,4096,next)||!RtxLibraryUpload036::profile(next)||
     !runtime(info,generation_,completed_,next,count,5)||count!=childBytes_||!equal(root.data,root_.data(),12288)||!equal(children.data,children_.data(),childBytes_))return false;
  for(unsigned i=0;i<36864;++i)if(device.data[i]!=(i>=B::Image&&i<B::Image+4096?payload.data[512+i-B::Image]:previous_[i]))return false;
  const auto nextIndex=N::nextIndex(completed_);
  return P::get32(device.data+0x888)==nextIndex&&P::get32(device.data+0x88c)==nextIndex&&P::get64(device.data+B::Fence)==completed_&&P::get64(device.data+B::Fence+16)==completed_;
 }
 bool acceptReplacement(Bytes info,Bytes root,Bytes children,Bytes device,Bytes payload){
  if(!validateReplacement(info,root,children,device,payload))return false;
  P::Library next;if(!P::decode(payload.data,512,payload.data+512,4096,next))return false;
  std::memcpy(payload_.data(),payload.data,4608);std::memcpy(previous_.data(),device.data,36864);library_=next;residentCapture_=true;return true;
 }
 void retire(){retired_=true;ready_=false;pending_=false;}
 bool seed(uint64_t generation,Bytes payload,Bytes info,Bytes root,Bytes children,Bytes device,Bytes executionRoot,Bytes executionChildren,Bytes executionDevice){
  if(ready_||retired_||generation_||!generation||!shape(payload,4608)||!shape(root,12288)||!shape(executionRoot,12288)||!shape(device,36864)||!shape(executionDevice,12288))return false;
  P::Library lib;unsigned count=0;
  if(!P::decode(payload.data,512,payload.data+512,4096,lib)||!RtxLibraryUpload036::profile(lib)||!runtime(info,generation,0,lib,count)||!shape(children,count)||!shape(executionChildren,count))return false;
  if(!equal(root.data,executionRoot.data,12288)||!equal(device.data,executionDevice.data,12288))return false;
  for(unsigned i=0;i<count;++i){
   uint8_t expected=executionChildren.data[i];
   if(i>=4128&&i<4176){unsigned page=(i-4128)/8,byte=(i-4128)%8;uint64_t pte=(UINT64_C(6)<<56)|((0x03409000+page*4096)>>4)|1;
    if(executionChildren.data[i])return false;expected=uint8_t(pte>>(byte*8));}
   if(children.data[i]!=expected)return false;
  }
  for(unsigned page=0;page<6;++page)for(unsigned edge:{0u,4095u}){
   bool mapped=false;uint64_t physical=0;
   if(!GMMULeaves::walk(root.data,root.size,children.data,children.size,UINT64_C(0x1020004000)+page*4096+edge,mapped,physical)||!mapped||physical!=0x03409000+page*4096+edge)return false;
  }
  if(P::get64(device.data)!=B::HostEntry||!P::zero(device.data,8,256)||P::get32(device.data+0x840)!=0x20001014||P::get32(device.data+0x844)!=0x20001014||P::get32(device.data+0x888)!=1||P::get32(device.data+0x88c)!=1)return false;
  const uint32_t host[]={0x20040004,0x10,0x20002000,0x30602401,0x01000002};
  for(unsigned i=0;i<5;++i)if(P::get32(device.data+4096+i*4)!=host[i])return false;
  if(!P::zero(device.data,4116,8192)||P::get32(device.data+8192)!=0x30602401||!P::zero(device.data,8196,12288))return false;
  for(unsigned i=0;i<24576;++i)if(device.data[12288+i]!=B::initialByte(i,payload.data+512))return false;
  std::memcpy(payload_.data(),payload.data,4608);std::memcpy(root_.data(),root.data,12288);std::memcpy(children_.data(),children.data,count);std::memcpy(previous_.data(),device.data,36864);
  generation_=generation;library_=lib;childBytes_=count;ready_=true;return true;
 }
 bool begin(Bytes wire,Bytes info){
  if(!ready()||completed_==UINT64_MAX||!shape(wire,2112))return false;
  N::Request request;unsigned count=0;
  if(!N::decode(wire.data,wire.size,library_,request)||request.generation!=generation_||request.serial!=completed_+1||!runtime(info,generation_,completed_,library_,count,residentCapture_?5:0)||count!=childBytes_)return false;
  std::array<uint8_t,4096> planned{};if(!plan(request,{payload_.data(),payload_.size()},planned))return false;
  request_=request;plan_=planned;std::memcpy(wire_.data(),wire.data,2112);candidate_=previous_;
  auto *b=candidate_.data();const auto *p=planned.data();
  if(P::get32(b+0x888)!=(request.serial&31)||P::get32(b+0x88c)!=(request.serial&31)||P::get64(b+B::Fence)!=completed_||P::get64(b+B::Fence+16)!=completed_)return false;
  std::memcpy(b+B::Data,p+64,2048);std::memcpy(b+B::Constant,p+2112,1024);std::memcpy(b+B::Qmd,p+3136,256);std::memcpy(b+4160,p+3392,56);std::memcpy(b+(request.serial&31)*8,p+3448,8);
  P::Q::put32(b+0x840,0x20001078);P::Q::put32(b+0x844,0x20001078);P::Q::put32(b+0x888,((request.serial&31)+1)&31);P::Q::put32(b+0x88c,((request.serial&31)+1)&31);
  P::Q::put64(b+B::Fence,request.serial);P::Q::put64(b+B::Fence+16,request.serial);pending_=true;return true;
 }
 // Validation is read-only. Only accept() advances the epoch; callers can save
 // diagnostics after rejection, then retire without accepting or replaying.
 bool validate(Bytes info,Bytes jobInfo,Bytes root,Bytes children,Bytes device,Bytes wire,Bytes planCapture,Bytes payload)const{
  if(!pending_||retired_||!shape(root,12288)||!shape(children,childBytes_)||!shape(device,36864)||!shape(wire,2112)||!shape(planCapture,4096)||!shape(payload,4608))return false;
  unsigned count=0;if(!runtime(info,generation_,request_.serial,library_,count)||count!=childBytes_||!job(jobInfo,generation_,request_,count))return false;
  if(!equal(root.data,root_.data(),root.size)||!equal(children.data,children_.data(),children.size)||!equal(wire.data,wire_.data(),wire.size)||!equal(planCapture.data,plan_.data(),planCapture.size)||!equal(payload.data,payload_.data(),payload.size))return false;
  const auto &program=library_.programs[request_.program];const unsigned invocations=request_.groups*program.localX;
  for(unsigned i=0;i<36864;++i){
   // GPU may write back QMD state. It must still match the separate immutable
   // submitted-plan capture above. Future jobs freeze this observed writeback.
   bool allowed=i>=B::Qmd&&i<B::Qmd+256;
   if(i>=B::Data&&i<B::Data+2048){unsigned local=i-B::Data,parameter=local/256;
    if(parameter<program.parameters&&(program.writeMask&(1u<<program.bindings[parameter]))&&local%256<invocations*4)allowed=true;
   }
   if(!allowed&&device.data[i]!=candidate_[i])return false;
  }
  return true;
 }
 bool accept(Bytes info,Bytes jobInfo,Bytes root,Bytes children,Bytes device,Bytes wire,Bytes planCapture,Bytes payload,std::array<uint8_t,2048> &result){
  if(!validate(info,jobInfo,root,children,device,wire,planCapture,payload))return false;
  std::memcpy(previous_.data(),device.data,36864);std::memcpy(result.data(),device.data+B::Data,2048);completed_=request_.serial;pending_=false;residentCapture_=false;return true;
 }
};
}
