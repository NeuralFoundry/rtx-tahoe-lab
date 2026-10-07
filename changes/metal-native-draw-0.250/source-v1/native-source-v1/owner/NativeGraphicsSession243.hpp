#pragma once
#include "NativeDataSession182.hpp"
#include "../probe/kernel/gpu242/GraphicsABI242.hpp"
#include <memory>
#include "RTXDrawTransfer248.hpp"

// Private fixed-triangle bring-up on the existing, serialized native connection.
// A successful transaction proves queue/fence/material/guard evidence. Pixel
// semantics still require the independent raster oracle and real GPU execution.
namespace RTXNativeGraphics243 {
namespace R=RTXNativeRoot196;namespace L=RTXGraphicsLayout242;
namespace G=RTXGraphicsSubmit241;namespace A=RTXGraphicsABI242;
namespace Q=RTXGraphicsRequest240;namespace C=RTXGraphicsCommands240;
namespace P=RtxProgram164;namespace B=RtxReusableBacking035;
namespace D=RTXDrawTransfer248;
using Bytes=std::vector<uint8_t>;using Info=std::array<uint8_t,256>;
constexpr uint64_t BudgetNs=30000000000ULL;
constexpr unsigned ColorBytes=24849,Width=64,Height=64,Pitch=384,Offset=256;
enum class Failure {None,State,Clock,Call,Save,Info,Mapping,Capture,Journal,Data,Hash};
class Session {
 enum class Phase {Cold,Preparing,Ready,Executing,Failed};
 Phase phase=Phase::Cold;Failure error=Failure::None;
 uint64_t generation=0,completed_=0,ticket_=0,start=0,elapsed_=0,totalCalls_=0;
 unsigned calls_=0;Info lastInfo{};std::array<uint8_t,B::DeviceBytes>canonical{};
 std::array<uint64_t,L::Buffers>handles{};std::array<RTXSpans165::Mapping,L::Buffers>maps{};
 Session(const Session&)=delete;Session&operator=(const Session&)=delete;
 bool fail(Failure f){if(error==Failure::None)error=f;phase=Phase::Failed;return false;}
 template<class IO>bool tick(IO&io){const auto now=io.nowNs();if(!start||now<start||now-start<elapsed_||now-start>=BudgetNs)return fail(Failure::Clock);elapsed_=now-start;return true;}
 template<class IO>bool call(IO&io,unsigned sel,const uint64_t*sc,unsigned count,const uint8_t*in,size_t n,uint8_t*out,size_t cap){
  if(!tick(io)||calls_>=192||totalCalls_==UINT64_MAX)return fail(Failure::Clock);++calls_;++totalCalls_;
  if(!io.call(sel,sc,count,in,n,out,cap))return fail(Failure::Call);return tick(io);
 }
 template<class Sink>bool save(Sink&sink,const char*name,const uint8_t*p,size_t n){return sink.save(name,p,n)||fail(Failure::Save);}
 template<class IO,class Sink>bool info(IO&io,Sink&sink,const char*name,Info&out){return call(io,A::Info,nullptr,0,nullptr,0,out.data(),out.size())&&save(sink,name,out.data(),out.size());}
 template<class IO,class Sink>bool capture(IO&io,Sink&sink,unsigned part,const char*name,Bytes&out,size_t n){
  out.resize(n);for(size_t off=0;off<n;off+=4096){const auto bytes=std::min<size_t>(4096,n-off);const uint64_t sc[]={generation,part,off,bytes};
   if(!call(io,A::Capture,sc,4,nullptr,0,out.data()+off,bytes))return false;
  }return save(sink,name,out.data(),out.size());
 }
 static uint64_t word(const Info&raw,unsigned i){return R::u64(raw.data()+i*8);}
 bool completedInfo(const Info&raw,const C::Output&expected,uint64_t serial,unsigned width,unsigned height,bool application)const{
  const auto count=word(raw,11);const auto index=P::get32(canonical.data()+0x888),next=(index+1)&31;
  const uint64_t fixed[]={G::Magic,241,generation,3,0,serial,serial,0,7,1,0,0,0,1,3,1,
   expected.ringEntry,expected.fenceAddress,1201,serial,index,next,ColorBytes,width,height,Pitch,5,8192,4804,application?320u:0u,application?249u:0u,0};
  for(unsigned i=0;i<32;++i)if(i!=7&&i!=10&&i!=11&&i!=12&&word(raw,i)!=fixed[i])return false;
  return index<32&&P::get32(canonical.data()+0x88c)==index&&word(raw,7)>ticket_&&count>=5&&count<=8192&&word(raw,10)==count-4&&word(raw,12)<G::BudgetNs;
 }
 bool journal(const Bytes&raw,const Info&info,uint64_t serial)const{
  if(raw.size()!=word(info,11)*48)return false;
  const auto count=raw.size()/48;const auto old=word(info,20),next=word(info,21);uint64_t prior=0,initialFence=0;
  for(size_t i=0;i<count;++i){const auto*p=raw.data()+i*48;const auto stage=R::u64(p),time=R::u64(p+8),queue=R::u64(p+16),qmd=R::u64(p+24),host=R::u64(p+32),fence=R::u64(p+40);
   if(stage!=(i<3?i+1:i+1==count?5:4)||time<prior||time>word(info,12)||qmd||host)return false;prior=time;
   const auto get=uint32_t(queue),put=uint32_t(queue>>32);
   if(i<3){if(get!=old||put!=old||fence>UINT32_MAX)return false;if(!i)initialFence=fence;if(i==1&&fence!=initialFence)return false;if(i==2&&fence)return false;}
   else{if((get!=old&&get!=next)||put!=next||(fence&&fence!=serial))return false;
    const bool done=get==next&&fence==serial;if(i+2>=count){if(!done)return false;}else if(done)return false;}
  }return true;
 }
 static std::array<uint8_t,64>hashes(const Bytes&image,unsigned width,unsigned height){
  std::array<uint8_t,64>out{};GSPDigest::SHA256 full,guards;full.update(image.data(),unsigned(image.size()));full.finish(out.data());
  guards.update(image.data(),Offset);for(unsigned y=0;y<height;++y)guards.update(image.data()+Offset+y*Pitch+width*4,Pitch-width*4);
  guards.update(image.data()+Offset+Pitch*height,ColorBytes-Offset-Pitch*height);guards.finish(out.data()+32);return out;
 }
public:
 Session()=default;
 bool active()const{return phase!=Phase::Cold;}bool ready()const{return phase==Phase::Ready;}bool failed()const{return phase==Phase::Failed;}
 Failure failure()const{return error;}uint64_t completed()const{return completed_;}uint64_t calls()const{return totalCalls_;}uint64_t elapsed()const{return elapsed_;}
 void invalidate(){fail(Failure::State);}
 template<class IO,class Sink,class Data,class Bootstrap>bool begin(IO&io,Sink&sink,const R::Capture&root,const Data&data,const Bootstrap&bootstrap){
  if(active()||L::ABI!=242||!root.passed||root.handles.size()!=L::Buffers*64||!data.ready()||data.session()!=root.info.w[2]||!bootstrap.ready()||bootstrap.completed())return false;
  try{
   R::Info checked;if(!R::decode(root.before.data(),512,data.session(),checked)||checked.w!=root.info.w||root.before!=root.after||!bootstrap.copyPristineDevice(canonical))return false;
   for(unsigned slot=0;slot<L::Buffers;++slot){const auto&d=L::Description[slot];const uint64_t want[]={data.session(),(1ULL<<32)|(slot+1),slot+1,slot+1,L::address(slot),d.bytes,L::mapped(slot),d.access};
    for(unsigned f=0;f<8;++f)if(R::u64(root.handles.data()+slot*64+f*8)!=want[f])return false;
    handles[slot]=want[1];maps[slot]={want[2],want[3],want[4],want[5],want[6],uint32_t(want[7])};if(data.capacity(slot)!=d.bytes)return false;
   }
   if(P::get32(canonical.data()+0x888)>=32||P::get32(canonical.data()+0x888)!=P::get32(canonical.data()+0x88c)||P::get64(canonical.data()+B::Fence)||P::get64(canonical.data()+B::Fence+16))return false;
   phase=Phase::Preparing;generation=data.session();start=io.nowNs();elapsed_=0;calls_=0;
   Info cold{};uint64_t expected[32]{};A::coldInfo(generation,expected);for(unsigned n=0;n<32;++n)P::Q::put64(cold.data()+n*8,expected[n]);
   if(!info(io,sink,"graphics-cold.bin",lastInfo)||lastInfo!=cold)return fail(Failure::Info);
   if(!save(sink,"baseline.bin",canonical.data(),canonical.size())||!tick(io))return false;
   phase=Phase::Ready;return true;
  }catch(...){return fail(Failure::State);}
 }
 // Fixed resources and shader; caller receives no image or completion until
 // full captures, readback hashes, stable info and all evidence saves succeed.
 template<class IO,class Tree,class Data>bool draw(IO&io,Tree&sinks,Data&data,Bytes&image,uint64_t&completion,const Bytes*applicationRequest=nullptr){
  completion=0;if(!ready()||!data.ready()||data.session()!=generation||completed_==UINT32_MAX)return false;
  try{
   D::Plan app;const bool application=applicationRequest!=nullptr;
   if(application){D::Digest digest{};GSPDigest::SHA256 sha;sha.update(C::T::Program,4096);sha.finish(digest.data());
    if(!D::decode(applicationRequest->data(),applicationRequest->size(),generation,completed_+1,digest,app)||app.vertexCount!=3||app.width>64||app.height>64||
       !Q::validVertices249(applicationRequest->data()+D::Header+app.vertexBegin))return false;
   }
   const unsigned width=application?app.width:Width,height=application?app.height:Height;
   Bytes seed(ColorBytes),readback;for(unsigned i=0;i<ColorBytes;++i)seed[i]=uint8_t(i*29+7);
   if(application)for(unsigned y=0;y<height;++y)std::memcpy(seed.data()+Offset+y*Pitch,applicationRequest->data()+D::Header+app.vertexBytes+app.colorOffset+uint64_t(y)*app.pitch,width*4);
   auto local=std::make_unique<RTXSpans165::Owner<64,16>>(generation,L::Begin,L::End);
   for(unsigned i=0;i<L::Buffers;++i){uint64_t h=0;if(!local->admitMapping(maps[i],h)||h!=handles[i])return fail(Failure::Mapping);}
   Q::Request request;request.session=generation;request.serial=completed_+1;request.width=width;request.height=height;request.pitch=Pitch;request.application=application;
   if(application)std::memcpy(request.vertices,applicationRequest->data()+D::Header+app.vertexBegin,48);
   const uint64_t offsets[]={0,0,Offset,0,0},sizes[]={4096,48,Pitch*height,16,8192};Bytes wire(application?320:256,0);
   P::Q::put64(wire.data(),Q::Magic);P::Q::put32(wire.data()+8,application?249:240);P::Q::put32(wire.data()+12,uint32_t(wire.size()));P::Q::put64(wire.data()+16,generation);P::Q::put64(wire.data()+24,request.serial);
   P::Q::put32(wire.data()+32,width);P::Q::put32(wire.data()+36,height);P::Q::put32(wire.data()+40,Pitch);P::Q::put32(wire.data()+44,application?3:2);if(application)std::memcpy(wire.data()+256,request.vertices,48);
   for(unsigned i=0;i<5;++i){request.bindings[i]={handles[i+4],offsets[i],sizes[i],i};auto*p=wire.data()+64+i*32;
    P::Q::put64(p,handles[i+4]);P::Q::put64(p+8,offsets[i]);P::Q::put64(p+16,sizes[i]);P::Q::put32(p+24,i);}
   RTXSpans165::Plan plan{};auto expected=std::make_unique<C::Output>();
   if(!local->acquireGraphics(generation,request.bindings,5,plan)||!C::build(request,maps.data()+4,plan,*expected)||!local->cancelPrepared(plan.ticket))return fail(Failure::Mapping);
   phase=Phase::Executing;start=io.nowNs();elapsed_=0;calls_=0;Info before{},after{},stable{};
   if(!info(io,sinks,"graphics-before.bin",before)||before!=lastInfo)return fail(Failure::Info);
   if(!sinks.part("seed",[&](auto&sink){return data.transfer(io,sink,6,0,seed.data(),seed.size(),readback,true);}))return fail(Failure::Data);
   if(!save(sinks,"submitted-request.bin",wire.data(),wire.size())||!call(io,A::Submit,nullptr,0,wire.data(),wire.size(),nullptr,0))return false;
   if(!info(io,sinks,"graphics-info.bin",after)||!completedInfo(after,*expected,request.serial,width,height,application))return fail(Failure::Info);
   const char*names[]={"request.bin","commands.bin","program.bin","vertices.bin","before.bin","staged.bin","after.bin","observations.bin","hashes.bin"};
   const size_t sizesOut[]={wire.size(),8192,4096,48,B::DeviceBytes,B::DeviceBytes,B::DeviceBytes,size_t(word(after,11))*48,128};
   std::array<Bytes,9>raw;for(unsigned i=0;i<9;++i)if(!capture(io,sinks,i,names[i],raw[i],sizesOut[i]))return false;
   if(!G::equal(raw[0].data(),wire.data(),wire.size())||!G::equal(raw[1].data(),expected->commands,8192)||!G::equal(raw[2].data(),expected->program,4096)||!G::equal(raw[3].data(),expected->vertices,48)||!G::equal(raw[4].data(),canonical.data(),canonical.size()))return fail(Failure::Capture);
   Bytes control(canonical.begin(),canonical.end());P::Q::put64(control.data()+word(after,20)*8,expected->ringEntry);if(control!=raw[5])return fail(Failure::Capture);
   const auto next=uint32_t(word(after,21));const auto end=maps[8].gpuVA+4804;
   P::Q::put32(control.data()+0x888,next);P::Q::put32(control.data()+0x88c,next);P::Q::put32(control.data()+0x840,uint32_t(end));P::Q::put32(control.data()+0x844,uint32_t(end));
   for(unsigned off:{0x84cu,0x860u})P::Q::put32(control.data()+off,(P::get32(control.data()+off)&~255u)|uint32_t(end>>32));
   if(control!=raw[6])return fail(Failure::Capture);if(!journal(raw[7],after,request.serial))return fail(Failure::Journal);
   if(!sinks.part("readback",[&](auto&sink){return data.transfer(io,sink,6,0,nullptr,ColorBytes,readback,false);})||readback.size()!=ColorBytes)return fail(Failure::Data);
   const auto seedHash=hashes(seed,width,height),resultHash=hashes(readback,width,height);
   if(!G::equal(seedHash.data(),raw[8].data(),32)||!G::equal(resultHash.data(),raw[8].data()+32,32)||!G::equal(seedHash.data()+32,raw[8].data()+64,32)||!G::equal(resultHash.data()+32,raw[8].data()+96,32)||!G::equal(seedHash.data()+32,resultHash.data()+32,32))return fail(Failure::Hash);
   if(!info(io,sinks,"graphics-stable.bin",stable)||stable!=after||!tick(io))return fail(Failure::Info);
   std::copy(raw[6].begin(),raw[6].end(),canonical.begin());lastInfo=stable;ticket_=word(after,7);completed_=request.serial;phase=Phase::Ready;
   if(application){Bytes payload(applicationRequest->begin()+D::Header,applicationRequest->end());
    for(unsigned y=0;y<height;++y)std::memcpy(payload.data()+app.vertexBytes+app.colorOffset+uint64_t(y)*app.pitch,readback.data()+Offset+y*Pitch,width*4);
    if(!D::readback(app,*applicationRequest,payload.data(),payload.size(),completed_))return fail(Failure::Data);image.swap(payload);
   }else image.swap(readback);
   completion=completed_;return true;
  }catch(...){return fail(Failure::State);}
 }
};
}
