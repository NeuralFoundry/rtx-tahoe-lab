#pragma once
#include "../root179/OwnedBufferSpans165.hpp"

// Serialized, post-root-ACK access to native-owned data. Backend.ready must
// verify the native runtime is idle, its owner is held and its root is bound.
// No caller-provided physical addresses or trusted completion callbacks.
namespace RTXDataAccess181 {
constexpr uint64_t Magic=0x5254584441544131ULL;
enum class Phase:uint64_t {Cold,Ready,Active,Retained};
enum class Error:uint64_t {None,Shape,Scope,Busy,Mapping,Stability,Write,Publish,Import,Read,State};
inline bool same(const RTXSpans165::Mapping&a,const RTXSpans165::Mapping&b){
 return a.allocation==b.allocation&&a.mapping==b.mapping&&a.gpuVA==b.gpuVA&&
  a.logicalBytes==b.logicalBytes&&a.mappedBytes==b.mappedBytes&&a.access==b.access;
}
class State {
 Phase phase_=Phase::Cold;Error error_=Error::None;
 uint64_t session_=0,sequence_=0,uploads_=0,downloads_=0;
 State(const State&)=delete;State&operator=(const State&)=delete;
 Error fault(Error e){phase_=Phase::Retained;error_=e;return e;}
 template<class Owner,class IO>Error resolve(Owner&owner,IO&io,uint64_t session,uint64_t handle,RTXSpans165::Mapping&m,uint32_t&index){
  if(phase_!=Phase::Ready||sequence_==UINT64_MAX)return Error::State;
  if(!session||session!=session_)return Error::Scope;
  if(!io.ready())return Error::Busy;
  uint32_t refs=0;if(!owner.inspect(handle,m,refs))return Error::Scope;if(refs)return Error::Busy;
  if(!m.allocation||m.allocation>64||m.mapping!=m.allocation)return Error::Mapping;
  index=uint32_t(m.allocation-1);RTXSpans165::Mapping expected{};uint64_t scope=0;
  if(!io.mapping(index,scope,expected)||scope!=session_||!same(m,expected))return Error::Mapping;
  return Error::None;
 }
public:
 State()=default;
 bool activate(uint64_t session){if(phase_!=Phase::Cold||!session)return false;session_=session;phase_=Phase::Ready;return true;}
 bool retained()const{return phase_==Phase::Retained;}
 void info(uint64_t registry,uint64_t*out)const{
  const uint64_t words[]={Magic,181,session_?session_:registry,uint64_t(phase_),sequence_,uploads_,downloads_,uint64_t(error_)};
  for(unsigned i=0;i<8;++i)out[i]=words[i];
 }
 template<class Owner,class IO>Error transfer(Owner&owner,IO&io,bool writing,uint64_t session,uint64_t handle,uint64_t offset,
    const void*input,void*output,uint64_t bytes){
  if(!bytes||bytes>4096||(writing?(!input||output):(!output||input))||
     !RtxProgram164::separate(writing?input:output,size_t(bytes),this,sizeof(*this)))return Error::Shape;
  RTXSpans165::Mapping mapping{};uint32_t index=0;auto e=resolve(owner,io,session,handle,mapping,index);if(e!=Error::None)return e;
  if(!RTXSpans165::span(offset,bytes,mapping.logicalBytes))return Error::Shape;
  if(!io.stable(index,offset,bytes))return fault(Error::Stability);
  phase_=Phase::Active;++sequence_;
  if(writing){
   if(!io.write(index,offset,input,bytes))return fault(Error::Write);
   if(!io.publish(index))return fault(Error::Publish);
  }else{
   if(!io.import(index))return fault(Error::Import);
   if(!io.read(index,offset,output,bytes))return fault(Error::Read);
  }
  if(!io.ready()||!io.stable(index,offset,bytes))return fault(Error::Stability);
  if(writing)++uploads_;else ++downloads_;phase_=Phase::Ready;return Error::None;
 }
 template<class Owner,class IO>Error page(Owner&owner,IO&io,uint64_t session,uint64_t handle,uint64_t ordinal,uint64_t*out){
  if(!out||!RtxProgram164::separate(out,32,this,sizeof(*this)))return Error::Shape;
  RTXSpans165::Mapping m{};uint32_t index=0;auto e=resolve(owner,io,session,handle,m,index);if(e!=Error::None)return e;
  if(ordinal>=m.mappedBytes/4096)return Error::Shape;
  const uint64_t offset=ordinal*4096;if(offset>=m.logicalBytes)return Error::Shape;
  const uint64_t bytes=m.logicalBytes-offset<4096?m.logicalBytes-offset:4096;
  if(!bytes||!io.stable(index,offset,bytes))return fault(Error::Stability);
  phase_=Phase::Active;++sequence_;
  uint64_t cached=0,physical=0,extent=0;
  if(!io.page(index,uint32_t(ordinal),cached,physical,extent)||cached!=physical||extent<4096||
     !io.ready()||!io.stable(index,offset,bytes))return fault(Error::Stability);
  const uint64_t words[]={ordinal,cached,physical,extent};for(unsigned i=0;i<4;++i)out[i]=words[i];
  phase_=Phase::Ready;return Error::None;
 }
};
}
