#pragma once
#include "cpu-root-fixture182.hpp"
#include "probe/kernel/root181/OwnedDataABI181.hpp"
#include "probe/kernel/root181/MappedDataWindow181.hpp"
#include "probe/kernel/SelectorRouting171.hpp"
namespace D=RTXDataAccess181;namespace ABI=RTXDataABI181;
struct IO {
 Trial&t;bool live=true;std::string fail;unsigned hits=0,failAt=1,operations=0;
 explicit IO(Trial&trial):t(trial){}
 bool go(const char*name){++operations;return fail!=name||++hits!=failAt;}
 bool ready(){return go("ready")&&live&&t.binding.ready();}
 bool mapping(uint32_t i,uint64_t&session,RTXSpans165::Mapping&m){return go("mapping")&&t.arena.preparedSpan(i,session,m);}
 bool stable(uint32_t i,uint64_t off,uint64_t n){return go("stable")&&RTXDataWindow181::stable(t.arena.life,t.arena.io,i,off,n);}
 bool write(uint32_t i,uint64_t off,const void*p,uint64_t n){
  // Model a partially completed CPU copy on failure; the access state must
  // retain ownership and reject replay even though bytes were already changed.
  const bool good=go("write");auto&d=t.arena.io.data[i];
  std::memcpy(d.bytes.data()+off,p,size_t(good?n:1));++d.info.writeEpoch;return good;
 }
 bool publish(uint32_t i){if(!go("publish"))return false;auto&d=t.arena.io.data[i];d.info.publishedEpoch=d.info.writeEpoch;return true;}
 bool import(uint32_t){return go("import");}
 bool read(uint32_t i,uint64_t off,void*p,uint64_t n){const bool good=go("read");std::memcpy(p,t.arena.io.data[i].bytes.data()+off,size_t(good?n:1));return good;}
 bool page(uint32_t i,uint32_t p,uint64_t&cached,uint64_t&physical,uint64_t&extent){
  if(!go("page"))return false;cached=t.arena.io.data[i].pages[p];return t.arena.io.physicalPage(i,p,physical,extent);
 }
};
struct Work {
 Trial root;IO io;D::State state;
 Work(const std::vector<uint8_t>&r,const std::vector<uint8_t>&c):root(r,c),io{root}{check(root.commit());check(state.activate(0x179));}
 uint64_t handle(unsigned i){return (1ULL<<32)|(i+1);}
 D::Error transfer(bool w,unsigned i,uint64_t off,void*p,uint64_t n){return state.transfer(root.owner,io,w,0x179,handle(i),off,w?p:nullptr,w?nullptr:p,n);}
};
