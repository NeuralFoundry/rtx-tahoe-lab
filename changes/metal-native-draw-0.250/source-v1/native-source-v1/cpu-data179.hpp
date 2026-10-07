#pragma once
#include "probe/kernel/root179/OwnedDataArena178.hpp"
#include <array>
#include <cstring>
#include <string>
#include <vector>
namespace CPUData179 {namespace A=RTXDataArena178;namespace T=RTXPageTree167;
struct IO {
 struct Data {RTXBacking166::Info info{};std::vector<uint64_t>pages;std::vector<uint8_t>bytes;};
 std::array<Data,A::MaxBuffers>data;
 std::vector<T::Mapping>r;
 std::vector<uint64_t>s,tablePages,tableScratch;
 std::vector<uint8_t>image;
 std::vector<std::string>events;
 std::string fail;
 unsigned hits=0,failAt=1;
 bool treeReady=false,treeExposed=false;
 bool go(const std::string&name){events.push_back(name);return name!=fail||++hits!=failAt;}
 bool allocateRows(uint32_t n){if(!go("rows"))return false;r.resize(n);return true;}
 bool allocateScratch(uint32_t n){if(!go("scratch"))return false;s.resize(n);return true;}
 T::Mapping*rows(){return r.data();}uint64_t*scratch(){return s.data();}
 bool prepareBuffer(uint32_t i,uint64_t n){
  auto&d=data[i];d.info.started=true;
  // Failure leaves partial ownership for the common cleanup path.
  d.info.memoryAllocated=true;
  if(!go("allocate"))return false;
  d.info.logical=n;d.info.mapped=(n+4095)&~uint64_t(4095);d.info.pages=uint32_t(d.info.mapped/4096);
  d.info.ready=true;d.info.writeEpoch=d.info.publishedEpoch=1;
  d.bytes.resize(size_t(d.info.mapped));
  for(uint32_t j=0;j<d.info.pages;++j)d.pages.push_back(0x100000000ULL+uint64_t(i)*0x10000000+uint64_t(j)*8192);
  if(fail=="duplicate"&&i==1)d.pages[0]=data[0].pages[0];
  if(fail=="invalid-page")d.pages[0]+=1;
  return true;
 }
 const RTXBacking166::Info&backingInfo(uint32_t i)const{return data[i].info;}
 const uint64_t*pages(uint32_t i)const{return data[i].pages.data();}
 bool physicalPage(uint32_t i,uint32_t n,uint64_t&pa,uint64_t&extent){
  if(!go("physical"))return false;pa=data[i].pages[n];extent=4096;
  if(fail=="changed-page")pa+=4096;
  if(fail=="short-page")extent=4095;
  return true;
 }
 bool publishBuffer(uint32_t i){if(!go("publish"))return false;data[i].info.publishedEpoch=data[i].info.writeEpoch;return true;}
 bool writeBuffer(uint32_t i,uint64_t off,const void*p,uint64_t n){
  if(!go("write"))return false;std::memcpy(data[i].bytes.data()+off,p,size_t(n));++data[i].info.writeEpoch;return true;
 }
 bool prepareTree(const T::Mapping*p,uint32_t n){
  if(!go("tree"))return false;
  auto shape=T::measure(p,n);if(shape.error!=T::Error::None)return false;
  for(uint32_t i=0;i<shape.tables;++i)tablePages.push_back(0x3000000000ULL+uint64_t(i)*8192);
  if(fail=="tree-data-alias")tablePages[0]=data[0].pages[0];
  tableScratch.resize(shape.tables);image.resize(size_t(shape.tables)*4096);
  auto result=T::build(p,n,tablePages.data(),shape.tables,tableScratch.data(),image.data(),image.size(),true);
  treeReady=result.error==T::Error::None;return treeReady;
 }
 bool treeStable(){return go("tree-stable")&&treeReady;}
 bool exposeBuffer(uint32_t i){if(!go("expose-data"))return false;data[i].info.exposed=true;return true;}
 bool exposeTree(){if(!go("expose-tree"))return false;treeExposed=true;return true;}
 bool cleanupTree(){if(!go("cleanup-tree")||treeExposed)return false;treeReady=false;image.clear();tablePages.clear();tableScratch.clear();return true;}
 bool cleanupBuffer(uint32_t i){
  if(!go("cleanup-data")||data[i].info.exposed)return false;
  data[i].info.memoryAllocated=false;data[i].info.ready=false;data[i].bytes.clear();data[i].pages.clear();return true;
 }
 void freeScratch(uint32_t){events.push_back("free-scratch");s.clear();}
 void freeRows(uint32_t){events.push_back("free-rows");r.clear();}
 bool empty()const{
  if(!r.empty()||!s.empty()||treeReady||!image.empty()||!tablePages.empty())return false;
  for(const auto&d:data)if(d.info.memoryAllocated||!d.bytes.empty()||!d.pages.empty())return false;
  return true;
 }
};
}
