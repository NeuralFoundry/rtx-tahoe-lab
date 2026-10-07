#pragma once
#include "probe/kernel/root179/RootDataBinding179.hpp"
#include "probe/kernel/root170/LegacyPageInventory169.hpp"
#include "owner/driver/GSPDigest.hpp"
#include "cpu-data179.hpp"
#include "cpu-queue179.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
namespace A=RTXDataArena178;namespace T=RTXPageTree167;
static unsigned checks=0,rejections=0;
static void check(bool v){++checks;if(!v){std::cerr<<"check "<<checks<<'\n';std::abort();}}
static std::vector<uint8_t>read(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary|std::ios::ate);check(bool(f));auto size=f.tellg();check(size>0);std::vector<uint8_t>raw(size_t(size),0);f.seekg(0);f.read(reinterpret_cast<char*>(raw.data()),size);check(bool(f));return raw;}
static void write(const std::filesystem::path&p,const void*raw,size_t n){std::ofstream f(p,std::ios::binary);f.write(static_cast<const char*>(raw),std::streamsize(n));check(bool(f));}
struct Arena {
 CPUData179::IO io;A::Lifetime life;RTXTreeBacking168::Info tree;
 unsigned calls=0,failStable=0;
 const A::Info&info()const{return life.info();}const RTXTreeBacking168::Info&treeInfo()const{return tree;}
 bool exposedStable(){return ++calls!=failStable&&life.exposedStable(io);}
 bool preparedSpan(uint32_t i,uint64_t&s,RTXSpans165::Mapping&m)const{return life.preparedSpan(i,s,m);}
 explicit Arena(const std::vector<uint8_t>&root,const std::vector<uint8_t>&children){
  std::vector<T::Mapping>prefix(3491);auto original=RTXLegacy169::decode(root.data(),root.size(),children.data(),children.size(),0x179,prefix.data(),prefix.size());
  check(original.error==RTXLegacy169::Error::None&&original.pages==3491);
  const A::Request requests[]={{4097,3},{65537,3},{1048577,3},{4194305,3}};
  check(life.prepare(io,0x179,requests,4,0x2000000000ULL,0x2100000000ULL,prefix.data(),uint32_t(prefix.size())));
  check(life.expose(io));tree.phase=RTXTreeBacking168::Phase::Exposed;tree.backingStarted=true;
  tree.root=io.tablePages[0];tree.tables=tree.written=uint32_t(io.tablePages.size());tree.bytes=uint64_t(tree.tables)*4096;tree.mappings=uint32_t(io.r.size());
 }
};
using Owner=RTXSpans165::Owner<64,16>;
struct Trial {
 Arena arena;CPUQueue179::Fake queue{3};CPUQueue179::Storage storage;
 Owner owner{0x179,0x2000000000ULL,0x2100000000ULL};RTXRootData179::Binding binding;
 uint8_t before[32]{},after[32]{};
 Trial(const std::vector<uint8_t>&root,const std::vector<uint8_t>&children):arena(root,children){
  queue.t=arena.treeInfo();check(RTXRootTransition170::execute(queue,queue.t,queue.p,storage.request.data(),storage.records.data(),storage.scratch.data(),storage.result));
  GSPDigest::SHA256 hash;hash.update(arena.io.image.data(),unsigned(arena.io.image.size()));hash.finish(before);std::memcpy(after,before,32);
 }
 bool commit(){return binding.commit(arena,owner,0x179,queue.p,storage.request.data(),storage.records.data(),storage.result,storage.scratch.data(),before,after);}
};
