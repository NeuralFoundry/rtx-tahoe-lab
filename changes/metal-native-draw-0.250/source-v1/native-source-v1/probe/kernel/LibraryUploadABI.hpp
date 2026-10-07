#pragma once
#include "LibraryUpload.hpp"
namespace RtxLibraryUploadABI036 {
namespace U=RtxLibraryUpload036;namespace P=U::P;
constexpr unsigned InfoSelector=72,BeginSelector=73,AppendSelector=74,SealSelector=75,ReadSelector=76;
inline bool selector(unsigned n){return n>=InfoSelector&&n<=ReadSelector;}
struct Call {
 const uint64_t *scalars=nullptr;unsigned scalarCount=0;
 const uint8_t *input=nullptr;size_t inputBytes=0;
 uint8_t *output=nullptr;size_t outputBytes=0;
};
inline U::Error dispatch(U::State &state,const U::Scope &scope,unsigned method,const Call &a){
 if(!selector(method))return U::Error::Shape;
 // All destinations are independent of the service and immutable inputs.
 // No local struct is allowed to alias bytes that the method may overwrite.
 if(!P::separate(&a,sizeof(a),&state,sizeof(state))||!P::separate(&scope,sizeof(scope),&state,sizeof(state))||
    a.scalarCount>3||(a.scalarCount&&!a.scalars)||(a.inputBytes&&!a.input)||(a.outputBytes&&!a.output))return U::Error::Shape;
 if(a.inputBytes&&(!P::separate(a.input,a.inputBytes,&state,sizeof(state))||
    !P::separate(a.input,a.inputBytes,&a,sizeof(a))))return U::Error::Shape;
 if(a.scalarCount&&!P::separate(a.scalars,a.scalarCount*8,&state,sizeof(state)))return U::Error::Shape;
 if(a.outputBytes&&(!P::separate(a.output,a.outputBytes,&state,sizeof(state))||!P::separate(a.output,a.outputBytes,&scope,sizeof(scope))||
    !P::separate(a.output,a.outputBytes,&a,sizeof(a))||(a.inputBytes&&!P::separate(a.output,a.outputBytes,a.input,a.inputBytes))||
    (a.scalarCount&&!P::separate(a.output,a.outputBytes,a.scalars,a.scalarCount*8))))return U::Error::Shape;
 switch(method){
 case InfoSelector:
  if(a.scalarCount||a.inputBytes||a.outputBytes!=U::InfoBytes)return U::Error::Shape;
  return state.info(a.output,a.outputBytes)?U::Error::None:U::Error::Shape;
 case BeginSelector:
  if(a.scalarCount||a.outputBytes||a.inputBytes!=U::HeaderBytes)return U::Error::Shape;
  return state.begin(scope,a.input,a.inputBytes);
 case AppendSelector:
  if(a.scalarCount!=1||a.outputBytes||!a.inputBytes||a.inputBytes>U::ChunkBytes)return U::Error::Shape;
  return state.append(scope,a.scalars[0],a.input,a.inputBytes);
 case SealSelector:
  if(a.scalarCount||a.inputBytes||a.outputBytes)return U::Error::Shape;
  return state.seal(scope);
 case ReadSelector:{
  if(a.scalarCount!=3||a.inputBytes||!a.outputBytes||a.outputBytes>U::ChunkBytes||a.scalars[0]>1||a.scalars[2]!=a.outputBytes)return U::Error::Shape;
  const uint8_t *source=nullptr;unsigned bytes=0;
  if(!state.data(scope,unsigned(a.scalars[0]),source,bytes))return U::Error::State;
  const auto offset=a.scalars[1],n=a.scalars[2];if(offset>bytes||n>bytes-offset)return U::Error::Shape;
  U::copy(a.output,source+unsigned(offset),unsigned(n));return U::Error::None;
 }
 default:return U::Error::Shape;
 }
}
}
