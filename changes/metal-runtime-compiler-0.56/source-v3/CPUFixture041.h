#import "RTXApplicationClientInternal.h"
#include "RTXBrokerWire.hpp"
#include <unistd.h>
#include <vector>
// Only linked into labelled test executables, never the production client dylib.
struct CPUBackend041 {
 std::array<uint8_t,RTXLibrary036::Bytes> image{};unsigned calls=0;bool late=false;
 std::vector<std::array<uint8_t,2112>> requests;
 bool claim(decltype(image)&out,uint64_t &gen,uint64_t &completed){out=image;gen=37;completed=0;return true;}
 bool execute(const std::array<uint8_t,2112> &request,std::array<uint8_t,2048>&out,uint64_t &completion){
  ++calls;requests.push_back(request);if(late)usleep(500000);
  RTXLibrary036::Catalog catalog;if(!RTXLibrary036::decode(image.data(),image.size(),catalog))return false;
  RtxReusable035::Request parsed;if(!RtxReusable035::decode(request.data(),request.size(),catalog.library,parsed))return false;
  std::memcpy(out.data(),parsed.data,2048);const auto &program=catalog.library.programs[parsed.program];
  for(unsigned i=0;i<program.parameters;++i)if(program.writeMask&(1u<<program.bindings[i]))std::memset(out.data()+i*256,0x5a,program.localX*parsed.groups*4);
  completion=parsed.serial;return true;
 }
};
