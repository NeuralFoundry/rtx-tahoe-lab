#pragma once
#include "NativeRootCapture196.hpp"
#include <limits>

// Used only by the private owner on the same already-open connection. The
// input root Capture is the preceding native collection, never a file import.
namespace RTXNativeData182 {
namespace R=RTXNativeRoot196;
constexpr uint64_t Magic=0x5254584441544131ULL,BudgetNs=30000000000ULL;
class Session {
 enum class Phase {Cold,Collecting,Ready,Transfer,Failed};
 Phase phase=Phase::Cold;
 uint64_t generation=0,totalCalls_=0,operations_=0,start=0,elapsed=0;
 unsigned callCount=0;
 std::array<uint64_t,8>stats{};
 std::array<std::array<uint64_t,8>,R::Buffers>handles{};
 Session(const Session&)=delete;Session&operator=(const Session&)=delete;
 bool fail(){phase=Phase::Failed;return false;}
 template<class IO>bool tick(IO&io){const auto now=io.nowNs();if(now<start||now-start<elapsed)return fail();elapsed=now-start;return elapsed<BudgetNs||fail();}
 template<class IO>bool call(IO&io,unsigned selector,const uint64_t*scalars,unsigned count,const uint8_t*in,size_t n,uint8_t*out,size_t bytes){
  if(callCount>=4096||totalCalls_==UINT64_MAX||!tick(io))return fail();++callCount;++totalCalls_;
  if(!io.call(selector,scalars,count,in,n,out,bytes))return fail();return tick(io);
 }
 template<class IO,class Sink>bool summary(IO&io,Sink&sink,const char*name,const std::array<uint64_t,8>&expected){
  std::array<uint8_t,64>raw{};
  if(!call(io,97,nullptr,0,nullptr,0,raw.data(),raw.size())||!sink.save(name,raw.data(),raw.size()))return fail();
  for(unsigned i=0;i<8;++i)if(R::u64(raw.data()+i*8)!=expected[i])return fail();return true;
 }
public:
 Session()=default;
 bool ready()const{return phase==Phase::Ready;}
 bool failed()const{return phase==Phase::Failed;}
 void invalidate(){phase=Phase::Failed;}
 uint64_t calls()const{return totalCalls_;}
 uint64_t operations()const{return operations_;}
 uint64_t session()const{return generation;}
 uint64_t capacity(unsigned slot)const{return ready()&&slot<R::Buffers?handles[slot][5]:0;}
 template<class IO,class Sink>bool collect(IO&io,Sink&sink,const R::Capture&root){
  if(phase!=Phase::Cold)return fail();phase=Phase::Collecting;
  try{
   if(!root.passed||root.handles.size()!=R::Buffers*64||root.rows.size()!=root.info.w[7]*40)return fail();
   R::Info decoded;if(!R::decode(root.before.data(),512,root.info.w[2],decoded)||decoded.w!=root.info.w||root.before!=root.after)return fail();
   generation=root.info.w[2];start=io.nowNs();if(!start)return fail();elapsed=0;callCount=0;
   stats={Magic,181,generation,1,0,0,0,0};
   if(!summary(io,sink,"owned-data-info-before.bin",stats))return false;
   std::vector<uint8_t>pages;pages.reserve(R::DataMaps*40);unsigned row=unsigned(root.info.w[55]);uint64_t va=R::L::Begin;
   for(unsigned b=0;b<R::Buffers;++b){const auto&d=R::L::Description[b];
    const uint64_t mapped=R::L::mapped(b),expected[]={generation,(1ULL<<32)|(b+1),b+1,b+1,va,d.bytes,mapped,d.access};
    for(unsigned f=0;f<8;++f){handles[b][f]=R::u64(root.handles.data()+b*64+f*8);if(handles[b][f]!=expected[f])return fail();}
    for(uint64_t p=0;p<mapped/4096;++p){const uint64_t scalars[]={generation,handles[b][1],p};std::array<uint8_t,32>raw{};
     if(!call(io,100,scalars,3,nullptr,0,raw.data(),raw.size()))return false;
     const uint8_t*record=root.rows.data()+row*40;
     if(R::u64(record)!=va+p*4096||R::u64(record+16)!=uint64_t(b)+1||R::u32(record+24)!=2||R::u32(record+28)!=((d.access&2)?2u:1u)||R::u32(record+32)||R::u32(record+36))return fail();
     const auto physical=R::u64(record+8);
     if(R::u64(raw.data())!=p||R::u64(raw.data()+8)!=physical||R::u64(raw.data()+16)!=physical||R::u64(raw.data()+24)<4096)return fail();
     const auto off=pages.size();pages.resize(off+40);R::put64(pages.data()+off,b);std::memcpy(pages.data()+off+8,raw.data(),32);++row;++stats[4];
    }
    va+=mapped+4096;
   }
   if(row!=root.info.w[7]||pages.size()!=R::DataMaps*40||!sink.save("owned-data-pages.bin",pages.data(),pages.size()))return fail();
   if(!summary(io,sink,"owned-data-info-after.bin",stats))return false;
   if(!tick(io))return false;phase=Phase::Ready;return true;
  }catch(...){return fail();}
 }
 template<class IO,class Sink>bool transfer(IO&io,Sink&sink,unsigned slot,uint64_t offset,const uint8_t*upload,size_t bytes,std::vector<uint8_t>&readback,bool writing){
  if(!ready()||slot>=R::Buffers||!bytes||bytes>capacity(slot)||offset>capacity(slot)||bytes>capacity(slot)-offset||
     (writing?!upload:upload!=nullptr)||operations_==UINT64_MAX)return false;
  try{
   // Allocate readback before any I/O. Failure does not leave a half-published
   // application result; the caller publishes only this complete private copy.
   std::vector<uint8_t>result(writing?0:bytes);
   start=io.nowNs();if(!start)return fail();elapsed=0;callCount=0;phase=Phase::Transfer;++operations_;
   if(!summary(io,sink,"data-info-before.bin",stats))return false;
   if(writing&&!sink.save("data-upload.bin",upload,bytes))return fail();
   for(size_t off=0;off<bytes;off+=4096){const auto n=std::min<size_t>(4096,bytes-off);const uint64_t scalars[]={generation,handles[slot][1],offset+off,n};
    if(stats[4]==UINT64_MAX||stats[writing?5:6]==UINT64_MAX)return fail();
    if(!call(io,writing?98:99,scalars,4,writing?upload+off:nullptr,writing?n:0,writing?nullptr:result.data()+off,writing?0:n))return false;
    ++stats[4];++stats[writing?5:6];
   }
   if(!summary(io,sink,"data-info-after.bin",stats))return false;
   if(!writing&&!sink.save("data-readback.bin",result.data(),result.size()))return fail();
   if(!tick(io))return false;readback.swap(result);phase=Phase::Ready;return true;
  }catch(...){return fail();}
 }
};
}
