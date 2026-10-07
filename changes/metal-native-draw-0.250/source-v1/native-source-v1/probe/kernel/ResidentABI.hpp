#pragma once
#include "ResidentRuntime.hpp"
namespace RtxResidentABI058 {
namespace D=RtxResident058;namespace P=RtxProgram033;
constexpr unsigned Begin=78,Append=79,Seal=80,Apply=81,Info=82,Data=83,Submit=84;
inline bool selector(unsigned n){return n>=Begin&&n<=Submit;}
struct Call {const uint64_t *scalars=nullptr;size_t count=0;const uint8_t *input=nullptr;size_t inputBytes=0;uint8_t *output=nullptr;size_t outputBytes=0;};
inline bool shape(const Call &c,unsigned scalars,unsigned input,unsigned output){return c.count==scalars&&bool(c.scalars)==bool(scalars)&&c.inputBytes==input&&bool(c.input)==bool(input)&&c.outputBytes==output&&bool(c.output)==bool(output);}
template<class ApplyCallback>D::Error dispatch(D::State &s,const D::Scope &scope,unsigned selector,const Call &c,ApplyCallback apply){
 if(selector==Info)return shape(c,0,0,D::InfoBytes)&&s.info(c.output,c.outputBytes)?D::Error::None:D::Error::Shape;
 if(selector==Begin)return shape(c,0,D::HeaderBytes,0)?s.begin(scope,c.input,c.inputBytes):D::Error::Shape;
 if(selector==Append){if(!c.inputBytes||c.inputBytes>1024||!shape(c,1,unsigned(c.inputBytes),0))return D::Error::Shape;return s.append(scope,c.scalars[0],c.input,c.inputBytes);}
 if(selector==Seal)return shape(c,0,0,0)?s.seal(scope):D::Error::Shape;
 if(selector==Apply){if(!shape(c,1,0,0))return D::Error::Shape;if(!s.accepts(scope,c.scalars[0]))return D::Error::Stale;return apply()?D::Error::None:D::Error::Runtime;}
 return D::Error::Shape;
}
}
