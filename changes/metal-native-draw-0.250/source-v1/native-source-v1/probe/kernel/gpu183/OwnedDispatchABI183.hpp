#pragma once
#include "OwnedDispatch183.hpp"
namespace RTXOwnedDispatchABI183 {
constexpr unsigned Info=101,Begin=102,Upload=103,Seal=104,Submit=105,Capture=106;
struct Call {const uint64_t*scalars;unsigned count;const void*input;uint64_t inputBytes;void*output;uint64_t outputBytes;};
inline bool shape(const Call&c,unsigned count,uint64_t in,uint64_t out){return c.count==count&&c.inputBytes==in&&c.outputBytes==out&&(!count||c.scalars)&&(!in||c.input)&&(!out||c.output);}
template<class IO>RTXOwnedDispatch183::Error dispatch(RTXOwnedDispatch183::State&s,RTXSpans165::Owner<64,16>&owner,IO&io,uint64_t generation,unsigned selector,const Call&c){
 using E=RTXOwnedDispatch183::Error;
 if(selector==Info){if(!shape(c,0,0,128))return E::Shape;s.info(generation,static_cast<uint64_t*>(c.output));return E::None;}
 if(selector==Capture){
  if(c.count!=4||!c.scalars||c.scalars[1]>10||!c.scalars[3]||c.scalars[3]>4096||!shape(c,4,0,c.scalars[3]))return E::Shape;
  if(c.scalars[0]!=generation)return E::Scope;
  return s.captureBytes(unsigned(c.scalars[1]),c.scalars[2],c.output,c.scalars[3])?E::None:E::Shape;
 }
 if(selector==RTXProgram205::Info){if(!shape(c,0,0,64))return E::Shape;s.programInfo205(generation,static_cast<uint64_t*>(c.output));return E::None;}
 if(!io.ready())return E::State;
 if(selector==RTXProgram205::Select){
  if(!shape(c,3,RTXProgram205::PayloadBytes,64))return E::Shape;
  if(c.scalars[0]!=generation)return E::Scope;
  const auto error=s.select205(c.scalars[0],c.scalars[1],c.scalars[2],c.input,size_t(c.inputBytes));
  if(error==E::None)s.programInfo205(generation,static_cast<uint64_t*>(c.output));return error;
 }
 if(selector==Begin){if(!shape(c,1,512,0))return E::Shape;if(c.scalars[0]!=generation)return E::Scope;return s.begin(generation,c.input,512);}
 if(selector==Upload){
  if(c.count!=3||!c.scalars||!c.scalars[2]||c.scalars[2]>4096||!shape(c,3,c.scalars[2],0))return E::Shape;
  return s.upload(c.scalars[0],c.scalars[1],c.input,size_t(c.scalars[2]));
 }
 if(selector==Seal){if(!shape(c,1,0,0))return E::Shape;return s.seal(c.scalars[0]);}
 if(selector==Submit){if(!shape(c,0,RTXOwnedDispatch183::RequestBytes,0))return E::Shape;return s.submit(io,owner,c.input,size_t(c.inputBytes));}
 return E::Shape;
}
}
