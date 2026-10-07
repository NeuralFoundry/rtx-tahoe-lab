#pragma once
#include "../root170/OwnedPageTree167.hpp"

// Private, serialized native-owner code. No allocation, MMIO, RPC, or user ABI.
// Every DMA page comes from the retained owner. A new mapping may consume a
// previously unused table page, but existing nodes keep their physical slots.
// Planning only changes caller-owned staging; rejection never touches old data.
namespace RTXStableTree194 {
namespace P=RTXPageTree167;
constexpr uint32_t MaxCapacity=256;
constexpr unsigned Shifts[4]={47,38,29,21};
struct Node {uint64_t key=0;uint32_t depth=0,reserved=0;};
struct View {
 const P::Mapping*rows=nullptr;uint32_t count=0;
 const uint64_t*pages=nullptr;uint32_t capacity=0;
 const Node*nodes=nullptr;uint32_t used=0;
 const uint8_t*image=nullptr;
};
enum class Error:uint32_t {None,Shape,Aliasing,Mapping,Pages,Topology,Image,Capacity,ChangedMapping,ChangedPage,ChangedNode,ChangedEntry};
struct Result {
 Error error=Error::None;uint32_t used=0,mappings=0,addedMappings=0,changedPages=0,addedEntries=0;
 uint64_t root=0;
};
inline uint64_t get64(const uint8_t*p){uint64_t n=0;for(unsigned i=0;i<8;++i)n|=uint64_t(p[i])<<(8*i);return n;}
inline uint32_t find(const Node*n,uint32_t count,uint32_t depth,uint64_t key){
 for(uint32_t i=0;i<count;++i)if(n[i].depth==depth&&n[i].key==key)return i;return UINT32_MAX;
}
inline uint32_t lower(const P::Mapping*m,uint32_t count,uint64_t va){
 uint32_t a=0,b=count;while(a<b){const auto mid=a+(b-a)/2;if(m[mid].va<va)a=mid+1;else b=mid;}return a;
}
inline bool same(const P::Mapping&a,const P::Mapping&b){
 return a.va==b.va&&a.physical==b.physical&&a.owner==b.owner&&a.aperture==b.aperture&&
  a.access==b.access&&a.cached==b.cached&&a.reserved==b.reserved;
}
inline bool nodeEqual(const Node&a,const Node&b){return a.key==b.key&&a.depth==b.depth&&a.reserved==b.reserved;}
inline bool shaped(const View&v){
 return v.capacity>=5&&v.capacity<=MaxCapacity&&v.used>=5&&v.used<=v.capacity&&
  v.count&&v.count<=P::MaxMappings&&P::validRange(v.rows,size_t(v.count)*sizeof(*v.rows))&&
  P::validRange(v.pages,size_t(v.capacity)*8)&&P::validRange(v.nodes,size_t(v.capacity)*sizeof(Node))&&
  P::validRange(v.image,size_t(v.capacity)*4096)&&
  !(reinterpret_cast<uintptr_t>(v.rows)%alignof(P::Mapping))&&
  !(reinterpret_cast<uintptr_t>(v.pages)%alignof(uint64_t))&&
  !(reinterpret_cast<uintptr_t>(v.nodes)%alignof(Node));
}
inline uint64_t entry(const View&v,uint32_t slot,uint32_t word){
 const auto&n=v.nodes[slot];
 if(n.depth==4){const auto va=(n.key<<21)+uint64_t(word)*4096;const auto at=lower(v.rows,v.count,va);
  return at<v.count&&v.rows[at].va==va?P::pte(v.rows[at]):0;}
 if((n.depth==0&&word>=4)||(n.depth==3&&!(word&1)))return 0;
 const uint32_t index=n.depth==3?word/2:word;
 const uint64_t childKey=n.depth==0?index:(n.key<<(Shifts[n.depth-1]-Shifts[n.depth]))+index;
 const auto child=find(v.nodes,v.used,n.depth+1,childKey);
 // PD1 disables ATS. PD0's unused big half is zero, as in the 167 encoder.
 return child==UINT32_MAX?0:(v.pages[child]>>4)|12ULL|(n.depth==2?32ULL:0ULL);
}
inline Error validate(const View&v){
 if(!shaped(v))return Error::Shape;
 const auto measure=P::measure(v.rows,v.count);
 if(measure.error!=P::Error::None)return Error::Mapping;
 if(measure.tables!=v.used)return Error::Topology;
 for(uint32_t i=0;i<v.capacity;++i){
  if(v.pages[i]<4096||v.pages[i]%4096||v.pages[i]>P::SysLimit-4096)return Error::Pages;
  for(uint32_t j=0;j<i;++j)if(v.pages[i]==v.pages[j])return Error::Pages;
 }
 for(uint32_t i=0;i<v.count;++i)if(v.rows[i].aperture==P::Aperture::System)
  for(uint32_t j=0;j<v.capacity;++j)if(v.rows[i].physical==v.pages[j])return Error::Pages;
 if(v.nodes[0].depth||v.nodes[0].key||v.nodes[0].reserved)return Error::Topology;
 for(uint32_t i=1;i<v.used;++i){const auto&n=v.nodes[i];
  if(!n.depth||n.depth>4||n.reserved||n.key>=uint64_t(1)<<(49-Shifts[n.depth-1]))return Error::Topology;
  if(find(v.nodes,i,n.depth,n.key)!=UINT32_MAX)return Error::Topology;
  const uint64_t parent=n.depth==1?0:n.key>>(Shifts[n.depth-2]-Shifts[n.depth-1]);
  if(find(v.nodes,i,n.depth-1,parent)==UINT32_MAX)return Error::Topology;
  const auto at=lower(v.rows,v.count,n.key<<Shifts[n.depth-1]);
  if(at==v.count||v.rows[at].va>>Shifts[n.depth-1]!=n.key)return Error::Topology;
 }
 for(uint32_t i=0;i<v.capacity;++i){
  if(i>=v.used&&(v.nodes[i].key||v.nodes[i].depth||v.nodes[i].reserved))return Error::Topology;
  for(uint32_t w=0;w<512;++w)if(get64(v.image+size_t(i)*4096+w*8)!=(i<v.used?entry(v,i,w):0))return Error::Image;
 }
 return Error::None;
}
inline Error monotonic(const View&before,const View&after){
 if(before.capacity!=after.capacity||after.used<before.used||after.count<before.count)return Error::Capacity;
 for(uint32_t i=0;i<before.capacity;++i)if(before.pages[i]!=after.pages[i])return Error::ChangedPage;
 for(uint32_t i=0;i<before.used;++i)if(!nodeEqual(before.nodes[i],after.nodes[i]))return Error::ChangedNode;
 for(uint32_t i=0;i<before.count;++i){const auto at=lower(after.rows,after.count,before.rows[i].va);
  if(at==after.count||!same(before.rows[i],after.rows[at]))return Error::ChangedMapping;}
 for(size_t off=0;off<size_t(before.capacity)*4096;off+=8){const auto old=get64(before.image+off);
  if(old&&get64(after.image+off)!=old)return Error::ChangedEntry;}
 return Error::None;
}

// Output arrays have exactly capacity slots/pages and may change on rejection.
// They MUST be private staging. Existing images, metadata and mappings are read-only.
inline Result plan(const P::Mapping*rows,uint32_t count,const uint64_t*pages,uint32_t capacity,
                   const View*before,Node*nodes,uint8_t*image){
 Result r;
 auto fail=[&](Error e){r.error=e;r.root=0;return r;};
 if(capacity<5||capacity>MaxCapacity||!count||count>P::MaxMappings)return fail(Error::Shape);
 const void*inputs[8]={rows,pages,before,nullptr,nullptr,nullptr,nullptr,nullptr};
 size_t sizes[8]={size_t(count)*sizeof(*rows),size_t(capacity)*8,before?sizeof(*before):0,0,0,0,0,0};
 if(before){if(!shaped(*before))return fail(Error::Shape);
  inputs[3]=before->rows;sizes[3]=size_t(before->count)*sizeof(*rows);
  inputs[4]=before->pages;sizes[4]=size_t(before->capacity)*8;
  inputs[5]=before->nodes;sizes[5]=size_t(before->capacity)*sizeof(Node);
  inputs[6]=before->image;sizes[6]=size_t(before->capacity)*4096;}
 const void*outputs[2]={nodes,image};const size_t outputBytes[2]={size_t(capacity)*sizeof(Node),size_t(capacity)*4096};
 if(reinterpret_cast<uintptr_t>(rows)%alignof(P::Mapping)||reinterpret_cast<uintptr_t>(pages)%8||
    reinterpret_cast<uintptr_t>(nodes)%alignof(Node))return fail(Error::Shape);
 for(unsigned i=0;i<7;++i)if(sizes[i]&&!P::validRange(inputs[i],sizes[i]))return fail(Error::Shape);
 for(unsigned i=0;i<2;++i){if(!P::validRange(outputs[i],outputBytes[i]))return fail(Error::Shape);
  for(unsigned j=0;j<7;++j)if(sizes[j]&&P::overlap(outputs[i],outputBytes[i],inputs[j],sizes[j]))return fail(Error::Aliasing);}
 if(P::overlap(nodes,outputBytes[0],image,outputBytes[1]))return fail(Error::Aliasing);
 const auto shape=P::measure(rows,count);
 if(shape.error!=P::Error::None)return fail(Error::Mapping);
 if(shape.tables>capacity)return fail(Error::Capacity);
 if(before){const auto e=validate(*before);if(e!=Error::None)return fail(e);
  if(capacity!=before->capacity)return fail(Error::Capacity);}
 for(uint32_t i=0;i<capacity;++i)nodes[i]=before&&i<before->used?before->nodes[i]:Node{};
 for(size_t i=0;i<size_t(capacity)*4096;++i)image[i]=0;
 uint32_t used=before?before->used:1;
 for(uint32_t i=0;i<count;++i)for(uint32_t d=1;d<=4;++d){const auto key=rows[i].va>>Shifts[d-1];
  if(find(nodes,used,d,key)==UINT32_MAX){if(used==capacity)return fail(Error::Capacity);nodes[used++]={key,d,0};}}
 View after{rows,count,pages,capacity,nodes,used,image};
 for(uint32_t i=0;i<used;++i)for(uint32_t w=0;w<512;++w)P::put64(image+size_t(i)*4096+w*8,entry(after,i,w));
 const auto check=validate(after);if(check!=Error::None)return fail(check);
 if(before){const auto e=monotonic(*before,after);if(e!=Error::None)return fail(e);}
 for(uint32_t i=0;i<used;++i){bool changed=false;
  for(uint32_t w=0;w<512;++w){const auto at=size_t(i)*4096+w*8;const auto old=before?get64(before->image+at):0,now=get64(image+at);
   if(now!=old){changed=true;++r.addedEntries;}}
  if(changed)++r.changedPages;
 }
 r.used=used;r.mappings=count;r.addedMappings=count-(before?before->count:0);r.root=pages[0];return r;
}

enum class PublishPhase:uint32_t {Idle,Attempted,Committed,Rejected,Retained};
enum class PublishError:uint32_t {None,Shape,Readiness,Physical,InitialReadback,Write,Publish,Readback,Invalidate};
struct PublishInfo {
 PublishPhase phase=PublishPhase::Idle;PublishError error=PublishError::None;
 uint32_t writes=0,publishes=0,depths=0;bool invalidateAttempted=false,invalidated=false;
 uint64_t root=0;
};
// One attempt per object. io.ready() includes the exclusive native coordinator,
// retained data/pages, and no outstanding GPU command. io.writeEntry() must be
// an aligned atomic 64-bit store into owned coherent DMA memory. Child depth is
// published/read back before a parent pointer is made visible. Any ambiguous
// write/publication/invalidation keeps the complete tree and data retained.
class Publisher {
 PublishInfo m;
 Publisher(const Publisher&)=delete;Publisher&operator=(const Publisher&)=delete;
 bool fail(PublishError e,bool touched){m.error=e;m.phase=touched?PublishPhase::Retained:PublishPhase::Rejected;return false;}
public:
 Publisher()=default;
 const PublishInfo&info()const{return m;}
 template<class IO>bool apply(IO&io,const View*before,const View&after){
  if(m.phase!=PublishPhase::Idle)return false;m.phase=PublishPhase::Attempted;
  if(validate(after)!=Error::None||(before&&(validate(*before)!=Error::None||monotonic(*before,after)!=Error::None)))return fail(PublishError::Shape,false);
  m.root=after.pages[0];
  if(!io.ready())return fail(PublishError::Readiness,false);
  if(!io.physicalPagesEqual(after.pages,after.capacity))return fail(PublishError::Physical,false);
  // On initial publication the backing must be freshly zeroed; subsequent
  // updates compare the complete retained shadow, including unused pool pages.
  if(!io.equals(before?before->image:nullptr,size_t(after.capacity)*4096))return fail(PublishError::InitialReadback,false);
  bool touched=false;
  for(int depth=4;depth>=0;--depth){bool changed=false;
   for(uint32_t i=0;i<after.used;++i)if(after.nodes[i].depth==uint32_t(depth))
    for(uint32_t w=0;w<512;++w){const auto off=size_t(i)*4096+w*8;
     const auto old=before?get64(before->image+off):0,now=get64(after.image+off);
     if(old==now)continue;
     if(!io.ready())return fail(PublishError::Readiness,touched);
     touched=changed=true;++m.writes;
     if(!io.writeEntry(off,now))return fail(PublishError::Write,true);
    }
   if(!changed)continue;
   ++m.publishes;if(!io.publish())return fail(PublishError::Publish,true);
   if(!io.physicalPagesEqual(after.pages,after.capacity))return fail(PublishError::Physical,true);
   for(uint32_t i=0;i<after.used;++i)if(after.nodes[i].depth==uint32_t(depth)&&!io.pageEquals(i,after.image+size_t(i)*4096))return fail(PublishError::Readback,true);
   ++m.depths;
  }
  if(!io.ready()||!io.physicalPagesEqual(after.pages,after.capacity))return fail(PublishError::Physical,touched);
  if(!io.equals(after.image,size_t(after.capacity)*4096))return fail(PublishError::Readback,touched);
  if(before&&touched){m.invalidateAttempted=true;if(!io.invalidate(m.root))return fail(PublishError::Invalidate,true);m.invalidated=true;}
  if(!io.ready()||!io.physicalPagesEqual(after.pages,after.capacity))return fail(PublishError::Physical,touched);
  // Initial publication is still host preparation, NOT a root RPC ACK.
  m.phase=PublishPhase::Committed;return true;
 }
};
}
