#include "app/OwnedBatch187.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
static unsigned checks=0;
static void check(bool v){++checks;if(!v)throw std::runtime_error("check "+std::to_string(checks));}
static void rehash(std::vector<uint8_t>&b){check(b.size()==5248);GSPDigest::SHA256 h;h.update(b.data()+128,5120);h.finish(b.data()+32);}
int main(int argc,char**argv){try{
 check(argc==3);std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t>image((std::istreambuf_iterator<char>(f)),{});
 RTXCatalog187::Catalog catalog;check(RTXCatalog187::decode(image.data(),image.size(),catalog));
 check(catalog.abi==2&&catalog.containerABI==3&&catalog.library.count==1);
 const auto&p=catalog.library.programs[0];check(p.parameters==3&&p.readMask==5&&p.writeMask==2);
 check(catalog.resources[0].kind==2&&catalog.resources[0].index==0&&catalog.resources[1].record==1&&catalog.resources[2].kind==3);
 for(unsigned offset:{256u,260u,264u,268u,272u,276u,280u,284u,288u,292u,296u,300u,304u,383u,384u,639u}){
  auto bad=image;bad[offset]=255;rehash(bad);RTXCatalog187::Catalog rejected;rejected.abi=99;
  check(!RTXCatalog187::decode(bad.data(),bad.size(),rejected));check(rejected.abi==99);
 }
 for(unsigned abi:{1u,2u}){auto bad=image;RTXTexture225::put(bad.data()+8,abi);check(!RTXCatalog187::decode(bad.data(),bad.size(),catalog));}
 check(RTXCatalog187::decode(image.data(),image.size(),catalog));
 RTXTexture224::Layout layout;check(RTXTexture224::make(RTXTexture224::Format::R32Float,17,9,256,256,4097,layout));
 RTXTexture225::Descriptor descriptor;check(RTXTexture225::descriptor(layout,4097,descriptor));
 const unsigned expected[]={RTXTexture225::Magic,1,17,9,256,4,3,2304};
 for(unsigned i=0;i<8;++i)check(RTXTexture225::get(descriptor.data()+i*4)==expected[i]);
 for(unsigned kind=1;kind<=10;++kind){auto altered=layout;altered.format=RTXTexture224::Format(kind);auto sentinel=descriptor;
  const bool accepted=RTXTexture225::descriptor(altered,4097,sentinel);check(accepted==(kind==3));if(!accepted)check(sentinel==descriptor);}
 for(unsigned field=0;field<7;++field){auto bad=layout;
  switch(field){case 0:bad.width=0;break;case 1:bad.height=16385;break;case 2:bad.pixel=16;break;case 3:bad.offset=1;break;case 4:bad.rowPitch=16;break;case 5:bad.storageBytes=4;break;default:bad.offset=4096;}
  auto sentinel=descriptor;check(!RTXTexture225::descriptor(bad,4097,sentinel));check(sentinel==descriptor);}
 auto sentinel=descriptor;check(!RTXTexture225::descriptor(layout,2559,sentinel));check(sentinel==descriptor);

 for(unsigned kind=1;kind<=10;++kind){
  RTXTexture224::Layout each;check(RTXTexture224::allocate(RTXTexture224::Format(kind),17,9,each));
  auto value=descriptor;const bool good=RTXTexture225::descriptor(each,each.storageBytes,value,RTXTexture225::FloatFormats226);
  check(good==(kind==3||kind==4||kind==7||kind==10));
  if(good){check(RTXTexture225::get(value.data()+20)==each.pixel);check(RTXTexture225::get(value.data()+24)==kind);}
  else check(value==descriptor);
  for(uint32_t badContract:{0u,4u,10u,0x80000008u,0x80000499u,0xffffffffu}){
   value=descriptor;check(!RTXTexture225::descriptor(each,each.storageBytes,value,badContract));check(value==descriptor);
  }
 }
 check(catalog.resources[0].format==RTXTexture225::FloatFormats226&&catalog.resources[1].format==RTXTexture225::FloatFormats226);
 for(uint32_t contract:{3u,RTXTexture225::FloatFormats226}){
  auto alternative=image;RTXTexture225::put(alternative.data()+268,contract);rehash(alternative);
  RTXCatalog187::Catalog c;check(RTXCatalog187::decode(alternative.data(),alternative.size(),c));check(c.resources[0].format==contract);
 }
 RTXBatch187::Input inputs[]={{1,4097,256,0},{2,4097,512,1},{3,256,0,2}};
 RTXBatch187::Plan plan;check(RTXBatch187::plan(p,0,inputs,3,{3,3,1},{8,4,1},225,1,plan));
 check(plan.count==3&&plan.resources==3&&plan.payloadBytes==8450);
 check(plan.binding[0].offset==256&&plan.binding[1].offset==512);
 check(plan.resource[0].access==1&&plan.resource[1].access==2&&plan.resource[2].access==1);
 std::array<RTXBatch187::Bytes,4> snapshots{};
 for(unsigned i=0;i<plan.resources;++i)snapshots[i].resize(size_t(plan.resource[i].bytes),uint8_t(17+i));
 std::copy(descriptor.begin(),descriptor.end(),snapshots[2].begin());std::copy(descriptor.begin(),descriptor.end(),snapshots[2].begin()+32);
 RTXBatch187::Bytes request;check(RTXBatch187::assemble(plan,snapshots,request));RTXBatch187::Plan decoded;
 check(RTXBatch187::decode(request.data(),request.size(),catalog.library,225,1,decoded));
 auto result=RTXBatch187::Bytes(request.begin()+512,request.end());check(RTXBatch187::readback(plan,request.data(),request.size(),result.data(),result.size()));
 result[size_t(plan.resource[2].payloadOffset)]^=1;check(!RTXBatch187::readback(plan,request.data(),request.size(),result.data(),result.size()));
 std::ofstream out(argv[2],std::ios::binary);out.write(reinterpret_cast<const char*>(request.data()),std::streamsize(request.size()));out.close();check(bool(out));
 std::cout<<"{\"passed\":true,\"checks\":"<<checks<<",\"request_bytes\":"<<request.size()<<",\"gpu_executed\":false}\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
