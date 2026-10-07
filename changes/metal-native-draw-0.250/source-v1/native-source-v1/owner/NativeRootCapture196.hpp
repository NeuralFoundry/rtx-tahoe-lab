#pragma once
#include "../probe/kernel/root170/LegacyPageInventory169.hpp"
#include "../probe/kernel/root194/StablePageTree194.hpp"
#include "../probe/kernel/changes/gsp-external-vas-0.27/ExternalVAS.hpp"
#include "driver/GSPDigest.hpp"
#include "../probe/kernel/gpu242/GraphicsLayout242.hpp"
#include <array>
#include <algorithm>
#include <cstring>
#include <vector>

// Read-only on the existing private native connection. Reconstruct all three
// stable-table stages from that connection's saved golden/HOST/final snapshots.
// No external file can supply admission and no selector here starts GPU work.
namespace RTXNativeRoot196 {
namespace P=RTXPageTree167;namespace S=RTXStableTree194;namespace E=ExternalVAS;
namespace L=RTXGraphicsLayout242;
constexpr uint64_t Magic=0x525458524f4f5430ULL,BudgetNs=30000000000ULL;
constexpr unsigned Tables=64,DataMaps=L::Pages,Buffers=L::Buffers,MaxCalls=320;
inline uint32_t u32(const uint8_t*p){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(p[i])<<(8*i);return v;}
inline uint64_t u64(const uint8_t*p){return uint64_t(u32(p))|(uint64_t(u32(p+4))<<32);}
inline void put32(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
inline void put64(uint8_t*p,uint64_t v){put32(p,uint32_t(v));put32(p+4,uint32_t(v>>32));}
inline bool cold(const uint8_t*p,size_t n,uint64_t gen){
 if(!p||n!=512||!gen||u64(p)!=Magic||u64(p+8)!=L::ABI||u64(p+16)!=gen)return false;
 for(unsigned i=24;i<512;++i)if(p[i])return false;return true;
}
struct Info {std::array<uint64_t,64>w{};};
inline bool decode(const uint8_t*p,size_t n,uint64_t gen,Info&out){
 if(!p||n!=512||gen<=Buffers||gen>=(1ULL<<63))return false;
 Info q;for(unsigned i=0;i<64;++i)q.w[i]=u64(p+i*8);const auto&w=q.w;
 const struct Pair{unsigned index;uint64_t value;}fixed[]={{0,Magic},{1,L::ABI},{2,gen},{3,1},{4,6},{5,1},{6,0},
  {10,4},{13,Tables*4096},{14,Tables},{15,0},{16,0},{17,1},{18,1},{19,1},{20,1},{24,19},
  {25,0},{26,0},{27,0},{28,0},{29,0},{30,0},{31,0},{32,1},{33,0},{34,1},{35,2},
  {36,1},{37,0},{38,1},{39,2},{40,0},{41,1},{49,54},{50,0},{51,19},{52,5},{53,5},{54,5},
  {56,Buffers},{57,DataMaps},{58,L::Bytes},{59,1},{60,gen},{61,L::Begin},{62,L::End},{63,0}};
 for(const auto&v:fixed)if(w[v.index]!=v.value)return false;
 if(w[11]<4096||w[11]%4096||w[11]>P::SysLimit-4096||w[12]<5||w[12]>Tables||
    !w[8]||w[8]>10||w[9]!=(w[8]+1)*4096||w[21]<4||w[22]<=w[21]||w[22]>RTXLegacy169::MaxPages-6||
    w[23]!=w[22]+6||w[55]!=w[23]||w[7]!=w[55]+DataMaps||w[42]<5||w[42]>16||
    w[43]<w[42]*4096||w[43]>131072||w[43]%4096||w[44]>=63||!w[45]||w[45]>UINT32_MAX-16||
    w[46]!=(w[44]+w[43]/4096)%63||w[47]>=63||w[48]!=w[45]+w[42])return false;
 out=q;return true;
}
inline bool request(const Info&i,const std::vector<uint8_t>&raw){
 if(raw.size()!=4096)return false;std::array<uint8_t,4096>expected{};
 return E::request(4,expected.data(),4096,{i.w[11],9})&&std::memcmp(raw.data(),expected.data(),4096)==0;
}
// Self-contained RM wire framing avoids importing kernel launch ownership into
// Objective-C++ translation units (Carbon also defines ResourceCount).
struct Record {unsigned function=0,sequence=0,payloadBytes=0;};
inline bool record(const uint8_t*p,unsigned bytes,unsigned sequence,Record&r){
 if(!p||bytes<4096||bytes>65536||bytes%4096)return false;
 for(unsigned n=0;n<32;++n)if(p[n])return false;
 const auto pages=u32(p+40),length=u32(p+56);
 if(!pages||pages>16||bytes!=pages*4096||u32(p+36)!=sequence||u32(p+44)||u32(p+48)!=0x03000000||
    u32(p+52)!=0x43505256||length<32||length>=65536-48||(48+length+4095)/4096!=pages||u32(p+64)||u32(p+76))return false;
 const auto end=48+length,aligned=(end+7)&~7U;uint32_t sum=0;
 for(unsigned n=end;n<aligned;++n)if(p[n])return false;
 for(unsigned n=0;n<aligned;n+=4)sum^=u32(p+n);if(sum)return false;
 r.function=u32(p+60);r.sequence=sequence;r.payloadBytes=length-32;return true;
}
inline bool notification(const uint8_t*p,const Record&r){return r.function==0x100c&&r.payloadBytes>=8&&u32(p+84)==r.payloadBytes-8;}
inline bool journal(const Info&i,const std::vector<uint8_t>&raw){
 if(raw.size()!=i.w[43])return false;size_t off=0;unsigned step=0;
 for(unsigned row=0;row<i.w[42];++row){
  if(step>=5||off>raw.size()||raw.size()-off<4096)return false;const auto*p=raw.data()+off;const auto pages=u32(p+40);
  if(!pages||pages>16||size_t(pages)*4096>raw.size()-off)return false;
  Record r;if(!record(p,pages*4096,unsigned(i.w[45])+row,r))return false;
  if(r.function==E::function(step)){E::Reply reply;if(!E::reply(step,r.sequence,p,pages*4096,reply))return false;++step;}
  else if(r.function!=0x100c||!notification(p,r))return false;
  off+=size_t(pages)*4096;
 }
 return off==raw.size()&&step==5;
}
struct Capture {
 enum class Failure:unsigned {None,Replay,Clock,Timeout,Call,Save,Summary,Request,Journal,Pages,Legacy,Rows,Tree,Handles,Digest,Changed,Exception,Stage,External};
 bool attempted=false,passed=false;Failure failure=Failure::None;unsigned calls=0,saves=0;uint64_t started=0,elapsed=0;
 Info info;std::array<uint8_t,512>before{},after{},goldenInfo{},contextInfo{},external{},goldenRM{};
 std::vector<uint8_t>packet,records,pages,root,children,rows,image,handles,digests;
 std::vector<uint8_t>goldenRoot,goldenChildren,contextRoot,contextChildren,requests;
 bool fail(Failure f){if(failure==Failure::None)failure=f;passed=false;return false;}
 template<class IO>bool tick(IO&io){const auto now=io.nowNs();if(!started||now<started||now-started<elapsed)return fail(Failure::Clock);elapsed=now-started;return elapsed<BudgetNs||fail(Failure::Timeout);}
 template<class IO>bool call(IO&io,unsigned selector,const uint64_t*scalars,unsigned count,uint8_t*out,size_t n){
  if(!tick(io)||calls>=MaxCalls)return fail(Failure::Timeout);++calls;
  if(!io.call(selector,scalars,count,nullptr,0,out,n))return fail(Failure::Call);return tick(io);
 }
 template<class Sink>bool save(Sink&sink,const char*name,const uint8_t*p,size_t n){++saves;return sink.save(name,p,n)||fail(Failure::Save);}
 template<class IO,class Sink>bool chunks(IO&io,Sink&sink,unsigned selector,unsigned part,const char*name,std::vector<uint8_t>&out,size_t size){
  if(!size||size>Tables*4096)return fail(Failure::Stage);out.resize(size);
  for(size_t off=0;off<size;off+=4096){const size_t n=std::min<size_t>(4096,size-off);const uint64_t scalars[]={part,off,n};
   if(!call(io,selector,scalars+(selector==63?1:0),selector==63?2:3,out.data()+off,n))return false;}
  return save(sink,name,out.data(),out.size());
 }
 template<class IO,class Sink>bool summary(IO&io,Sink&sink,unsigned sel,const char*name,std::array<uint8_t,512>&out){
  return call(io,sel,nullptr,0,out.data(),out.size())&&save(sink,name,out.data(),out.size());
 }
 bool snapshotInfo(const std::array<uint8_t,512>&raw,uint64_t magic,unsigned&bytes){
  if(u64(raw.data())!=magic||u64(raw.data()+8)!=1||u64(raw.data()+16)!=info.w[2]||u64(raw.data()+24)!=1||
     u64(raw.data()+32)!=1||u64(raw.data()+40)||u64(raw.data()+48)!=12288)return false;
  const auto n=u64(raw.data()+56);if(n<8192||n>RTXLegacy169::MaxChildBytes||n%4096)return false;bytes=unsigned(n);return true;
 }
 bool verifyExternal(){
  if(!request(info,packet)||!journal(info,records))return fail(Failure::Journal);
  const auto*w=external.data();auto v=[&](unsigned n){return u64(w+n*8);};
  const struct Pair{unsigned a,b;}cross[]={{11,42},{13,43},{14,24},{15,51},{16,46},{17,47},{18,48},{28,44},{29,45},{32,49},{33,50},{59,11}};
  for(const auto&p:cross)if(v(p.a)!=info.w[p.b])return fail(Failure::External);
  const struct Fixed{unsigned a;uint64_t n;}fixed[]={{0,0x5254584556413237ULL},{1,2},{2,info.w[2]},{3,1},{4,1},{5,1},{6,0},{7,4},
   {8,5},{9,5},{10,5},{12,info.w[43]/4096},{30,info.w[42]},{31,1},{34,0},{37,E::Client},{38,E::Device},{39,E::Subdevice},{40,E::Vaspace},
   {41,4096},{42,16},{43,32},{50,2},{51,1},{52,5},{53,1},{60,4},{61,9},{62,20480}};
  for(const auto&p:fixed)if(v(p.a)!=p.n)return fail(Failure::External);
  if(v(25)>=15000000000ULL||v(19)>150000||requests.size()!=20480)return fail(Failure::External);
  std::array<uint8_t,4096>expected{};
  for(unsigned step=0;step<5;++step)if(!E::request(step,expected.data(),4096,{info.w[11],9})||
     std::memcmp(expected.data(),requests.data()+step*4096,4096))return fail(Failure::Request);
  if(std::memcmp(packet.data(),requests.data()+4*4096,4096))return fail(Failure::Request);
  const auto*g=goldenRM.data();auto q=[&](unsigned n){return u64(g+n*8);};
  if(q(0)!=0x52545843484e3233ULL||q(1)!=1||q(2)!=info.w[2]||q(3)!=1||q(4)!=1||q(5)!=1||q(6)||
     q(8)!=5||q(9)!=5||q(14)!=14||q(15)!=14||q(16)!=info.w[44]||q(18)!=info.w[45])return fail(Failure::External);
  // Allocation response describes the VAS actually accepted by RM.
  size_t off=0;unsigned step=0;E::Reply vas;
  for(unsigned n=0;n<info.w[42];++n){const auto*p=records.data()+off;const auto bytes=u32(p+40)*4096;
   if(u32(p+60)==E::function(step)){E::Reply parsed;if(!E::reply(step,unsigned(info.w[45])+n,p,bytes,parsed))return fail(Failure::Journal);if(step==3)vas=parsed;++step;}off+=bytes;}
  return (vas.accepted&&v(54)==vas.base&&v(55)==vas.size&&v(56)==vas.internalLo&&v(57)==vas.internalHi&&v(58)==vas.bigPage)||fail(Failure::External);
 }
 bool verifyMappings(uint64_t gen){
  const unsigned legacy=unsigned(info.w[55]),maps=unsigned(info.w[7]);
  if(pages.size()!=Tables*24||root.size()!=12288||children.size()!=info.w[9]||rows.size()!=maps*40||image.size()!=Tables*4096)return fail(Failure::Rows);
  if(handles.size()!=Buffers*64||digests.size()!=64)return fail(Failure::Handles);
  std::vector<uint64_t>addresses(Tables),dataAddresses;
  for(unsigned n=0;n<Tables;++n){const auto*p=pages.data()+n*24;addresses[n]=u64(p+8);if(u64(p)!=n||u64(p+16)<4096)return fail(Failure::Pages);}
  if(addresses[0]!=info.w[11])return fail(Failure::Pages);
  std::vector<P::Mapping>expected(maps);
  auto result=RTXLegacy169::decode(root.data(),root.size(),children.data(),children.size(),gen,expected.data(),legacy);
  if(result.error!=RTXLegacy169::Error::None||result.pages!=legacy||result.leaves!=info.w[8])return fail(Failure::Legacy);
  uint64_t va=L::Begin;unsigned ordinal=legacy;
  for(unsigned b=0;b<Buffers;++b){const auto&d=L::Description[b];const uint64_t mapped=L::mapped(b);const auto*p=handles.data()+b*64;
   const uint64_t fields[]={gen,(1ULL<<32)|(b+1),b+1,b+1,va,d.bytes,mapped,d.access};
   for(unsigned n=0;n<8;++n)if(u64(p+n*8)!=fields[n])return fail(Failure::Handles);
   for(uint64_t n=0;n<mapped/4096;++n){if(ordinal>=maps)return fail(Failure::Rows);const auto pa=u64(rows.data()+ordinal*40+8);
    if(pa<4096||pa%4096||pa>P::SysLimit-4096)return fail(Failure::Rows);
    expected[ordinal++]={va+n*4096,pa,uint64_t(b)+1,P::Aperture::System,(d.access&2)?P::Access::ReadWrite:P::Access::Read,0,0};dataAddresses.push_back(pa);}
   va+=mapped+4096;
  }
  if(ordinal!=maps||dataAddresses.size()!=DataMaps)return fail(Failure::Rows);
  std::sort(dataAddresses.begin(),dataAddresses.end());if(std::adjacent_find(dataAddresses.begin(),dataAddresses.end())!=dataAddresses.end())return fail(Failure::Pages);
  for(const auto pa:addresses)if(std::binary_search(dataAddresses.begin(),dataAddresses.end(),pa))return fail(Failure::Pages);
  for(unsigned n=0;n<maps;++n){const auto*p=rows.data()+n*40;const auto&m=expected[n];
   if(u64(p)!=m.va||u64(p+8)!=m.physical||u64(p+16)!=m.owner||u32(p+24)!=uint32_t(m.aperture)||
      u32(p+28)!=uint32_t(m.access)||u32(p+32)!=m.cached||u32(p+36))return fail(Failure::Rows);}
  if(goldenRoot!=root||contextRoot!=root)return fail(Failure::Stage);
  // Fixed preparation adds precisely these three empty slots in PT0.
  auto fixed=goldenChildren;if(fixed.size()<8192||u64(fixed.data())!=0x20||u64(fixed.data()+8)!=0x100602)return fail(Failure::Stage);
  const uint64_t physical[]={0x3402000,0x3403000,0x3400000};
  for(unsigned i=0;i<3;++i){auto*p=fixed.data()+4096+(i+1)*8;if(u64(p))return fail(Failure::Stage);put64(p,(6ULL<<56)|(physical[i]>>4)|1);}
  std::vector<P::Mapping>stages[3];std::vector<S::Node>nodes[3];std::vector<uint8_t>images[3];S::Result planned[3];
  const std::vector<uint8_t>*child[]={&fixed,&contextChildren,&children};
  for(unsigned stage=0;stage<3;++stage){const auto count=unsigned(info.w[21+stage]);auto&next=stages[stage];next.resize(count+DataMaps);
   auto parsed=RTXLegacy169::decode(root.data(),root.size(),child[stage]->data(),child[stage]->size(),gen,next.data(),count);
   if(parsed.error!=RTXLegacy169::Error::None||parsed.pages!=count)return fail(Failure::Stage);
   std::copy(expected.begin()+legacy,expected.end(),next.begin()+count);nodes[stage].resize(Tables);images[stage].resize(Tables*4096);
   S::View prior;if(stage)prior={stages[stage-1].data(),uint32_t(stages[stage-1].size()),addresses.data(),Tables,nodes[stage-1].data(),planned[stage-1].used,images[stage-1].data()};
   planned[stage]=S::plan(next.data(),uint32_t(next.size()),addresses.data(),Tables,stage?&prior:nullptr,nodes[stage].data(),images[stage].data());
   if(planned[stage].error!=S::Error::None||planned[stage].root!=info.w[11])return fail(Failure::Tree);
  }
  if(planned[2].used!=info.w[12]||images[2]!=image)return fail(Failure::Tree);
  for(unsigned n=0;n<2;++n){uint8_t digest[32];const auto&bytes=images[n?2:0];GSPDigest::SHA256 hash;hash.update(bytes.data(),bytes.size());hash.finish(digest);
   if(std::memcmp(digest,digests.data()+n*32,32))return fail(Failure::Digest);}
  return true;
 }
 template<class IO,class Sink>bool collectWork(IO&io,Sink&sink,uint64_t gen){
  if(!summary(io,sink,85,"owned-root-info-before.bin",before))return false;
  if(!decode(before.data(),512,gen,info))return fail(Failure::Summary);
  if(!chunks(io,sink,86,0,"owned-root-request.bin",packet,4096)||!request(info,packet))return fail(Failure::Request);
  if(!chunks(io,sink,86,1,"owned-root-records.bin",records,size_t(info.w[43]))||!journal(info,records))return fail(Failure::Journal);
  if(!summary(io,sink,60,"owned-root-external.bin",external)||!summary(io,sink,37,"owned-root-golden-rm.bin",goldenRM)||
     !chunks(io,sink,63,0,"owned-root-initial-requests.bin",requests,20480)||!verifyExternal())return false;
  unsigned goldenBytes=0,contextBytes=0;
  if(!summary(io,sink,43,"owned-root-golden-info.bin",goldenInfo)||!summary(io,sink,52,"owned-root-context-info.bin",contextInfo))return false;
  if(!snapshotInfo(goldenInfo,0x525458534e503233ULL,goldenBytes)||!snapshotInfo(contextInfo,0x5254584558533234ULL,contextBytes)||contextBytes!=info.w[9])return fail(Failure::Stage);
  if(!chunks(io,sink,42,0,"owned-root-golden-root.bin",goldenRoot,12288)||!chunks(io,sink,42,1,"owned-root-golden-children.bin",goldenChildren,goldenBytes)||
     !chunks(io,sink,51,0,"owned-root-context-root.bin",contextRoot,12288)||!chunks(io,sink,51,1,"owned-root-context-children.bin",contextChildren,contextBytes))return false;
  pages.resize(Tables*24);for(uint64_t n=0;n<Tables;++n)if(!call(io,87,&n,1,pages.data()+n*24,24))return false;
  if(!save(sink,"owned-root-pages.bin",pages.data(),pages.size()))return false;
  if(!chunks(io,sink,86,4,"owned-root-source-root.bin",root,12288)||!chunks(io,sink,86,5,"owned-root-source-children.bin",children,size_t(info.w[9]))||
     !chunks(io,sink,86,3,"owned-root-rows.bin",rows,size_t(info.w[7])*40)||!chunks(io,sink,86,2,"owned-root-image.bin",image,Tables*4096)||
     !chunks(io,sink,86,6,"owned-root-handles.bin",handles,Buffers*64)||!chunks(io,sink,86,7,"owned-root-digests.bin",digests,64)||!verifyMappings(gen))return false;
  if(!summary(io,sink,85,"owned-root-info-after.bin",after))return false;
  if(before!=after)return fail(Failure::Changed);if(!tick(io))return false;passed=true;return true;
 }
 template<class IO,class Sink>bool collect(IO&io,Sink&sink,uint64_t gen){
  if(attempted)return fail(Failure::Replay);attempted=true;
  try{started=io.nowNs();return collectWork(io,sink,gen);}catch(...){return fail(Failure::Exception);}
 }
};
}
