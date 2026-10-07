#pragma once
#include "ResidentBrokerWire.hpp"
#include <mutex>
#include <vector>
namespace RTXCPU062 {
namespace W=RTXResidentBroker058;namespace P=W::P;
using Image=std::array<uint8_t,RTXLibrary036::Bytes>;using Payload=std::array<uint8_t,W::Payload>;
struct Backend {
 Image image{},second{};Payload active{};uint64_t epoch=1,completed=0;unsigned claims=0,admissions=0,replacements=0,executions=0;
 std::vector<std::array<uint8_t,2112>> wires;std::vector<Payload> payloads;
 bool allowed(const Payload &p)const{P::Library library;return W::library(p.data(),library);}
 bool claim(Image &out,uint64_t &gen,uint64_t &done,uint64_t &version){if(claims++)return false;out=image;gen=37;done=0;version=1;std::memcpy(active.data(),image.data()+640,4608);return true;}
 bool admit(const Payload &p){++admissions;return allowed(p);}
 bool replace(const Payload &p,uint64_t expected,uint64_t done,uint64_t &next){
  next=0;if(!allowed(p)||expected!=epoch||done!=completed||p==active)return false;active=p;next=++epoch;++replacements;return true;
 }
 bool execute(const std::array<uint8_t,2112> &wire,const Payload &p,uint64_t version,std::array<uint8_t,2048> &out,uint64_t &completion){
  completion=0;P::Library library;W::N::Request request;
  if(p!=active||version!=epoch||!W::library(p.data(),library)||!W::N::decode(wire.data(),wire.size(),library,request)||request.serial!=completed+1)return false;
  std::memcpy(out.data(),request.data,2048);const auto &program=library.programs[request.program];
  for(unsigned i=0;i<program.parameters;++i)if(program.writeMask&(1u<<program.bindings[i]))std::memset(out.data()+i*256,0x5a,program.localX*request.groups*4);
  wires.push_back(wire);payloads.push_back(p);++executions;completion=++completed;return true;
 }
};
struct Shared {std::mutex mutex;W::Core core;Backend backend;};
}
