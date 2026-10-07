#pragma once
#include <cstdint>
#include <cstddef>
namespace RTXGraphicsBackend251{
struct Callbacks{
 uint32_t abi=251,bytes=sizeof(Callbacks);
 void*context=nullptr;uint64_t generation=0;
 int(*begin)(void*)=nullptr;
 int(*draw)(void*,const void*,size_t,void*,size_t,uint64_t*)=nullptr;
 int(*retire)(void*)=nullptr;
 uint64_t reserved=0;
};
inline bool valid(const Callbacks&c){return c.abi==251&&c.bytes==sizeof(c)&&c.generation&&c.begin&&c.draw&&c.retire&&!c.reserved;}
}
