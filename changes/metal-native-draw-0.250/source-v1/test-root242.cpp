#include "native-source-v1/owner/NativeDataSession182.hpp"
#include "native-source-v1/probe/kernel/gpu242/GraphicsABI242.hpp"
#include "native-source-v1/probe/kernel/SelectorRouting171.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;namespace R=RTXNativeRoot196;namespace L=RTXGraphicsLayout242;namespace T=RTXPageTree167;namespace ST=RTXStableTree194;
using Bytes=std::vector<uint8_t>;unsigned checks=0,rejections=0;
void checked(bool v,unsigned line){++checks;if(!v)throw std::runtime_error("root242 line "+std::to_string(line));}
#define check(v) checked(bool(v),__LINE__)
Bytes read(const fs::path&p){std::ifstream f(p,std::ios::binary);check(f);return Bytes(std::istreambuf_iterator<char>(f),{});}
void write(const fs::path&p,const void*v,size_t n){std::ofstream f(p,std::ios::binary);f.write(static_cast<const char*>(v),std::streamsize(n));check(f);}
template<class A>void load(A&out,const fs::path&p){auto b=read(p);check(b.size()==out.size());std::copy(b.begin(),b.end(),out.begin());}
void regenerate(R::Capture&c){
 const auto gen=c.info.w[2];std::vector<uint64_t>pages(64);for(unsigned n=0;n<64;++n)pages[n]=R::u64(c.pages.data()+n*24+8);
 auto fixed=c.goldenChildren;const uint64_t physical[]={0x3402000,0x3403000,0x3400000};for(unsigned i=0;i<3;++i)R::put64(fixed.data()+4096+(i+1)*8,(6ULL<<56)|(physical[i]>>4)|1);
 const Bytes*child[]={&fixed,&c.contextChildren,&c.children};std::vector<T::Mapping>rows[3];std::vector<ST::Node>nodes[3];Bytes images[3];ST::Result result[3];
 for(unsigned stage=0;stage<3;++stage){const auto count=unsigned(c.info.w[21+stage]);rows[stage].resize(count+L::Pages);
  auto parsed=RTXLegacy169::decode(c.root.data(),c.root.size(),child[stage]->data(),child[stage]->size(),gen,rows[stage].data(),count);check(parsed.error==RTXLegacy169::Error::None&&parsed.pages==count);
  for(unsigned n=0;n<L::Pages;++n){const auto*p=c.rows.data()+(c.info.w[55]+n)*40;rows[stage][count+n]={R::u64(p),R::u64(p+8),R::u64(p+16),T::Aperture(R::u32(p+24)),T::Access(R::u32(p+28)),R::u32(p+32),R::u32(p+36)};}
  nodes[stage].resize(64);images[stage].resize(64*4096);ST::View prior;
  if(stage)prior={rows[stage-1].data(),uint32_t(rows[stage-1].size()),pages.data(),64,nodes[stage-1].data(),result[stage-1].used,images[stage-1].data()};
  result[stage]=ST::plan(rows[stage].data(),uint32_t(rows[stage].size()),pages.data(),64,stage?&prior:nullptr,nodes[stage].data(),images[stage].data());check(result[stage].error==ST::Error::None);
 }
 c.info.w[12]=result[2].used;c.image=images[2];c.digests.resize(64);
 for(unsigned i=0;i<2;++i){GSPDigest::SHA256 hash;auto&b=images[i?2:0];hash.update(b.data(),unsigned(b.size()));hash.finish(c.digests.data()+i*32);}
 for(unsigned i=0;i<64;++i)R::put64(c.before.data()+i*8,c.info.w[i]);c.after=c.before;
}
std::unique_ptr<R::Capture> fixture(const fs::path&dir){
 auto c=std::make_unique<R::Capture>();load(c->before,dir/"owned-root-info-before.bin");for(unsigned n=0;n<64;++n)c->info.w[n]=R::u64(c->before.data()+n*8);
 c->root=read(dir/"owned-root-source-root.bin");c->children=read(dir/"owned-root-source-children.bin");c->goldenRoot=read(dir/"owned-root-golden-root.bin");c->goldenChildren=read(dir/"owned-root-golden-children.bin");c->contextRoot=read(dir/"owned-root-context-root.bin");c->contextChildren=read(dir/"owned-root-context-children.bin");c->pages=read(dir/"owned-root-pages.bin");c->rows=read(dir/"owned-root-rows.bin");c->handles=read(dir/"owned-root-handles.bin");
 check(c->info.w[56]==4&&c->info.w[57]==1301&&c->handles.size()==256);
 c->info.w[1]=L::ABI;c->info.w[7]=c->info.w[55]+L::Pages;c->info.w[56]=L::Buffers;c->info.w[57]=L::Pages;c->info.w[58]=L::Bytes;
 for(unsigned slot=4;slot<L::Buffers;++slot){const auto&d=L::Description[slot];const auto va=L::address(slot),mapped=L::mapped(slot);const size_t at=c->handles.size();c->handles.resize(at+64);
  const uint64_t fields[]={c->info.w[2],(1ULL<<32)|(slot+1),slot+1,slot+1,va,d.bytes,mapped,d.access};for(unsigned n=0;n<8;++n)R::put64(c->handles.data()+at+n*8,fields[n]);
  for(uint64_t n=0;n<mapped/4096;++n){const auto offset=c->rows.size();c->rows.resize(offset+40);auto*p=c->rows.data()+offset;R::put64(p,va+n*4096);R::put64(p+8,0x1200000000ULL+uint64_t(slot)*0x100000+n*8192);R::put64(p+16,slot+1);R::put32(p+24,2);R::put32(p+28,(d.access&2)?2:1);R::put32(p+32,0);R::put32(p+36,0);}
 }
 regenerate(*c);check(c->verifyMappings(c->info.w[2]));R::Info decoded;check(R::decode(c->before.data(),512,c->info.w[2],decoded)&&decoded.w==c->info.w);c->passed=true;return c;
}
struct IO {
 R::Capture&root;std::array<uint64_t,8>stats{};unsigned calls=0;uint64_t clock=1000;Bytes data[L::Buffers];
 explicit IO(R::Capture&r):root(r){stats={RTXNativeData182::Magic,181,r.info.w[2],1,0,0,0,0};for(unsigned n=0;n<L::Buffers;++n)data[n].resize(size_t(L::Description[n].bytes));}
 uint64_t nowNs(){return ++clock;}
 bool call(unsigned selector,const uint64_t*s,unsigned count,const uint8_t*in,size_t bytes,uint8_t*out,size_t n){++calls;
  if(selector==97){check(!count&&!in&&!bytes&&n==64);for(unsigned i=0;i<8;++i)R::put64(out+i*8,stats[i]);return true;}
  check(s&&s[0]==root.info.w[2]&&(s[1]>>32)==1);const unsigned slot=uint32_t(s[1])-1;check(slot<L::Buffers);++stats[4];
  if(selector==100){check(count==3&&!in&&!bytes&&n==32&&s[2]<L::mapped(slot)/4096);unsigned ordinal=unsigned(root.info.w[55]);for(unsigned i=0;i<slot;++i)ordinal+=unsigned(L::mapped(i)/4096);const auto*p=root.rows.data()+(ordinal+s[2])*40;
   const uint64_t words[]={s[2],R::u64(p+8),R::u64(p+8),4096};for(unsigned i=0;i<4;++i)R::put64(out+i*8,words[i]);return true;}
  check(count==4&&s[2]<=data[slot].size()&&s[3]<=data[slot].size()-s[2]);
  if(selector==98){check(in&&bytes==s[3]&&!out&&!n);std::memcpy(data[slot].data()+s[2],in,bytes);++stats[5];return true;}
  check(selector==99&&!in&&!bytes&&n==s[3]);std::memcpy(out,data[slot].data()+s[2],n);++stats[6];return true;
 }
};
struct Sink{std::map<std::string,Bytes>files;bool save(const char*n,const uint8_t*p,size_t bytes){files[n]=Bytes(p,p+bytes);return true;}};
