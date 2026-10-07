#pragma once
#include "../root194/MacStablePageTree194.hpp"
#include "../root181/MappedDataWindow181.hpp"

// Kernel adapter. Caller owns an open PCI provider and serializes every call in
// sleepable context. Prefix mappings must belong to that same retained native
// owner. This object owns every new data allocation and the replacement tree.
// Keep it alive until cleanup() succeeds; exposed/ambiguous state is retained.
class MacOwnedDataArena195 {
 IOPCIDevice*pci;
 MacMetalDmaBacking166*data[RTXDataArena178::MaxBuffers]{};
 MacStablePageTree194 tree;
 RTXDataArena178::Lifetime lifetime;
 RTXPageTree167::Mapping*rows=nullptr,*pending=nullptr;
 uint64_t*scratch=nullptr;
 struct Backend {
  MacOwnedDataArena195&s;
  bool allocateRows(uint32_t n){s.rows=static_cast<RTXPageTree167::Mapping*>(IOMalloc(size_t(n)*sizeof(*s.rows)));return s.rows!=nullptr;}
  bool allocateScratch(uint32_t n){s.scratch=static_cast<uint64_t*>(IOMalloc(size_t(n)*8));return s.scratch!=nullptr;}
  RTXPageTree167::Mapping*rows(){return s.rows;}
  uint64_t*scratch(){return s.scratch;}
  bool prepareBuffer(uint32_t i,uint64_t n){
   if(s.data[i])return false;s.data[i]=new MacMetalDmaBacking166(s.pci);
   return s.data[i]&&s.data[i]->prepare(n);
  }
  const RTXBacking166::Info&backingInfo(uint32_t i)const{return s.data[i]->info();}
  const uint64_t*pages(uint32_t i)const{return s.data[i]->pages();}
  bool physicalPage(uint32_t i,uint32_t n,uint64_t&pa,uint64_t&extent)const{return s.data[i]->physicalPage(n,pa,extent);}
  bool publishBuffer(uint32_t i){return s.data[i]->publish();}
  bool writeBuffer(uint32_t i,uint64_t off,const void*p,uint64_t n){return s.data[i]->write(off,p,n);}
  bool treeStable()const{return s.tree.hostReadyStable()||s.tree.exposedStable();}
  bool allocatePendingRows(uint32_t n){if(s.pending)return false;s.pending=static_cast<RTXPageTree167::Mapping*>(IOMalloc(size_t(n)*sizeof(*s.pending)));return s.pending!=nullptr;}
  RTXPageTree167::Mapping*pendingRows(){return s.pending;}
  bool commitRows(uint32_t oldCount){if(!s.pending)return false;IOFree(s.rows,size_t(oldCount)*sizeof(*s.rows));s.rows=s.pending;s.pending=nullptr;return true;}
  bool exposeBuffer(uint32_t i){return s.data[i]->expose();}
  bool exposeTree(){return s.tree.expose();}
  bool cleanupTree(){return s.tree.cleanup();}
  bool cleanupBuffer(uint32_t i){
   if(!s.data[i])return true;
   if(!s.data[i]->cleanup()||s.data[i]->held())return false;
   delete s.data[i];s.data[i]=nullptr;return true;
  }
  void freeScratch(uint32_t n){IOFree(s.scratch,size_t(n)*8);s.scratch=nullptr;}
  void freeRows(uint32_t n){IOFree(s.rows,size_t(n)*sizeof(*s.rows));s.rows=nullptr;}
 };
 MacOwnedDataArena195(const MacOwnedDataArena195&)=delete;
 MacOwnedDataArena195&operator=(const MacOwnedDataArena195&)=delete;
public:
 explicit MacOwnedDataArena195(IOPCIDevice*p):pci(p),tree(p){}
 ~MacOwnedDataArena195()=default; // Never destroys still-prepared/exposed backing.
 const RTXDataArena178::Info&info()const{return lifetime.info();}
 struct TreeInfo {uint64_t root,bytes;uint32_t tables,capacity,mappings;};
 TreeInfo treeInfo()const{return{tree.root(),uint64_t(tree.capacity())*4096,tree.used(),tree.capacity(),tree.mappings()};}
 const RTXStableTree194::PublishInfo&lastPublication()const{return tree.lastPublication();}
 template<class Ready>bool prepare(uint64_t session,const RTXDataArena178::Request*q,uint32_t n,uint64_t begin,uint64_t end,
              const RTXPageTree167::Mapping*prefix,uint32_t prefixCount,Ready&ready){
  if(!pci)return false;
  struct B:Backend {
   Ready&ready;B(MacOwnedDataArena195&s,Ready&r):Backend{s},ready(r){}
   bool prepareTree(const RTXPageTree167::Mapping*r,uint32_t n){return this->s.tree.prepare(r,n,ready);}
  }b{*this,ready};
  return lifetime.prepare(b,session,q,n,begin,end,prefix,prefixCount);
 }
 template<class Ready,class Invalidate>bool appendPrefix(const RTXPageTree167::Mapping*p,uint32_t n,Ready&ready,Invalidate&invalidate){
  struct B:Backend {
   Ready&ready;Invalidate&invalidate;
   B(MacOwnedDataArena195&s,Ready&r,Invalidate&i):Backend{s},ready(r),invalidate(i){}
   bool appendTree(const RTXPageTree167::Mapping*r,uint32_t n){return this->s.tree.append(r,n,ready,invalidate);}
  }b{*this,ready,invalidate};
  return lifetime.appendPrefix(b,p,n);
 }
 bool copyInitial(uint32_t i,uint64_t off,const void*p,uint64_t n){Backend b{*this};return lifetime.copyInitial(b,i,off,p,n);}
 bool expose(){Backend b{*this};return lifetime.expose(b);}
 bool cleanup(){Backend b{*this};return lifetime.cleanup(b);}
 bool exposedStable(){Backend b{*this};return lifetime.exposedStable(b);}
 bool importTreeForObservation(){return tree.importForObservation();}
 bool captureRows(uint64_t offset,void*out,uint64_t bytes)const{
  if(!out||!bytes||bytes>4096||!rows||
     (info().phase!=RTXDataArena178::Phase::HostReady&&info().phase!=RTXDataArena178::Phase::Exposed)||
     !RTXBacking166::range(offset,bytes,uint64_t(info().totalRows)*sizeof(*rows)))return false;
  bcopy(reinterpret_cast<const uint8_t*>(rows)+offset,out,size_t(bytes));return true;
 }
 bool preparedSpan(uint32_t i,uint64_t&session,RTXSpans165::Mapping&out)const{return lifetime.preparedSpan(i,session,out);}
 bool readInitial(uint32_t i,uint64_t off,void*p,uint64_t n)const{
  return info().phase==RTXDataArena178::Phase::HostReady&&i<info().buffers&&data[i]&&data[i]->read(off,p,n);
 }
 bool readTree(uint64_t off,void*p,uint64_t n)const{return tree.read(off,p,n);}
 bool tablePage(uint32_t i,uint64_t&pa,uint64_t&extent)const{return tree.tablePage(i,pa,extent);}
 // Access181 calls these only after root acknowledgement, idle-runtime and
 //165 handle/reference checks, all under the existing sleepable client mutex.
 bool mappedStable(uint32_t i,uint64_t off,uint64_t n){Backend b{*this};return RTXDataWindow181::stable(lifetime,b,i,off,n);}
 bool writeMapped(uint32_t i,uint64_t off,const void*p,uint64_t n){return mappedStable(i,off,n)&&data[i]->write(off,p,n);}
 bool publishMapped(uint32_t i){return info().phase==RTXDataArena178::Phase::Exposed&&i<info().buffers&&data[i]&&data[i]->publish();}
 bool importMapped(uint32_t i){return info().phase==RTXDataArena178::Phase::Exposed&&i<info().buffers&&data[i]&&data[i]->importForObservation();}
 bool readMapped(uint32_t i,uint64_t off,void*p,uint64_t n){return mappedStable(i,off,n)&&data[i]->read(off,p,n);}
 bool mappedPage(uint32_t i,uint32_t p,uint64_t&cached,uint64_t&physical,uint64_t&extent)const{
  if(info().phase!=RTXDataArena178::Phase::Exposed||i>=info().buffers||!data[i]||p>=data[i]->info().pages||!data[i]->pages())return false;
  cached=data[i]->pages()[p];return data[i]->physicalPage(p,physical,extent);
 }

};
