#pragma once
#include "test-fixture205.hpp"
#include "owner/OwnedCommand187.hpp"
#include "owner/RTXDeviceLimitsLayout.hpp"
namespace B187=RTXBatch187;
struct Tree187 {
 std::map<std::string,Sink185>children;Sink185 top;std::vector<std::string>order;std::string fail;
 bool save(const char*n,const uint8_t*p,size_t bytes){if(fail==n)return false;return top.save(n,p,bytes);}
 template<class Action>bool part(const std::string&name,Action action){order.push_back(name);if(fail==name)return false;check(!children.count(name));return action(children[name]);}
 void dump(const std::filesystem::path&path){top.dump(path);for(auto&c:children)c.second.dump(path/c.first);}
};
struct Wire187:Wire185 {
 const B187::Plan*plan=nullptr;bool corruptReadOnly=false;std::vector<unsigned>selectors;
 explicit Wire187(Fake&f):Wire185(f){}
 bool call(unsigned sel,const uint64_t*sc,unsigned n,const uint8_t*in,size_t bytes,uint8_t*out,size_t cap){
  selectors.push_back(sel);const bool result=Wire185::call(sel,sc,n,in,bytes,out,cap);
  if(result&&sel==105&&plan){
   // Explicit data-plane fixture mutation after modeled completion. This is
   // neither shader emulation nor evidence that any arithmetic ran on a GPU.
   for(unsigned i=0;i<plan->resources;++i){const auto&r=plan->resource[i];auto&data=f.root->arena.io.data[r.slot].bytes;
    if(r.access&2)for(size_t j=0;j<r.bytes;++j)data[j]^=uint8_t(0x5a+(j%31));
    else if(corruptReadOnly)data[0]^=1;
   }
  }
  return result;
 }
};
struct Live187 {
 Fake fake;Wire187 io;RTXNativeData182::Session data;C::Session gpu;Bootstrap185 bootstrap;
 Live187(Input&input,const R::Capture&root,const Bytes&image):fake(input),io(fake),bootstrap{fake}{Sink185 sink;check(data.collect(io,sink,root));Sink185 begin;check(gpu.begin(io,begin,root,data,bootstrap,image.data()+640,4608));io.count=0;io.selectors.clear();}
 void retire(){gpu.invalidate();data.invalidate();}
};
static B187::Plan makePlan(const RTXCatalog187::Catalog&catalog,bool alias,bool large,uint64_t serial=1){
 B187::Input bindings[3]={{17,large?65537ULL:4097ULL,4,0},{19,large?1048577ULL:8193ULL,4092,1},{alias?17ULL:23ULL,alias?(large?65537ULL:4097ULL):(large?4194305ULL:16385ULL),0,2}};
 B187::Plan p;const auto&q=catalog.library.programs[0];check(B187::plan(q,0,bindings,3,{2,3,4},{q.localX,q.localY,q.localZ},0x179,serial,p));check(p.resources==(alias?2:3));return p;
}
static Bytes request(const B187::Plan&p){std::array<Bytes,4>copies;for(unsigned i=0;i<p.resources;++i){copies[i].resize(p.resource[i].bytes);for(size_t j=0;j<copies[i].size();++j)copies[i][j]=uint8_t(j*17+i*23);}Bytes wire;check(B187::assemble(p,copies,wire));return wire;}
static void expected(const B187::Plan&p,const Bytes&wire,const Bytes&out){
 check(out.size()==p.payloadBytes);for(unsigned i=0;i<p.resources;++i){const auto&r=p.resource[i];for(size_t j=0;j<r.bytes;++j){const auto before=wire[B187::Header+r.payloadOffset+j];const auto want=uint8_t(before^((r.access&2)?uint8_t(0x5a+j%31):0));if(out[r.payloadOffset+j]!=want)check(false);}}check(true);
}
