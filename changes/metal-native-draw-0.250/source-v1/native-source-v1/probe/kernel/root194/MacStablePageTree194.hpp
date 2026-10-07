#pragma once
#include "../root170/MacMetalDmaBacking166.hpp"
#include "StablePageTree194.hpp"

// Owner-held kernel adapter; not yet connected to the external VAS bootstrap.
// The caller holds the open PCI provider, all DATA backing and the sleepable
// coordinator mutex. Ready checks must exclude outstanding GPU jobs. Pool DMA
// addresses never change, and failed exposed updates are retained until reboot.
class MacStablePageTree194 {
 static constexpr uint32_t Capacity=64,MaxRows=8192;
 static constexpr size_t ImageBytes=size_t(Capacity)*4096;
 struct Slot {
  RTXPageTree167::Mapping*rows=nullptr;RTXStableTree194::Node*nodes=nullptr;uint8_t*image=nullptr;
  uint32_t count=0,used=0;
 }slots[2];
 MacMetalDmaBacking166 backing;
 uint64_t pinnedPages[Capacity]{};
 unsigned current=0;
 enum class Phase:uint32_t {Idle,Preparing,HostReady,Exposed,Updating,Failed,Retained,Released};
 Phase phase=Phase::Idle;
 RTXStableTree194::Result planResult;
 RTXStableTree194::PublishInfo publication;
 bool cleanupAttempted=false;
 RTXStableTree194::View view(unsigned i)const{
  const auto&s=slots[i];return{s.rows,s.count,pinnedPages,Capacity,s.nodes,s.used,s.image};
 }
 bool allocate(Slot&s){
  s.rows=static_cast<RTXPageTree167::Mapping*>(IOMalloc(size_t(MaxRows)*sizeof(*s.rows)));
  s.nodes=static_cast<RTXStableTree194::Node*>(IOMalloc(size_t(Capacity)*sizeof(*s.nodes)));
  s.image=static_cast<uint8_t*>(IOMalloc(ImageBytes));return s.rows&&s.nodes&&s.image;
 }
 bool copyRows(unsigned i,const RTXPageTree167::Mapping*rows,uint32_t count){
  if(!count||count>MaxRows||RTXPageTree167::measure(rows,count).error!=RTXPageTree167::Error::None)return false;
  const auto bytes=size_t(count)*sizeof(*rows);
  for(const auto&s:slots)
   if(RTXPageTree167::overlap(rows,bytes,s.rows,size_t(MaxRows)*sizeof(*rows))||
      RTXPageTree167::overlap(rows,bytes,s.nodes,size_t(Capacity)*sizeof(*s.nodes))||
      RTXPageTree167::overlap(rows,bytes,s.image,ImageBytes))return false;
  bcopy(rows,slots[i].rows,bytes);slots[i].count=count;return true;
 }
 bool stablePhysical()const{
  const auto&b=backing.info();
  if(!b.ready||b.cleanupAttempted||b.pages!=Capacity||!backing.pages())return false;
  for(uint32_t i=0;i<Capacity;++i){uint64_t pa=0,extent=0;
   if(backing.pages()[i]!=pinnedPages[i]||!backing.physicalPage(i,pa,extent)||pa!=pinnedPages[i]||extent<4096)return false;}
  return true;
 }
 bool equals(uint64_t off,const uint8_t*expected,size_t n)const{
  uint8_t scratch[256];
  for(size_t i=0;i<n;i+=sizeof(scratch)){const auto count=n-i<sizeof(scratch)?n-i:sizeof(scratch);
   if(!backing.read(off+i,scratch,count))return false;
   for(size_t j=0;j<count;++j)if(scratch[j]!=(expected?expected[i+j]:0))return false;}
  return true;
 }
 template<class Ready,class Invalidate>struct IO {
  MacStablePageTree194&s;Ready&live;Invalidate&flush;
  bool ready(){return live()&&(s.phase==Phase::Preparing||s.phase==Phase::Updating)&&s.backing.info().ready&&!s.cleanupAttempted;}
  bool physicalPagesEqual(const uint64_t*p,uint32_t n){
   if(n!=Capacity||!s.stablePhysical())return false;
   for(uint32_t i=0;i<n;++i)if(p[i]!=s.pinnedPages[i])return false;return true;
  }
  bool equals(const uint8_t*p,size_t n){return n==ImageBytes&&s.equals(0,p,n);}
  bool writeEntry(size_t off,uint64_t value){return s.backing.writeTableEntry(off,value);}
  bool publish(){return s.backing.publish();}
  bool pageEquals(uint32_t i,const uint8_t*p){return i<Capacity&&s.equals(uint64_t(i)*4096,p,4096);}
  bool invalidate(uint64_t root){return root==s.pinnedPages[0]&&ready()&&flush(root);}
 };
 MacStablePageTree194(const MacStablePageTree194&)=delete;MacStablePageTree194&operator=(const MacStablePageTree194&)=delete;
public:
 explicit MacStablePageTree194(IOPCIDevice*p):backing(p){}
 ~MacStablePageTree194()=default;
 template<class Ready>bool prepare(const RTXPageTree167::Mapping*rows,uint32_t count,Ready&ready){
  if(phase!=Phase::Idle||cleanupAttempted)return false;phase=Phase::Preparing;
  auto fail=[&](){phase=Phase::Failed;return false;};
  if(!ready()||!allocate(slots[0])||!allocate(slots[1])||!copyRows(0,rows,count)||!backing.prepare(ImageBytes))return fail();
  for(uint32_t i=0;i<Capacity;++i)pinnedPages[i]=backing.pages()[i];
  planResult=RTXStableTree194::plan(slots[0].rows,count,pinnedPages,Capacity,nullptr,slots[0].nodes,slots[0].image);
  if(planResult.error!=RTXStableTree194::Error::None)return fail();slots[0].used=planResult.used;
  auto noInvalidate=[](uint64_t){return false;};IO<Ready,decltype(noInvalidate)>io{*this,ready,noInvalidate};RTXStableTree194::Publisher p;
  const bool passed=p.apply(io,nullptr,view(0));publication=p.info();
  if(!passed||!stablePhysical())return fail();phase=Phase::HostReady;return true;
 }
 bool expose(){
  if(phase!=Phase::HostReady||cleanupAttempted||!stablePhysical()||!equals(0,slots[current].image,ImageBytes))return false;
  // Set retained state before the operation; ambiguous exposure is never freed.
  phase=Phase::Retained;if(!backing.expose())return false;phase=Phase::Exposed;return true;
 }
 template<class Ready,class Invalidate>bool append(const RTXPageTree167::Mapping*rows,uint32_t count,Ready&ready,Invalidate&invalidate){
  if(phase!=Phase::Exposed||cleanupAttempted||!ready()||!stablePhysical())return false;
  const unsigned pending=current^1U;
  if(!copyRows(pending,rows,count))return false;
  const auto prior=view(current);
  planResult=RTXStableTree194::plan(slots[pending].rows,count,pinnedPages,Capacity,&prior,slots[pending].nodes,slots[pending].image);
  if(planResult.error!=RTXStableTree194::Error::None)return false;slots[pending].used=planResult.used;
  phase=Phase::Updating;IO<Ready,Invalidate>io{*this,ready,invalidate};RTXStableTree194::Publisher p;
  const bool passed=p.apply(io,&prior,view(pending));publication=p.info();
  if(!passed){phase=Phase::Retained;return false;}
  current=pending;phase=Phase::Exposed;return true;
 }
 bool exposedStable()const{
  return phase==Phase::Exposed&&!cleanupAttempted&&backing.info().exposed&&
   backing.info().writeEpoch==backing.info().publishedEpoch&&stablePhysical();
 }
 uint64_t root()const{return (phase==Phase::HostReady||phase==Phase::Exposed)?pinnedPages[0]:0;}
 uint32_t used()const{return slots[current].used;}
 uint32_t mappings()const{return slots[current].count;}
 bool hostReadyStable()const{return phase==Phase::HostReady&&!cleanupAttempted&&backing.info().writeEpoch==backing.info().publishedEpoch&&stablePhysical();}
 uint32_t capacity()const{return Capacity;}
 bool tablePage(uint32_t i,uint64_t&pa,uint64_t&extent)const{return i<Capacity&&backing.physicalPage(i,pa,extent);}
 bool importForObservation(){return exposedStable()&&backing.importForObservation();}
 const RTXStableTree194::PublishInfo&lastPublication()const{return publication;}
 const RTXStableTree194::Result&lastPlan()const{return planResult;}
 bool read(uint64_t off,void*out,uint64_t n)const{return n<=4096&&backing.read(off,out,n);}
 bool cleanup(){
  if(phase==Phase::Exposed||phase==Phase::Updating||phase==Phase::Retained)return false;
  if(cleanupAttempted)return phase==Phase::Released;cleanupAttempted=true;
  if(!backing.cleanup())return false;
  for(auto&s:slots){
   if(s.rows){IOFree(s.rows,size_t(MaxRows)*sizeof(*s.rows));s.rows=nullptr;}
   if(s.nodes){IOFree(s.nodes,size_t(Capacity)*sizeof(*s.nodes));s.nodes=nullptr;}
   if(s.image){IOFree(s.image,ImageBytes);s.image=nullptr;}
  }
  phase=Phase::Released;return true;
 }
};
