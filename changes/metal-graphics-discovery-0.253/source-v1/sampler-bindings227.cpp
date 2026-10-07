#include "app/OwnedBatch187.hpp"
#include "app/RTXSamplerState227.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
static unsigned checks=0;
static void check(bool v){++checks;if(!v)throw std::runtime_error("check "+std::to_string(checks));}
static void hash(std::vector<uint8_t>&v){GSPDigest::SHA256 h;h.update(v.data()+128,5120);h.finish(v.data()+32);}
int main(int argc,char**argv){try{
 check(argc==3);using namespace RTXSampler227;
 unsigned states=0;Descriptor sentinel;sentinel.fill(0xa5);
 for(uint32_t n=0;n<3;++n)for(uint32_t s=0;s<5;++s)for(uint32_t t=0;t<5;++t)for(uint32_t f=0;f<3;++f){
  State v{n,s,t,f};auto d=sentinel;const bool expected=n<2&&s<4&&t<4&&f<2&&(n||((s==0||s==3)&&(t==0||t==3)));
  check(encode(v,d)==expected);if(!expected){check(d==sentinel);continue;}++states;
  State decoded;check(decode(d.data(),decoded));check(decoded.normalized==n&&decoded.s==s&&decoded.t==t&&decoded.filter==f);
  check(get(d.data())==Magic&&get(d.data()+4)==1&&get(d.data()+24)==0&&get(d.data()+28)==0);
  for(unsigned i=0;i<32;++i){auto corrupt=d;corrupt[i]^=0x80;State before{99,99,99,99};const bool okay=decode(corrupt.data(),before);
   if(okay){Descriptor encoded;check(encode(before,encoded)&&encoded==corrupt);}
   else check(before.normalized==99&&before.s==99&&before.t==99&&before.filter==99);
  }
 }
 check(states==40);
 std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t>image((std::istreambuf_iterator<char>(f)),{});check(image.size()==5248);
 RTXCatalog187::Catalog c;check(RTXCatalog187::decode(image.data(),image.size(),c));check(c.abi==2&&c.containerABI==3&&c.library.count==1);
 const auto&p=c.library.programs[0];check(p.parameters==5&&p.readMask==29&&p.writeMask==2);check(p.localX==64&&p.localY==1&&p.localZ==1);
 check(c.resources[2].kind==2&&c.resources[2].record==0&&c.resources[2].format==RTXTexture225::FloatFormats226);
 check(c.resources[3].kind==4&&c.resources[3].record==1&&c.resources[3].index==0&&c.resources[4].kind==3);
 for(unsigned offset:{304u,308u,312u,316u,320u,324u,328u,332u,336u,383u,384u}){
  auto bad=image;bad[offset]=255;hash(bad);RTXCatalog187::Catalog rejected;rejected.abi=99;check(!RTXCatalog187::decode(bad.data(),bad.size(),rejected));check(rejected.abi==99);
 }
 for(unsigned mask:{0u,21u,31u}){auto bad=image;RTXTexture225::put(bad.data()+736,mask);if(mask==31)RTXTexture225::put(bad.data()+740,10);hash(bad);RTXCatalog187::Catalog rejected;check(!RTXCatalog187::decode(bad.data(),bad.size(),rejected));}
 RTXTexture224::Layout layout;check(RTXTexture224::make(RTXTexture224::Format::RGBA8Unorm,8,4,512,512,4097,layout));
 RTXTexture225::Descriptor texture;check(RTXTexture225::descriptor(layout,4097,texture,RTXTexture225::FloatFormats226));Descriptor sampler;check(encode({1,1,3,1},sampler));
 RTXBatch187::Input inputs[]={{1,4097,128,0},{2,4097,256,1},{3,4097,512,2},{4,256,32,3},{4,256,0,4}};RTXBatch187::Plan plan;
 check(RTXBatch187::plan(p,0,inputs,5,{2,1,1},{64,1,1},227,1,plan));check(plan.count==5&&plan.resources==4&&plan.payloadBytes==12547);
 check(plan.binding[3].resource==plan.binding[4].resource&&plan.binding[3].offset==32&&plan.binding[4].offset==0);check(plan.resource[3].access==1);
 std::array<RTXBatch187::Bytes,4> snapshots{};for(unsigned i=0;i<4;++i)snapshots[i].resize(size_t(plan.resource[i].bytes),uint8_t(17+i));
 std::fill(snapshots[3].begin(),snapshots[3].end(),uint8_t(0));std::copy(texture.begin(),texture.end(),snapshots[3].begin());std::copy(sampler.begin(),sampler.end(),snapshots[3].begin()+32);
 RTXBatch187::Bytes request;check(RTXBatch187::assemble(plan,snapshots,request));RTXBatch187::Plan decoded;
 check(RTXBatch187::decode(request.data(),request.size(),c.library,227,1,decoded));auto native=RTXBatch187::native(plan);check(native[3].slot==native[4].slot&&native[3].offset==32&&native[4].offset==0);
 auto result=RTXBatch187::Bytes(request.begin()+512,request.end());check(RTXBatch187::readback(plan,request.data(),request.size(),result.data(),result.size()));
 for(unsigned offset:{0u,32u,63u,255u}){auto bad=result;bad[size_t(plan.resource[3].payloadOffset)+offset]^=1;check(!RTXBatch187::readback(plan,request.data(),request.size(),bad.data(),bad.size()));}
 for(size_t i=0;i<plan.resource[1].bytes;++i)result[size_t(plan.resource[1].payloadOffset)+i]^=0x5a;check(RTXBatch187::readback(plan,request.data(),request.size(),result.data(),result.size()));
 std::ofstream out(argv[2],std::ios::binary);out.write(reinterpret_cast<const char*>(request.data()),std::streamsize(request.size()));out.close();check(bool(out));
 std::cout<<"{\"passed\":true,\"checks\":"<<checks<<",\"sampler_states\":"<<states<<",\"request_bytes\":"<<request.size()<<",\"gpu_executed\":false}\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
