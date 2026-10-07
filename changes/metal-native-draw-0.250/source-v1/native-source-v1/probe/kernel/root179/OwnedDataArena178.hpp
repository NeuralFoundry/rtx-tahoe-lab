#pragma once
#include "OwnedPageTree167.hpp"
#include "OwnedBufferSpans165.hpp"

// Serialized native-owner implementation, not a user-client ABI. Requests carry
// lengths/access only. Physical pages come exclusively from owned DMA backing.
// A prepared/exposed tree is NOT an acknowledged GPU mapping. Admit the returned
// span descriptors only after the root-switch/fence backend verifies that step.
namespace RTXDataArena178 {
constexpr uint32_t MaxBuffers=64;
constexpr uint64_t MaxBytes=RTXBacking166::MaxBufferBytes;
enum class Phase:uint32_t {Idle,Preparing,HostReady,Exposing,Exposed,Failed,Retained,Released};
enum class Step:uint32_t {None,Shape,Rows,Scratch,Allocation,Pages,Uniqueness,Publish,Tree,Stability,ExposeData,ExposeTree,Cleanup};
struct Request {uint64_t bytes;uint32_t access;};
struct Buffer {
 uint64_t logical=0,mapped=0,va=0;
 uint32_t pages=0,access=0;
 bool attempted=false,exposed=false;
};
struct Info {
 Phase phase=Phase::Idle;Step step=Step::None;
 uint64_t session=0,logicalBytes=0,mappedBytes=0;
 uint32_t buffers=0,dataPages=0,prefixPages=0,totalRows=0;
 bool rows=false,scratch=false,treeAttempted=false,cleanupAttempted=false,cleanupSucceeded=false;
};
inline bool overlap(uint64_t a,uint64_t an,uint64_t b,uint64_t bn){return a<b+bn&&b<a+an;}
inline void sortRows(RTXPageTree167::Mapping*p,uint32_t count){
 auto sift=[&](uint32_t at,uint32_t n){
  while(at*2+1<n){uint32_t child=at*2+1;if(child+1<n&&p[child].va<p[child+1].va)++child;
   if(p[at].va>=p[child].va)break;auto t=p[at];p[at]=p[child];p[child]=t;at=child;}
 };
 for(uint32_t i=count/2;i;--i)sift(i-1,count);
 for(uint32_t n=count;n>1;--n){auto t=p[0];p[0]=p[n-1];p[n-1]=t;sift(0,n-1);}
}

class Lifetime {
 Info m;
 uint64_t arenaBegin=0,arenaEnd=0;
 Buffer buffers[MaxBuffers]{};
 Lifetime(const Lifetime&)=delete;Lifetime&operator=(const Lifetime&)=delete;
 bool fail(Step step){m.phase=Phase::Failed;m.step=step;return false;}
 bool retained(Step step){m.phase=Phase::Retained;m.step=step;return false;}
 template<class IO>bool backingMatches(IO&io,uint32_t i,bool exposed)const{
  const auto&s=io.backingInfo(i);const auto&b=buffers[i];
  return s.ready&&!s.error&&!s.cleanupAttempted&&s.exposed==exposed&&
   s.logical==b.logical&&s.mapped==b.mapped&&s.pages==b.pages&&s.writeEpoch&&
   s.writeEpoch==s.publishedEpoch&&io.pages(i);
 }
 template<class IO>bool stable(IO&io,bool exposed)const{
  for(uint32_t i=0;i<m.buffers;++i){
   if(!backingMatches(io,i,exposed))return false;
   const auto*pages=io.pages(i);
   for(uint32_t n=0;n<buffers[i].pages;++n){uint64_t pa=0,extent=0;
    if(!io.physicalPage(i,n,pa,extent)||pa!=pages[n]||extent<4096)return false;
    const auto*rows=io.rows();uint32_t low=0,high=m.totalRows;
    const auto va=buffers[i].va+uint64_t(n)*4096;
    while(low<high){uint32_t mid=low+(high-low)/2;if(rows[mid].va<va)low=mid+1;else high=mid;}
    if(low==m.totalRows||rows[low].va!=va||rows[low].physical!=pa||rows[low].owner!=uint64_t(i)+1||
       rows[low].aperture!=RTXPageTree167::Aperture::System)return false;
   }
  }
  return true;
 }
public:
 Lifetime()=default;
 const Info&info()const{return m;}
 const Buffer*buffer(uint32_t index)const{return index<m.buffers?&buffers[index]:nullptr;}
 template<class IO>bool prepare(IO&io,uint64_t session,const Request*requests,uint32_t count,
                              uint64_t begin,uint64_t end,const RTXPageTree167::Mapping*prefix,uint32_t prefixCount){
  if(m.phase!=Phase::Idle||m.cleanupAttempted)return false;
  m.phase=Phase::Preparing;m.session=session;
  if(!session||!requests||!count||count>MaxBuffers||begin>=end||begin%4096||end%4096||
     end>RTXPageTree167::VaLimit||!begin||prefixCount>RTXPageTree167::MaxMappings||
     (prefixCount&&RTXPageTree167::measure(prefix,prefixCount).error!=RTXPageTree167::Error::None))return fail(Step::Shape);
  arenaBegin=begin;arenaEnd=end;uint64_t cursor=begin;
  // Validate and copy all caller lengths before a backend allocation can occur.
  for(uint32_t i=0;i<count;++i){
   const auto&q=requests[i];
   if(!q.bytes||q.bytes>MaxBytes||!RTXSpans165::access(q.access))return fail(Step::Shape);
   const uint64_t mapped=(q.bytes+4095)&~uint64_t(4095);
   if(mapped>MaxBytes-m.mappedBytes||cursor>=end||mapped>end-cursor)return fail(Step::Shape);
   buffers[i].logical=q.bytes;buffers[i].mapped=mapped;buffers[i].pages=uint32_t(mapped/4096);
   buffers[i].va=cursor;buffers[i].access=q.access;m.logicalBytes+=q.bytes;m.mappedBytes+=mapped;
   m.dataPages+=buffers[i].pages;
   cursor+=mapped;
   // An unmapped guard page separates allocations. Logical tail padding never
   // becomes a binding range, although GPU shader robustness is still separate.
   if(i+1<count){if(4096>end-cursor)return fail(Step::Shape);cursor+=4096;}
  }
  if(prefixCount>RTXPageTree167::MaxMappings-m.dataPages)return fail(Step::Shape);
  for(uint32_t i=0;i<prefixCount;++i){
   if(overlap(begin,end-begin,prefix[i].va,4096))return fail(Step::Shape);
   // Owner IDs are scoped to this complete tree; reserve the new arena's IDs.
   if(prefix[i].owner<=count)return fail(Step::Shape);
  }
  m.buffers=count;m.prefixPages=prefixCount;m.totalRows=prefixCount+m.dataPages;
  if(!io.allocateRows(m.totalRows))return fail(Step::Rows);m.rows=true;
  auto*rows=io.rows();if(!rows)return fail(Step::Rows);
  for(uint32_t i=0;i<prefixCount;++i)rows[i]=prefix[i];
  if(!io.allocateScratch(m.dataPages))return fail(Step::Scratch);m.scratch=true;
  auto*scratch=io.scratch();if(!scratch)return fail(Step::Scratch);
  uint32_t ordinal=0;
  for(uint32_t i=0;i<count;++i){auto&b=buffers[i];b.attempted=true;
   if(!io.prepareBuffer(i,b.logical))return fail(Step::Allocation);
   if(!backingMatches(io,i,false))return fail(Step::Pages);
   const auto*pages=io.pages(i);
   for(uint32_t n=0;n<b.pages;++n){const auto pa=pages[n];uint64_t physical=0,extent=0;
    if(pa<4096||pa%4096||pa>RTXBacking166::AddressLimit-4096||
       !io.physicalPage(i,n,physical,extent)||physical!=pa||extent<4096)return fail(Step::Pages);
    scratch[ordinal]=pa;
    rows[prefixCount+ordinal]={b.va+uint64_t(n)*4096,pa,uint64_t(i)+1,RTXPageTree167::Aperture::System,
     (b.access&RTXSpans165::Write)?RTXPageTree167::Access::ReadWrite:RTXPageTree167::Access::Read,0,0};
    ++ordinal;
   }
  }
  // Sort a private copy of all data PAs; keep original allocation page order.
  for(uint32_t i=m.dataPages/2;i;--i)RTXBacking166::sift(scratch,i-1,m.dataPages);
  for(uint32_t n=m.dataPages;n>1;--n){auto t=scratch[0];scratch[0]=scratch[n-1];scratch[n-1]=t;RTXBacking166::sift(scratch,0,n-1);}
  for(uint32_t i=1;i<m.dataPages;++i)if(scratch[i]==scratch[i-1])return fail(Step::Uniqueness);
  for(uint32_t i=0;i<prefixCount;++i)
   if(rows[i].aperture==RTXPageTree167::Aperture::System&&RTXPageTree167::contains(scratch,m.dataPages,rows[i].physical))return fail(Step::Uniqueness);
  sortRows(rows,m.totalRows);
  if(RTXPageTree167::measure(rows,m.totalRows).error!=RTXPageTree167::Error::None)return fail(Step::Rows);
  for(uint32_t i=0;i<count;++i)if(!io.publishBuffer(i))return fail(Step::Publish);
  if(!stable(io,false))return fail(Step::Stability);
  m.treeAttempted=true;
  if(!io.prepareTree(rows,m.totalRows))return fail(Step::Tree);
  if(!stable(io,false)||!io.treeStable())return fail(Step::Stability);
  io.freeScratch(m.dataPages);m.scratch=false;
  m.phase=Phase::HostReady;m.step=Step::None;return true;
 }
 template<class IO>bool copyInitial(IO&io,uint32_t index,uint64_t offset,const void*data,uint64_t bytes){
  if(m.phase!=Phase::HostReady||index>=m.buffers||!data||bytes>4096||
     !RTXBacking166::range(offset,bytes,buffers[index].logical))return false;
  if(!io.writeBuffer(index,offset,data,bytes)||!io.publishBuffer(index))return fail(Step::Publish);
  return stable(io,false)?true:fail(Step::Stability);
 }
 // Before exposing any address, every data allocation must become retained.
 // Partial exposure is irreversible here, even if no firmware call followed.
 template<class IO>bool expose(IO&io){
  if(m.phase!=Phase::HostReady||m.cleanupAttempted)return false;
  if(!stable(io,false)||!io.treeStable())return fail(Step::Stability);
  m.phase=Phase::Exposing;
  for(uint32_t i=0;i<m.buffers;++i){
   if(!io.exposeBuffer(i))return retained(Step::ExposeData);
   buffers[i].exposed=true;
  }
  if(!stable(io,true))return retained(Step::Stability);
  if(!io.exposeTree())return retained(Step::ExposeTree);
  if(!stable(io,true)||!io.treeStable())return retained(Step::Stability);
  m.phase=Phase::Exposed;m.step=Step::None;return true;
 }
 template<class IO>bool cleanup(IO&io){
  if(m.phase==Phase::Exposing||m.phase==Phase::Exposed||m.phase==Phase::Retained)return false;
  if(m.cleanupAttempted)return m.cleanupSucceeded;
  m.cleanupAttempted=true;
  if(m.treeAttempted&&!io.cleanupTree()){m.phase=Phase::Failed;m.step=Step::Cleanup;return false;}
  m.treeAttempted=false;
  for(uint32_t n=m.buffers;n;--n){auto&b=buffers[n-1];
   if(b.attempted){if(!io.cleanupBuffer(n-1)){m.phase=Phase::Failed;m.step=Step::Cleanup;return false;}b.attempted=false;}
  }
  if(m.scratch){io.freeScratch(m.dataPages);m.scratch=false;}
  if(m.rows){io.freeRows(m.totalRows);m.rows=false;}
  m.phase=Phase::Released;m.cleanupSucceeded=true;m.step=Step::None;return true;
 }
 template<class IO>bool exposedStable(IO&io)const{
  return m.phase==Phase::Exposed&&!m.cleanupAttempted&&stable(io,true)&&io.treeStable();
 }
 //195: extend only the retained bootstrap prefix. All owned data mappings and
 // their original physical pages remain unchanged; the stable tree publisher
 // rejects removal, remapping or changed access. No new mapping is admitted to
 // an application here. Any ambiguous backend update retains the whole arena.
 template<class IO>bool appendPrefix(IO&io,const RTXPageTree167::Mapping*prefix,uint32_t count){
  if(m.phase!=Phase::Exposed||m.cleanupAttempted||count<m.prefixPages||count>RTXPageTree167::MaxMappings-m.dataPages||
     RTXPageTree167::measure(prefix,count).error!=RTXPageTree167::Error::None)return false;
  if(!stable(io,true)||!io.treeStable())return retained(Step::Stability);
  const auto*old=io.rows();
  for(uint32_t i=0;i<count;++i){
   if(prefix[i].owner<=m.buffers||overlap(arenaBegin,arenaEnd-arenaBegin,prefix[i].va,4096))return false;
   if(prefix[i].aperture==RTXPageTree167::Aperture::System)
    for(uint32_t j=0;j<m.totalRows;++j)if(old[j].owner<=m.buffers&&old[j].physical==prefix[i].physical)return false;
  }
  const auto total=count+m.dataPages;
  if(!io.allocatePendingRows(total))return retained(Step::Rows);
  auto*pending=io.pendingRows();if(!pending)return retained(Step::Rows);
  for(uint32_t i=0;i<count;++i)pending[i]=prefix[i];
  uint32_t n=count;
  for(uint32_t i=0;i<m.totalRows;++i)if(old[i].owner<=m.buffers){
   if(n==total)return retained(Step::Rows);pending[n++]=old[i];}
  if(n!=total)return retained(Step::Rows);
  sortRows(pending,total);
  if(RTXPageTree167::measure(pending,total).error!=RTXPageTree167::Error::None)return retained(Step::Rows);
  if(!io.appendTree(pending,total))return retained(Step::Tree);
  if(!io.commitRows(m.totalRows))return retained(Step::Rows);
  m.prefixPages=count;m.totalRows=total;
  return stable(io,true)&&io.treeStable()?true:retained(Step::Stability);
 }
 // A CPU layout description, explicitly not an admitMapping call.
 bool preparedSpan(uint32_t index,uint64_t&session,RTXSpans165::Mapping&out)const{
  if(index>=m.buffers||(m.phase!=Phase::HostReady&&m.phase!=Phase::Exposed))return false;
  const auto&b=buffers[index];session=m.session;
  out={uint64_t(index)+1,uint64_t(index)+1,b.va,b.logical,b.mapped,b.access};return true;
 }
};
}
