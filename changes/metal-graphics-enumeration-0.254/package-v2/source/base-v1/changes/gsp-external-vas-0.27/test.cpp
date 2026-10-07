#include "ExternalVAS.hpp"
#include <cstdio>
#include <cstring>
#include <new>
#include <initializer_list>
namespace E=ExternalVAS;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"check failed line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static void fix(unsigned char *p){E::put32(p+32,0);E::put32(p+32,E::checksum(p,48+E::get32(p+56)));}
int main(int argc,char **argv){
 if(argc!=2)return 2;char name[1024];std::snprintf(name,sizeof(name),"%s/requests.bin",argv[1]);FILE *requests=std::fopen(name,"wb");if(!requests)return 3;
 std::snprintf(name,sizeof(name),"%s/replies.bin",argv[1]);FILE *replies=std::fopen(name,"wb");if(!replies)return 3;
 unsigned char raw[4096],good[4096],scratch[4096];E::Journal journal;journal.nextSequence=21;
 for(unsigned step=0;step<E::Steps;++step){
  CHECK(E::request(step,raw,sizeof(raw)));CHECK(std::fwrite(raw,1,4096,requests)==4096);
  CHECK(E::get32(raw+36)==14+step);CHECK(E::get32(raw+60)==(step<4?103U:54U));CHECK(!E::checksum(raw,48+E::get32(raw+56)));
  E::put32(raw+36,21+step);E::put32(raw+64,0);E::put32(raw+68,0);if(step==0)E::put32(raw+112,E::Client);if(step==4)E::put32(raw+56,32);fix(raw);
  std::memcpy(good,raw,4096);E::Reply r;CHECK(E::reply(step,21+step,raw,4096,r)&&r.accepted);
  CHECK(E::consume(journal,raw,4096));CHECK(journal.next==step+1);CHECK(std::fwrite(raw,1,4096,replies)==4096);
  // All truncations are rejected before parsing an incomplete frame.
  for(unsigned size=0;size<4096;++size){CHECK(!E::reply(step,21+step,raw,size,r));CHECK(!r.accepted);}
  CHECK(!E::reply(step,22+step,raw,4096,r));CHECK(!E::reply(step,~0U,raw,4096,r));
  for(unsigned off: {0U,16U,36U,40U,44U,48U,52U,56U,60U,64U,68U,72U,76U}){
   std::memcpy(raw,good,4096);raw[off]^=1;
   // Length mutation deliberately keeps original checksum: parser must not use an unchecked length.
   if(off!=56)fix(raw);CHECK(!E::reply(step,21+step,raw,4096,r));
  }
  const unsigned n=step<4?E::paramBytes(step)+32:0;
  for(unsigned off=0;off<n;++off){
   // VAS output extents are validated below; all other payload bytes are exact.
   if(step==3&&off>=32&&((off>=40&&off<68)||(off>=72&&off<80)))continue;
   std::memcpy(raw,good,4096);raw[80+off]^=1;fix(raw);CHECK(!E::reply(step,21+step,raw,4096,r));
  }
  std::memcpy(raw,good,4096);raw[4095]^=1;CHECK(E::reply(step,21+step,raw,4096,r)); // stale slot tail is not payload
  if(step==4){
   for(unsigned off=80;off<4096;++off){std::memcpy(raw,good,4096);raw[off]^=1;CHECK(E::reply(step,25,raw,4096,r));}
   for(unsigned length:{33U,36U,40U,80U}){std::memcpy(raw,good,4096);E::put32(raw+56,length);fix(raw);CHECK(!E::reply(step,25,raw,4096,r));}
  }
  if(step==3){
   constexpr uint64_t badSizes[]={0,E::RequiredHi-4096,(1ULL<<49)+4096,E::RequestedSize+1};
   for(auto v:badSizes){std::memcpy(raw,good,4096);E::put64(raw+120,v);fix(raw);CHECK(!E::reply(step,24,raw,4096,r));}
   constexpr uint64_t badBases[]={0,E::RequiredLo+4096,4097};
   for(auto v:badBases){std::memcpy(raw,good,4096);E::put64(raw+152,v);fix(raw);CHECK(!E::reply(step,24,raw,4096,r));}
   for(unsigned v:{0U,65536U,131072U}){std::memcpy(raw,good,4096);E::put32(raw+144,v);fix(raw);CHECK(E::reply(step,24,raw,4096,r));}
   std::memcpy(raw,good,4096);E::put64(raw+128,E::RequiredLo);E::put64(raw+136,E::RequiredLo+4095);fix(raw);CHECK(!E::reply(step,24,raw,4096,r));
  }
 }
 CHECK(journal.next==5&&journal.vas.accepted);CHECK(!E::consume(journal,good,4096));
 E::Journal wrong;wrong.nextSequence=21;CHECK(!E::consume(wrong,good,4096));CHECK(wrong.failed);CHECK(!E::consume(wrong,good,4096));
 std::memset(scratch,0x5a,sizeof(scratch));std::memcpy(good,scratch,4096);
 CHECK(!E::request(5,scratch,4096));CHECK(!std::memcmp(good,scratch,4096));CHECK(!E::request(0,scratch,4095));CHECK(!std::memcmp(good,scratch,4096));
 CHECK(!E::request(0,nullptr,4096));E::Reply result;CHECK(!E::reply(0,21,nullptr,4096,result));
 alignas(E::Reply) unsigned char alias[4096]{};auto *overlap=new(alias)E::Reply{};std::memcpy(good,alias,4096);
 CHECK(!E::reply(0,21,alias,sizeof(alias),*overlap));CHECK(!std::memcmp(good,alias,4096));
 CHECK(std::fclose(requests)==0);CHECK(std::fclose(replies)==0);
 std::printf("{\"passed\":true,\"checks\":%u,\"requests\":5,\"hardware_accessed\":false}\n",checks);return 0;
}
