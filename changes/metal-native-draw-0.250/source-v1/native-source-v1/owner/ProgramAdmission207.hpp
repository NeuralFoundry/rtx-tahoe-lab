#pragma once
#include <cstddef>
#include <cstdint>
namespace RTXProgramAdmission207 {
using Function=uint32_t(*)(void*,const void*,size_t);
struct Callbacks {uint32_t abi,bytes;void*context;Function function;uint64_t reserved;};
static_assert(sizeof(Callbacks)==32,"Root admission ABI");
// The owner binds once, before opening the native device. Callbacks and their
// context must remain alive until native close, including quarantine holds.
class Gate {
 Callbacks callbacks{};int64_t process=0;bool bound=false;
public:
 bool bind(const Callbacks&c,int64_t pid,uint32_t uid){
  if(bound||pid<=0||uid||c.abi!=207||c.bytes!=sizeof(c)||!c.context||!c.function||c.reserved)return false;
  callbacks=c;process=pid;bound=true;return true;
 }
 bool admit(const uint8_t*payload,size_t bytes,int64_t pid,uint32_t uid)const{
  if(!bound||uid||pid!=process||!payload||bytes!=4608)return false;
  try{return callbacks.function(callbacks.context,payload,bytes)==0;}catch(...){return false;}
 }
};
}
