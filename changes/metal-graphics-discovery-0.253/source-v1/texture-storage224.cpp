#include "app/RTXTextureStorage224.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
namespace T=RTXTexture224;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("line "+std::to_string(__LINE__)+": " #x);}while(0)
static void write(const std::filesystem::path&p,const std::vector<std::uint8_t>&bytes){
 CHECK(!std::filesystem::exists(p));std::ofstream out(p,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));CHECK(bool(out));
}
static bool same(const T::Layout&a,const T::Layout&b){return a.format==b.format&&a.width==b.width&&a.height==b.height&&a.pixel==b.pixel&&a.offset==b.offset&&a.rowPitch==b.rowPitch&&a.storageBytes==b.storageBytes;}
static bool same(const T::Transfer&a,const T::Transfer&b){return a.imageOffset==b.imageOffset&&a.imageStride==b.imageStride&&a.rowBytes==b.rowBytes&&a.rows==b.rows&&a.hostStride==b.hostStride&&a.hostExtent==b.hostExtent&&a.compactBytes==b.compactBytes&&a.imageExtent==b.imageExtent;}
static void negative(){
 T::Layout sentinel;CHECK(T::allocate(T::Format::R32Float,7,3,sentinel));
 const auto maximum=std::numeric_limits<std::size_t>::max();
 const std::array<std::array<std::size_t,6>,13>bad={{{0,3,0,256,1024,3},{7,0,0,256,1024,3},{16385,1,0,65792,65792,3},{1,16385,0,256,T::MaxStorage,3},{7,3,1,256,1024,3},{7,3,0,255,1024,3},{65,3,0,256,1024,3},{7,3,256,256,1023,3},{7,3,0,256,T::MaxStorage+1,3},{7,3,0,maximum,maximum,3},{maximum,1,0,256,1024,3},{7,3,maximum&~std::size_t(255),256,maximum,3},{7,3,0,256,1024,99}}};
 for(const auto&v:bad){auto out=sentinel;CHECK(!T::make(T::Format(v[5]),v[0],v[1],v[2],v[3],v[4],out));CHECK(same(out,sentinel));}
 T::Transfer good;CHECK(T::plan(sentinel,{1,1,3,2},20,good));
 const std::array<T::Region,5>outside={{{8,0,0,1},{0,4,1,0},{6,1,2,1},{0,2,1,2},{maximum,0,1,1}}};
 for(auto region:outside){auto out=good;CHECK(!T::plan(sentinel,region,20,out));CHECK(same(out,good));}
 for(auto stride:{std::size_t(0),std::size_t(8),std::size_t(13),maximum&~std::size_t(3)}){auto out=good;CHECK(!T::plan(sentinel,{1,0,3,3},stride,out));CHECK(same(out,good));}
 auto corrupt=sentinel;corrupt.pixel=16;T::Transfer out;CHECK(!T::plan(corrupt,{0,0,1,1},16,out));
 corrupt=sentinel;++corrupt.storageBytes;CHECK(!T::plan(corrupt,{0,0,0,0},0,out));
 T::Transfer empty;CHECK(T::plan(sentinel,{7,3,0,0},0,empty));CHECK(T::copy(empty,true,nullptr,0,nullptr,0));
 T::Transfer single;CHECK(T::plan(sentinel,{0,1,7,1},0,single));CHECK(single.hostExtent==28&&single.hostStride==28);
 std::vector<std::uint8_t>image(768,0x44),host(128,0x55);const auto beforeImage=image,beforeHost=host;
 CHECK(!T::copy(good,true,image.data(),good.imageExtent-1,host.data(),host.size()));
 CHECK(!T::copy(good,true,image.data(),image.size(),host.data(),good.hostExtent-1));
 CHECK(!T::copy(good,true,nullptr,image.size(),host.data(),host.size()));
 CHECK(!T::copy(good,false,image.data(),image.size(),nullptr,host.size()));
 for(unsigned field=0;field<4;++field){auto changed=good;if(field==0)++changed.compactBytes;if(field==1)++changed.imageExtent;if(field==2)++changed.hostExtent;if(field==3)changed.rows=maximum;CHECK(!T::copy(changed,true,image.data(),image.size(),host.data(),host.size()));}
 CHECK(image==beforeImage&&host==beforeHost);
 CHECK(!T::pointerExtent(reinterpret_cast<const void*>(maximum-1),4));
 T::Layout largest;CHECK(T::allocate(T::Format::R32Float,16384,256,largest));CHECK(largest.storageBytes==T::MaxStorage);
 CHECK(!T::allocate(T::Format::R32Float,16384,257,largest));
}
int main(int argc,char**argv){try{
 if(argc!=3)return 2;negative();std::ifstream input(argv[1]);CHECK(bool(input));std::filesystem::path root(argv[2]);CHECK(std::filesystem::create_directories(root));
 std::size_t count=0;input>>count;CHECK(count>0&&count<=1024);
 for(std::size_t index=0;index<count;++index){
  std::array<std::uint64_t,13>a{};for(auto&v:a)input>>v;CHECK(bool(input));
  T::Layout layout;CHECK(T::make(T::Format(a[0]),a[1],a[2],a[3],a[4],a[5],layout));T::Transfer transfer;CHECK(T::plan(layout,{a[6],a[7],a[8],a[9]},a[10],transfer));
  const std::size_t backing=a[5],prefix=64,hostPrefix=31;std::vector<std::uint8_t>image(backing+128),host(transfer.hostExtent+hostPrefix+47);
  for(std::size_t i=0;i<image.size();++i)image[i]=std::uint8_t(i*17+index*29);
  for(std::size_t i=0;i<host.size();++i)host[i]=std::uint8_t(i*7+index*11);
  auto*hostPointer=host.data()+hostPrefix;std::size_t hostLength=transfer.hostExtent;
  const bool alias=a[12]!=std::numeric_limits<std::uint64_t>::max();
  if(alias){CHECK(a[12]<=backing&&transfer.hostExtent<=backing-a[12]);hostPointer=image.data()+prefix+a[12];hostLength=backing-a[12];}
  CHECK(T::copy(transfer,a[11]!=0,image.data()+prefix,backing,hostPointer,hostLength));
  auto d=root/std::to_string(index);CHECK(std::filesystem::create_directory(d));write(d/"image.bin",image);write(d/"host.bin",host);
  std::ofstream meta(d/"plan.txt");meta<<layout.pixel<<' '<<layout.storageBytes<<' '<<transfer.imageOffset<<' '<<transfer.imageStride<<' '<<transfer.rowBytes<<' '<<transfer.rows<<' '<<transfer.hostStride<<' '<<transfer.hostExtent<<' '<<transfer.compactBytes<<' '<<transfer.imageExtent<<'\n';CHECK(bool(meta));
 }
 std::uint64_t extra=0;CHECK(!(input>>extra));
 std::cout<<"{\"passed\":true,\"checks\":"<<checks<<",\"captures\":"<<count<<",\"gpu_executed\":false}\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
