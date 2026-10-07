#include "app/RTXRenderClear231.hpp"
#include "app/OwnedBatch187.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
static unsigned checks=0;
static void check(bool v){++checks;if(!v)throw std::runtime_error("check "+std::to_string(checks));}
static void write(const std::filesystem::path&p,const void*b,size_t n){std::ofstream f(p,std::ios::binary);f.write(static_cast<const char*>(b),std::streamsize(n));check(bool(f));}
int main(int argc,char**argv){try{
 check(argc==3);namespace R=RTXRenderClear231;namespace T=RTXTexture224;namespace B=RTXBatch187;
 std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t>image((std::istreambuf_iterator<char>(f)),{});RTXCatalog187::Catalog catalog;
 check(RTXCatalog187::decode(image.data(),image.size(),catalog));check(catalog.abi==2&&catalog.containerABI==3&&catalog.library.count==1);
 const auto&p=catalog.library.programs[0];check(p.parameters==3&&p.readMask==5&&p.writeMask==2&&p.localX==64&&p.localY==p.localZ==1);
 check(catalog.resources[0].kind==1&&catalog.resources[1].kind==2&&catalog.resources[2].kind==3&&catalog.resources[1].format==RTXTexture225::FloatFormats226);
 const std::array<double,4>rgba{-2.0,0.1,0.5,2.0};unsigned cases=0,rejections=0;
 const std::filesystem::path output(argv[2]);check(std::filesystem::create_directory(output));
 for(unsigned format:{3u,4u,7u,10u})for(unsigned width:{1u,17u,64u,65u})for(unsigned offset:{0u,256u}) {
  const unsigned height=3,pixel=format==10?16:4,pitch=(width*pixel+255)&~255u,backing=offset+pitch*height+17;
  T::Layout layout;check(T::make(static_cast<T::Format>(format),width,height,offset,pitch,backing,layout));
  RTXTexture225::Descriptor descriptor;check(RTXTexture225::descriptor(layout,backing,descriptor,RTXTexture225::FloatFormats226));
  R::Plan plan;check(R::make(descriptor.data(),backing,offset,rgba,plan));check(plan.width==width&&plan.height==height&&plan.groupsX==(width+63)/64&&plan.groupsY==height);
  const auto dir=output/("case-"+std::to_string(++cases));check(std::filesystem::create_directory(dir));
  const uint32_t metadata[]={format,width,height,pitch,offset,backing,plan.groupsX,plan.groupsY};write(dir/"metadata.bin",metadata,sizeof(metadata));
  write(dir/"color.bin",plan.color.data(),plan.color.size());write(dir/"descriptors.bin",plan.descriptors.data(),plan.descriptors.size());
  B::Input input[]={{1,256,0,0},{2,backing,offset,1},{3,256,0,2}};B::Plan native;
  check(B::plan(p,0,input,3,{plan.groupsX,plan.groupsY,1},{64,1,1},231,cases,native));check(native.resources==3&&native.count==3);
  std::array<B::Bytes,4>snapshots;snapshots[0].assign(plan.color.begin(),plan.color.end());snapshots[1].resize(backing);snapshots[2].assign(plan.descriptors.begin(),plan.descriptors.end());
  for(size_t i=0;i<backing;++i)snapshots[1][i]=uint8_t(i*17+13);
  B::Bytes request;check(B::assemble(native,snapshots,request));B::Plan decoded;check(B::decode(request.data(),request.size(),catalog.library,231,cases,decoded));
  write(dir/"request.bin",request.data(),request.size());B::Bytes result(request.begin()+512,request.end());
  check(B::readback(native,request.data(),request.size(),result.data(),result.size()));
  for(unsigned resource:{0u,2u}){auto changed=result;changed[size_t(native.resource[resource].payloadOffset)+31]^=1;check(!B::readback(native,request.data(),request.size(),changed.data(),changed.size()));++rejections;}
  auto missing=result;missing.pop_back();check(!B::readback(native,request.data(),request.size(),missing.data(),missing.size()));++rejections;
  // Every malformed descriptor must preserve the caller's output plan.
  for(unsigned word=0;word<8;++word){auto bad=descriptor;RTXTexture225::put(bad.data()+word*4,0xffffffffu);auto unchanged=plan;
   check(!R::make(bad.data(),backing,offset,rgba,unchanged));check(unchanged.color==plan.color&&unchanged.descriptors==plan.descriptors&&unchanged.width==plan.width);++rejections;}
 }
 T::Layout layout;check(T::make(T::Format::RGBA32Float,1,1,0,256,256,layout));RTXTexture225::Descriptor descriptor;check(RTXTexture225::descriptor(layout,256,descriptor,RTXTexture225::FloatFormats226));
 for(double bad:{std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN(),double(std::numeric_limits<float>::max())*2}) {
  auto color=rgba;color[2]=bad;R::Plan unchanged;unchanged.width=99;check(!R::make(descriptor.data(),256,0,color,unchanged));check(unchanged.width==99);++rejections;
 }
 R::Plan plan;check(!R::make(nullptr,256,0,rgba,plan));check(!R::make(descriptor.data(),0,0,rgba,plan));check(!R::make(descriptor.data(),R::MaxBacking+1,0,rgba,plan));check(!R::make(descriptor.data(),256,1,rgba,plan));
 std::cout<<"{\"passed\":true,\"cases\":"<<cases<<",\"checks\":"<<checks<<",\"rejected_mutations\":"<<rejections<<",\"gpu_executed\":false}\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
