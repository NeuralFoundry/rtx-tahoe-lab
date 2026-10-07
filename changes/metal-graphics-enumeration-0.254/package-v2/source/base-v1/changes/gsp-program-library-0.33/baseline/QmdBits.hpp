#pragma once
#include <stddef.h>
#include <stdint.h>

// Bounded wire-bit operations. Hardware generation and semantics are supplied
// by QmdProfile and checked against the pinned NVIDIA QMD3 source schema.
namespace RtxQmd3Bits {
constexpr size_t kBytes=256;
struct Field { uint32_t low,width; };
inline bool valid(Field f){return f.width&&f.width<=32&&f.low<2048&&f.width<=2048-f.low;}
inline uint32_t mask(Field f){return f.width==32?UINT32_MAX:(uint32_t(1)<<f.width)-1;}
inline bool get(const uint8_t *data,size_t bytes,Field f,uint32_t &value){
 if(!data||bytes!=kBytes||!valid(f))return false;
 uint32_t result=0;
 for(uint32_t bit=0;bit<f.width;++bit)result|=uint32_t((data[(f.low+bit)/8]>>((f.low+bit)%8))&1u)<<bit;
 value=result;return true;
}
inline bool put(uint8_t *data,size_t bytes,Field f,uint32_t value){
 if(!data||bytes!=kBytes||!valid(f)||(value&~mask(f)))return false;
 for(uint32_t bit=0;bit<f.width;++bit){
  const uint32_t index=f.low+bit,byte=index/8,shift=index%8;
  data[byte]=uint8_t((data[byte]&~(1u<<shift))|(((value>>bit)&1u)<<shift));
 }
 return true;
}
}
