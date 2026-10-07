#pragma once
#include "OwnedBatch187.hpp"
#include "NativeOwnedDispatch205.hpp"
#include <string>
namespace RTXOwnedCommand208 {
// A complete serialized native transaction. Sinks creates distinct evidence
// children; the caller owns invalidation after a failed committed operation.
// No arithmetic is simulated here: results come only from Data.readback after
// the dispatch client independently verifies the kernel execution captures.
template<class IO,class Sinks,class Data,class GPU>
bool execute(IO&io,Sinks&sinks,Data&data,GPU&gpu,const RTXProgram205::Library&library,const uint8_t*request,size_t n,RTXBatch187::Bytes&out,uint64_t&completion){
 completion=0;if(!data.ready()||!gpu.ready()||gpu.completed()==UINT64_MAX)return false;
 try{
  RTXBatch187::Plan plan;if(!RTXBatch187::decode(request,n,library.programs,data.session(),gpu.completed()+1,plan))return false;
  if(!RTXProgram205::geometry(library,plan.program,plan.groups,plan.threads))return false;
  // All capacity checks precede the first native data write.
  for(unsigned i=0;i<plan.resources;++i){const auto&r=plan.resource[i];if(data.capacity(r.slot)<r.bytes)return false;}
  if(!sinks.save("host-request.bin",request,n))return false;
  for(unsigned i=0;i<plan.resources;++i){const auto&r=plan.resource[i];RTXBatch187::Bytes ignored;
   if(!sinks.part("upload-"+std::to_string(i),[&](auto&sink){return data.transfer(io,sink,r.slot,0,request+RTXBatch187::Header+r.payloadOffset,size_t(r.bytes),ignored,true);} ))return false;
   if(!ignored.empty())return false;
  }
  std::array<RTXNativeDispatch205::Binding,8>bindings{};for(unsigned i=0;i<plan.count;++i){const auto&b=plan.binding[i];bindings[i]={plan.resource[b.resource].slot,b.index,b.offset,b.bytes};}uint64_t verified=0;
  if(!sinks.part("dispatch",[&](auto&sink){return gpu.submit(io,sink,plan.program,bindings.data(),plan.count,plan.groups,plan.threads,verified);})||verified!=plan.serial)return false;
  RTXBatch187::Bytes result(size_t(plan.payloadBytes));
  for(unsigned i=0;i<plan.resources;++i){const auto&r=plan.resource[i];RTXBatch187::Bytes read;
   if(!sinks.part("readback-"+std::to_string(i),[&](auto&sink){return data.transfer(io,sink,r.slot,0,nullptr,size_t(r.bytes),read,false);})||read.size()!=r.bytes)return false;
   std::copy(read.begin(),read.end(),result.begin()+r.payloadOffset);
  }
  if(!RTXBatch187::readback(plan,request,n,result.data(),result.size())||!sinks.save("host-result.bin",result.data(),result.size()))return false;
  uint8_t record[32]{};QmdProfile::put64(record,187);QmdProfile::put64(record+8,plan.serial);QmdProfile::put64(record+16,plan.resources);QmdProfile::put64(record+24,plan.payloadBytes);
  if(!sinks.save("host-completion.bin",record,sizeof(record)))return false;
  out.swap(result);completion=plan.serial;return true;
 }catch(...){return false;}
}
}
