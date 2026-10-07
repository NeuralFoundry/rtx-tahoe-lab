#pragma once
#include "NativeDataSession182.hpp"
#include "../probe/kernel/root179/OwnedBufferSpans165.hpp"
#include "../probe/kernel/ReusableRuntime.hpp"
#include <memory>

// Private owner component. The adapter serializes this object together with
// bootstrap, data transfer, close and IOConnectCallMethod on ONE connection.
// No file import or caller-supplied GPU address is an admission mechanism.
namespace RTXNativeDispatch185 {
namespace P=RtxProgram164;namespace Q=P::Q;namespace R=RTXNativeRoot196;
namespace B=RtxReusableBacking035;namespace N=RtxReusable035;
using Bytes=std::vector<uint8_t>;
constexpr uint64_t Magic=0x5254584744503138ULL,BudgetNs=30000000000ULL;
struct Binding {uint32_t slot=0,index=0;uint64_t offset=0,bytes=0;};
enum class Failure {None,State,Clock,Call,Save,Info,Capture,Program,Mapping,Completion};
class Session {
 enum class Phase {Cold,Preparing,Ready,Executing,Failed};
 Phase phase=Phase::Cold;Failure error=Failure::None;
 uint64_t generation=0,completed_=0,ticket_=0,start=0,elapsed_=0;
 unsigned calls_=0;uint64_t totalCalls_=0;
 P::Library decoded{};std::array<uint8_t,4608>payload{};
 std::array<uint8_t,128>lastInfo{};std::array<uint8_t,B::DeviceBytes>canonical{};
 std::array<RTXSpans165::Mapping,4>mappings{};std::array<uint64_t,4>handles{};
 Session(const Session&)=delete;Session&operator=(const Session&)=delete;
 bool fail(Failure f){if(error==Failure::None)error=f;phase=Phase::Failed;return false;}
 template<class IO>bool tick(IO&io){const auto now=io.nowNs();if(!start||now<start||now-start<elapsed_||now-start>=BudgetNs)return fail(Failure::Clock);elapsed_=now-start;return true;}
 template<class IO>bool call(IO&io,unsigned sel,const uint64_t*sc,unsigned count,const uint8_t*in,size_t n,uint8_t*out,size_t cap){
  if(!tick(io)||calls_>=128||totalCalls_==UINT64_MAX)return fail(Failure::Clock);++calls_;++totalCalls_;
  if(!io.call(sel,sc,count,in,n,out,cap))return fail(Failure::Call);return tick(io);
 }
 template<class Sink>bool save(Sink&sink,const char*name,const uint8_t*p,size_t n){return sink.save(name,p,n)||fail(Failure::Save);}
 template<class IO,class Sink>bool info(IO&io,Sink&sink,const char*name,std::array<uint8_t,128>&raw){return call(io,101,nullptr,0,nullptr,0,raw.data(),128)&&save(sink,name,raw.data(),128);}
 template<class IO,class Sink>bool capture(IO&io,Sink&sink,unsigned part,const char*name,Bytes&raw,size_t n){
  raw.resize(n);for(size_t off=0;off<n;off+=4096){const size_t count=std::min<size_t>(4096,n-off);const uint64_t sc[]={generation,part,off,count};
   if(!call(io,106,sc,4,nullptr,0,raw.data()+off,count))return false;
  }return save(sink,name,raw.data(),n);
 }
 bool expected(const P::Program&p,const RTXSpans165::Plan&plan,uint64_t serial,Bytes&qmd,Bytes&cb,Bytes&command,Bytes&ring){
  qmd.resize(256);cb.resize(4096);command.resize(56);ring.resize(8);
  if(!Q::build(qmd.data(),256,cb.data(),4096,command.data(),32))return false;
  const uint64_t va=Q::ProgramVA+p.offset;
  const struct Field {Q::B::Field bits;uint32_t value;} fields[]={
   {{256,32},uint32_t(va>>8)},{{1632,9},uint32_t(va>>40)},{{1536,32},uint32_t(va)},{{1568,17},uint32_t(va>>32)},
   {{1641,9},(p.bytes+255)/256},{{648,9},p.registers},{{1075,13},(p.constantBytes+15)/16},
   {{829,1},1},{{830,2},2},{{832,32},uint32_t(serial)},{{864,32},uint32_t(serial>>32)}};
  for(const auto&f:fields)if(!Q::B::put(qmd.data(),256,f.bits,f.value))return false;
  if(!RTXSpans165::patch(plan,p,qmd.data(),256,cb.data(),4096))return false;
  const uint32_t tail[]={0x20050017,uint32_t(N::TimelineVA),uint32_t(N::TimelineVA>>32),uint32_t(serial),uint32_t(serial>>32),N::ReleaseWfi64};
  for(unsigned i=0;i<6;++i)Q::put32(command.data()+32+i*4,tail[i]);
  Q::put64(ring.data(),RtxProgram033::commandVA(0)|(1ULL<<41)|(14ULL<<42));return true;
 }
 bool completedInfo(const std::array<uint8_t,128>&raw,uint64_t serial)const{
  uint64_t w[16];for(unsigned i=0;i<16;++i)w[i]=R::u64(raw.data()+i*8);
  return w[0]==Magic&&w[1]==183&&w[2]==generation&&w[3]==2&&!w[4]&&w[5]==serial&&w[6]>ticket_&&
   w[7]==7&&w[8]==1&&w[9]>=1&&w[9]<=4092&&w[10]<5000000000ULL&&w[11]==1&&w[12]==3&&w[13]==1&&w[14]==4096&&w[15]==w[9]+4;
 }
 bool journal(const Bytes&raw,uint64_t serial,uint64_t elapsed)const{
  const auto count=raw.size()/40;const unsigned old=N::entryIndex(serial),next=N::nextIndex(serial);uint64_t prior=0;
  if(raw.size()%40||count<5||count>4096)return false;
  for(size_t i=0;i<count;++i){const auto*p=raw.data()+i*40;const auto stage=R::u64(p),time=R::u64(p+8),queue=R::u64(p+16),qmd=R::u64(p+24),host=R::u64(p+32);
   const uint64_t expectedStage=i<3?i+1:i+1==count?5:4;
   if(stage!=expectedStage||time<prior||time>elapsed)return false;prior=time;
   const unsigned get=uint32_t(queue),put=uint32_t(queue>>32);
   if(i<3){if(get!=old||put!=old||qmd!=(i==2?0:completed_)||host!=completed_)return false;}
   else{
    if((get!=old&&get!=next)||put!=next||(qmd&&qmd!=serial)||(host!=completed_&&host!=serial)||(host==serial&&qmd!=serial))return false;
    const bool complete=get==next&&qmd==serial&&host==serial;
    if(i+2>=count){if(!complete)return false;}else if(complete)return false;
   }
  }return true;
 }
public:
 Session()=default;
 bool active()const{return phase!=Phase::Cold;}bool ready()const{return phase==Phase::Ready;}bool failed()const{return phase==Phase::Failed;}
 Failure failure()const{return error;}uint64_t completed()const{return completed_;}uint64_t calls()const{return totalCalls_;}
 unsigned operationCalls()const{return calls_;}uint64_t elapsed()const{return elapsed_;}
 void invalidate(){fail(Failure::State);}
 // Bootstrap supplies only its already validated pristine device capture;
 // data/root are the same native collection used by the owning connection.
 template<class IO,class Sink,class Bootstrap,class Data>bool begin(IO&io,Sink&sink,const R::Capture&root,const Data&data,const Bootstrap&bootstrap,const uint8_t*image,size_t n){
  if(active()||!image||n!=4608||!root.passed||root.handles.size()!=256||!data.ready()||data.session()!=root.info.w[2])return false;
  try{
   std::array<uint8_t,4608>copy{};std::memcpy(copy.data(),image,n);P::Library checked{};
   if(!P::decode(copy.data(),512,copy.data()+512,4096,checked))return false;
   if(!bootstrap.copyPristineDevice(canonical))return false;
   R::Info rootInfo;if(!R::decode(root.before.data(),512,data.session(),rootInfo)||rootInfo.w!=root.info.w||root.before!=root.after)return false;
   uint64_t va=0x2000000000ULL;const uint64_t sizes[]={4097,65537,1048577,4194305};
   for(unsigned i=0;i<4;++i){const auto*p=root.handles.data()+i*64;const uint64_t mapped=(sizes[i]+4095)&~4095ULL;
    const uint64_t want[]={data.session(),(1ULL<<32)|(i+1),i+1,i+1,va,sizes[i],mapped,3};
    for(unsigned f=0;f<8;++f)if(R::u64(p+8*f)!=want[f])return false;
    handles[i]=want[1];mappings[i]={want[2],want[3],want[4],want[5],want[6],3};va+=mapped+4096;
   }
   phase=Phase::Preparing;generation=data.session();payload=copy;decoded=checked;start=io.nowNs();elapsed_=0;calls_=0;
   std::array<uint8_t,128>before{},after{},stable{},want{};Q::put64(want.data(),Magic);Q::put64(want.data()+8,183);Q::put64(want.data()+16,generation);
   if(!info(io,sink,"dispatch-cold.bin",before)||before!=want)return fail(Failure::Info);
   if(!save(sink,"baseline.bin",canonical.data(),canonical.size())||!save(sink,"payload.bin",payload.data(),payload.size()))return false;
   if(!call(io,102,&generation,1,payload.data(),512,nullptr,0))return false;
   for(unsigned off=0;off<4096;off+=1024){const uint64_t sc[]={generation,off,1024};if(!call(io,103,sc,3,payload.data()+512+off,1024,nullptr,0))return false;}
   if(!call(io,104,&generation,1,nullptr,0,nullptr,0))return false;
   Q::put64(want.data()+24,2);Q::put64(want.data()+104,1);Q::put64(want.data()+112,4096);
   if(!info(io,sink,"dispatch-ready.bin",after)||after!=want)return fail(Failure::Info);
   Bytes library,code;if(!capture(io,sink,9,"library.bin",library,512)||!capture(io,sink,10,"code.bin",code,4096))return false;
   if(std::memcmp(library.data(),payload.data(),512)||std::memcmp(code.data(),payload.data()+512,4096))return fail(Failure::Program);
   if(!info(io,sink,"dispatch-stable.bin",stable)||stable!=after||!tick(io))return fail(Failure::Info);
   lastInfo=stable;phase=Phase::Ready;return true;
  }catch(...){return fail(Failure::State);}
 }
 template<class IO,class Sink>bool submit(IO&io,Sink&sink,unsigned program,const Binding*bindings,size_t count,RTXGeometry164::Size groups,RTXGeometry164::Size threads,uint64_t&completion){
  completion=0;if(!ready()||program>=decoded.count||!bindings||!count||count>8||completed_==UINT64_MAX)return false;
  try{
   // Snapshot and validate before any call or retirement of a usable context.
   std::array<Binding,8>copied{};std::copy_n(bindings,count,copied.begin());std::array<RTXSpans165::Binding,8>native{};
   auto local=std::make_unique<RTXSpans165::Owner<64,16>>(generation,0x2000000000ULL,0x2100000000ULL);
   for(unsigned i=0;i<4;++i){uint64_t h=0;if(!local->admitMapping(mappings[i],h)||h!=handles[i])return fail(Failure::Mapping);}
   for(unsigned i=0;i<count;++i){const auto&b=copied[i];if(b.slot>=4||b.index>=32||(b.offset&3)||!b.bytes)return false;native[i]={handles[b.slot],b.offset,b.bytes,b.index};}
   RTXSpans165::Plan plan{};if(!local->acquire(generation,decoded.programs[program],native.data(),count,groups,threads,plan))return false;
   Bytes wire(384),qmd,cb,command,ring;const uint64_t serial=completed_+1;
   Q::put64(wire.data(),Magic);Q::put32(wire.data()+8,183);Q::put32(wire.data()+12,384);Q::put64(wire.data()+16,generation);Q::put64(wire.data()+24,serial);Q::put32(wire.data()+32,program);Q::put32(wire.data()+36,uint32_t(count));
   const uint64_t dims[]={groups.x,groups.y,groups.z,threads.x,threads.y,threads.z};for(unsigned i=0;i<6;++i)Q::put64(wire.data()+48+i*8,dims[i]);
   for(unsigned i=0;i<count;++i){auto*p=wire.data()+128+i*32;Q::put64(p,native[i].handle);Q::put64(p+8,native[i].offset);Q::put64(p+16,native[i].bytes);Q::put32(p+24,native[i].index);}
   if(!expected(decoded.programs[program],plan,serial,qmd,cb,command,ring))return false;
   phase=Phase::Executing;start=io.nowNs();elapsed_=0;calls_=0;
   std::array<uint8_t,128>before{},after{},stable{};
   if(!info(io,sink,"dispatch-before.bin",before)||before!=lastInfo)return fail(Failure::Info);
   if(!save(sink,"submitted-request.bin",wire.data(),wire.size())||!call(io,105,nullptr,0,wire.data(),wire.size(),nullptr,0))return false;
   if(!info(io,sink,"info.bin",after)||!completedInfo(after,serial))return fail(Failure::Info);
   const char*names[]={"request.bin","qmd.bin","constants.bin","command.bin","ring.bin","before.bin","staged.bin","after.bin","observations.bin","library.bin","code.bin"};
   const size_t sizes[]={384,256,4096,56,8,B::DeviceBytes,B::DeviceBytes,B::DeviceBytes,size_t(R::u64(after.data()+120))*40,512,4096};
   std::array<Bytes,11>raw;for(unsigned p=0;p<11;++p)if(!capture(io,sink,p,names[p],raw[p],sizes[p]))return false;
   if(raw[0]!=wire||raw[1]!=qmd||raw[2]!=cb||raw[3]!=command||raw[4]!=ring||std::memcmp(raw[5].data(),canonical.data(),canonical.size())||std::memcmp(raw[9].data(),payload.data(),512)||std::memcmp(raw[10].data(),payload.data()+512,4096))return fail(Failure::Capture);
   Bytes staged(canonical.begin(),canonical.end());
   std::memcpy(staged.data()+B::Image,payload.data()+512,4096);std::memcpy(staged.data()+B::Constant,cb.data(),1024);std::memcpy(staged.data()+B::Qmd,qmd.data(),256);
   Q::put64(staged.data()+B::Fence,0);std::memcpy(staged.data()+4160,command.data(),56);std::memcpy(staged.data()+N::entryIndex(serial)*8,ring.data(),8);
   if(staged!=raw[6]||!journal(raw[8],serial,R::u64(after.data()+80)))return fail(Failure::Completion);
   Q::put32(staged.data()+0x88c,N::nextIndex(serial));Q::put32(staged.data()+0x888,N::nextIndex(serial));Q::put32(staged.data()+0x840,0x20001078);Q::put32(staged.data()+0x844,0x20001078);Q::put64(staged.data()+B::Fence,serial);Q::put64(staged.data()+B::Fence+16,serial);
   // QMD writeback is hardware-owned. All other bytes must match exactly.
   for(size_t i=0;i<staged.size();++i)if((i<B::Qmd||i>=B::Qmd+256)&&staged[i]!=raw[7][i])return fail(Failure::Capture);
   if(!info(io,sink,"dispatch-stable.bin",stable)||stable!=after||!tick(io))return fail(Failure::Info);
   std::copy(raw[7].begin(),raw[7].end(),canonical.begin());lastInfo=stable;ticket_=R::u64(stable.data()+48);completed_=serial;phase=Phase::Ready;completion=serial;return true;
  }catch(...){return fail(Failure::State);}
 }
};
}
