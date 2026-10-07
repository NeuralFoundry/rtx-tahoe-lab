#pragma once
#include "../probe/kernel/root170/LegacyPageInventory169.hpp"
#include <array>
#include <cstring>
#include <vector>

// Reads only native selectors85..87 on the SAME already-owned connection.
// Does not send RPCs, allocate GPU memory, accept external captures, or start a
// shader. The owner cannot arm Metal commands until this collection verifies.
namespace RTXNativeRoot171 {
constexpr uint64_t Magic=0x525458524f4f5430ULL,BudgetNs=30000000000ULL;
constexpr unsigned Maps=3491,Tables=13,ChildBytes=40960,MaxCalls=128;
inline uint32_t u32(const uint8_t*p){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(p[i])<<(8*i);return v;}
inline uint64_t u64(const uint8_t*p){return uint64_t(u32(p))|(uint64_t(u32(p+4))<<32);}
inline void put32(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
inline void put64(uint8_t*p,uint64_t v){put32(p,uint32_t(v));put32(p+4,uint32_t(v>>32));}
inline bool cold(const uint8_t*p,size_t n,uint64_t gen){
 if(!p||n!=512||!gen||u64(p)!=Magic||u64(p+8)!=170||u64(p+16)!=gen)return false;
 for(unsigned i=24;i<512;++i)if(p[i])return false;return true;
}
struct Info {std::array<uint64_t,64>w{};};
inline bool decode(const uint8_t*p,size_t n,uint64_t gen,Info&out){
 if(!p||n!=512||!gen)return false;Info q;for(unsigned i=0;i<64;++i)q.w[i]=u64(p+i*8);const auto&w=q.w;
 const struct Pair{unsigned index;uint64_t value;}fixed[]={{0,Magic},{1,170},{2,gen},{3,1},{4,7},{5,1},{6,0},{7,Maps},{8,9},{9,ChildBytes},
  {10,4},{12,Tables},{13,Tables*4096},{14,Tables},{15,0},{16,0},{17,1},{18,1},{19,1},{20,1},{21,0},{22,0},{23,1},{24,1},{25,1},{26,1},{27,1},
  {31,33},{32,33},{38,54},{39,0},{43,1},{44,1},{45,1},{46,1},{47,0},{48,1},{49,0}};
 for(const auto&v:fixed)if(w[v.index]!=v.value)return false;
 if(w[11]<4096||w[11]%4096||w[11]>(1ULL<<40)-4096||w[28]<1||w[28]>16||w[29]<w[28]||w[29]>32||w[30]!=w[29]*4096||
  w[33]>=63||w[34]>=63||w[36]>=63||!w[37]||w[37]>=UINT32_MAX-16||w[35]!=w[37]+w[28]||w[33]!=(w[36]+w[29])%63||
  w[40]>=15000000000ULL||!w[41]||w[41]>150000||w[42]!=w[28]||!w[50]||!w[51]||!w[52]||w[53]!=2+w[28]||w[54]!=2+w[28])return false;
 for(unsigned i=55;i<64;++i)if(w[i])return false;out=q;return true;
}
inline bool request(const Info&i,const std::vector<uint8_t>&raw){
 if(raw.size()!=4096)return false;std::array<uint8_t,4096>w{};
 const struct Pair{unsigned off,value;}fields[]={{36,32},{40,1},{48,0x03000000},{52,0x43505256},{56,80},{60,54},{64,~0U},{68,~0U},
  {80,0xc1000000},{84,0xcf000011},{88,~0U},{104,4},{108,9},{112,0xcf000013},{120,1},{124,~0U}};
 for(auto f:fields)put32(w.data()+f.off,f.value);put64(w.data()+96,i.w[11]);uint32_t sum=0;
 for(unsigned j=0;j<128;j+=4)sum^=u32(w.data()+j);put32(w.data()+32,sum);return std::memcmp(w.data(),raw.data(),4096)==0;
}
inline bool journal(const Info&i,const std::vector<uint8_t>&raw){
 if(raw.size()!=i.w[30])return false;size_t offset=0;unsigned totalPages=0;
 for(unsigned row=0;row<i.w[28];++row){if(offset>raw.size()||raw.size()-offset<4096)return false;const auto*p=raw.data()+offset;
  for(unsigned j=0;j<32;++j)if(p[j])return false;
  const uint32_t pages=u32(p+40),length=u32(p+56),fn=u32(p+60);
  if(!pages||pages>16||size_t(pages)*4096>raw.size()-offset||u32(p+36)!=i.w[37]+row||u32(p+44)||u32(p+48)!=0x03000000||
   u32(p+52)!=0x43505256||length<32||length>=65536-48||(48+length+4095)/4096!=pages||u32(p+64)||u32(p+76))return false;
  const unsigned end=48+length,aligned=(end+7)&~7U;
  for(unsigned j=end;j<aligned;++j)if(p[j])return false;uint32_t sum=0;for(unsigned j=0;j<aligned;j+=4)sum^=u32(p+j);if(sum)return false;
  if(row+1==i.w[28]){if(fn!=54||length!=32||u32(p+68)||u32(p+72))return false;}
  else if(fn!=0x100c||length<40||u32(p+84)!=length-40)return false;
  totalPages+=pages;offset+=size_t(pages)*4096;
 }
 return offset==raw.size()&&totalPages==i.w[29];
}
struct Capture {
 enum class Failure:unsigned {None,Replay,Clock,Timeout,Call,Save,Summary,Request,Journal,Pages,Legacy,Rows,Tree,Changed,Exception};
 bool attempted=false,passed=false;Failure failure=Failure::None;unsigned calls=0,saves=0;uint64_t started=0,elapsed=0;
 Info info;std::array<uint8_t,512>before{},after{};
 std::vector<uint8_t>packet,records,pages,root,children,rows,image;
 bool fail(Failure f){if(failure==Failure::None)failure=f;passed=false;return false;}
 template<class IO>bool tick(IO&io){const auto now=io.nowNs();if(now<started||now-started<elapsed)return fail(Failure::Clock);elapsed=now-started;return elapsed<BudgetNs||fail(Failure::Timeout);}
 template<class IO>bool call(IO&io,unsigned selector,const uint64_t*scalars,unsigned count,uint8_t*out,size_t n){
  if(!tick(io)||calls>=MaxCalls)return fail(Failure::Timeout);++calls;
  if(!io.call(selector,scalars,count,nullptr,0,out,n))return fail(Failure::Call);return tick(io);
 }
 template<class Sink>bool save(Sink&sink,const char*name,const uint8_t*p,size_t n){++saves;return sink.save(name,p,n)||fail(Failure::Save);}
 template<class IO,class Sink>bool chunks(IO&io,Sink&sink,unsigned part,const char*name,std::vector<uint8_t>&out,size_t size){
  out.resize(size);
  for(size_t off=0;off<size;off+=4096){const size_t n=size-off<4096?size-off:4096;const uint64_t scalars[]={part,off,n};if(!call(io,86,scalars,3,out.data()+off,n))return false;}
  return save(sink,name,out.data(),out.size());
 }
 bool verifyMappings(uint64_t gen){
  if(pages.size()!=Tables*24||root.size()!=12288||children.size()!=ChildBytes||rows.size()!=Maps*40||image.size()!=Tables*4096)return fail(Failure::Rows);
  std::vector<uint64_t>addresses(Tables),scratch(Tables);
  for(unsigned n=0;n<Tables;++n){const auto*p=pages.data()+n*24;addresses[n]=u64(p+8);if(u64(p)!=n||u64(p+16)<4096)return fail(Failure::Pages);}
  if(addresses[0]!=info.w[11])return fail(Failure::Pages);
  std::vector<RTXPageTree167::Mapping>legacy(Maps);
  auto r=RTXLegacy169::decode(root.data(),root.size(),children.data(),children.size(),gen,legacy.data(),legacy.size());
  if(r.error!=RTXLegacy169::Error::None||r.pages!=Maps||r.leaves!=9)return fail(Failure::Legacy);
  for(unsigned n=0;n<Maps;++n){const auto*p=rows.data()+n*40;const auto&m=legacy[n];
   if(u64(p)!=m.va||u64(p+8)!=m.physical||u64(p+16)!=gen||u32(p+24)!=uint32_t(m.aperture)||u32(p+28)!=uint32_t(m.access)||u32(p+32)!=m.cached||u32(p+36))return fail(Failure::Rows);
  }
  std::vector<uint8_t>expected(Tables*4096);
  auto built=RTXPageTree167::build(legacy.data(),Maps,addresses.data(),Tables,scratch.data(),expected.data(),expected.size(),true);
  if(built.error!=RTXPageTree167::Error::None||built.tables!=Tables||expected!=image)return fail(Failure::Tree);
  return true;
 }
 template<class IO,class Sink>bool collectWork(IO&io,Sink&sink,uint64_t gen){
  if(!call(io,85,nullptr,0,before.data(),512)||!save(sink,"owned-root-info-before.bin",before.data(),512))return false;
  if(!decode(before.data(),512,gen,info))return fail(Failure::Summary);
  if(!chunks(io,sink,0,"owned-root-request.bin",packet,4096)||!request(info,packet))return fail(Failure::Request);
  if(!chunks(io,sink,1,"owned-root-records.bin",records,size_t(info.w[30]))||!journal(info,records))return fail(Failure::Journal);
  pages.resize(Tables*24);
  for(uint64_t i=0;i<Tables;++i)if(!call(io,87,&i,1,pages.data()+i*24,24))return false;
  if(!save(sink,"owned-root-pages.bin",pages.data(),pages.size()))return false;
  if(!chunks(io,sink,4,"owned-root-source-root.bin",root,12288)||!chunks(io,sink,5,"owned-root-source-children.bin",children,ChildBytes)||
   !chunks(io,sink,3,"owned-root-rows.bin",rows,Maps*40)||!chunks(io,sink,2,"owned-root-image.bin",image,Tables*4096))return false;
  if(!verifyMappings(gen))return false;
  if(!call(io,85,nullptr,0,after.data(),512)||!save(sink,"owned-root-info-after.bin",after.data(),512))return false;
  if(before!=after)return fail(Failure::Changed);if(!tick(io))return false;passed=true;return true;
 }
 template<class IO,class Sink>bool collect(IO&io,Sink&sink,uint64_t gen){
  if(attempted)return fail(Failure::Replay);attempted=true;
  try{started=io.nowNs();return collectWork(io,sink,gen);}catch(...){return fail(Failure::Exception);}
 }
};
}
