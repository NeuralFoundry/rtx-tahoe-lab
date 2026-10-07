#pragma once
#include "OwnedDataAccess181.hpp"
namespace RTXDataABI181 {
constexpr unsigned Info=97,Write=98,Read=99,Page=100;
struct Call {const uint64_t*scalars;unsigned count;const void*input;uint64_t inputBytes;void*output;uint64_t outputBytes;};
inline bool shape(const Call&c,unsigned count,uint64_t in,uint64_t out){
 return c.count==count&&c.inputBytes==in&&c.outputBytes==out&&(!count||c.scalars)&&(!in||c.input)&&(!out||c.output);
}
template<class Owner,class IO>RTXDataAccess181::Error dispatch(RTXDataAccess181::State&state,Owner&owner,IO&io,unsigned selector,const Call&c){
 using E=RTXDataAccess181::Error;
 if(selector==Write||selector==Read){
  if(c.count!=4||!c.scalars||!c.scalars[3]||c.scalars[3]>4096||!shape(c,4,selector==Write?c.scalars[3]:0,selector==Read?c.scalars[3]:0))return E::Shape;
  return state.transfer(owner,io,selector==Write,c.scalars[0],c.scalars[1],c.scalars[2],selector==Write?c.input:nullptr,selector==Read?c.output:nullptr,c.scalars[3]);
 }
 if(selector==Page){if(!shape(c,3,0,32))return E::Shape;return state.page(owner,io,c.scalars[0],c.scalars[1],c.scalars[2],static_cast<uint64_t*>(c.output));}
 return E::Shape;
}
}
