#pragma once
#include "../gpu241/GraphicsSubmit241.hpp"
namespace RTXGraphicsABI242 {
namespace G=RTXGraphicsSubmit241;
constexpr unsigned Info=109,Submit=110,Capture=111;
struct Call {const uint64_t*scalars;unsigned count;const void*input;uint64_t inputBytes;void*output;uint64_t outputBytes;};
inline bool shape(const Call&c,unsigned n,uint64_t in,uint64_t out){return c.count==n&&c.inputBytes==in&&c.outputBytes==out&&(!n||c.scalars)&&(!in||c.input)&&(!out||c.output);}
inline bool submitShape249(const Call&c){return (c.inputBytes==256||c.inputBytes==320)&&shape(c,0,c.inputBytes,0);}
inline void coldInfo(uint64_t generation,uint64_t*out){for(unsigned n=0;n<32;++n)out[n]=0;out[0]=G::Magic;out[1]=241;out[2]=generation;out[26]=5;out[27]=8192;out[28]=4804;}
// The external structure is a byte buffer and need not have uint64 alignment.
inline void encodeInfo(const uint64_t*words,void*output){auto*p=static_cast<uint8_t*>(output);for(unsigned n=0;n<32;++n)G::P::Q::put64(p+n*8,words[n]);}
template<class IO>G::Error dispatch(G::State&s,RTXSpans165::Owner<64,16>&owner,IO&io,uint64_t generation,unsigned selector,const Call&c){
 using E=G::Error;
 if(selector==Info){if(!shape(c,0,0,256))return E::Shape;uint64_t words[32]{};s.info(generation,words);encodeInfo(words,c.output);return E::None;}
 if(selector==Capture){
  if(c.count!=4||!c.scalars||c.scalars[1]>8||!c.scalars[3]||c.scalars[3]>4096||!shape(c,4,0,c.scalars[3]))return E::Shape;
  if(c.scalars[0]!=generation)return E::Scope;
  return s.capture(unsigned(c.scalars[1]),c.scalars[2],c.output,c.scalars[3])?E::None:E::Shape;
 }
 if(selector!=Submit||!submitShape249(c))return E::Shape;
 return s.submit(io,owner,generation,c.input,size_t(c.inputBytes));
}
}
