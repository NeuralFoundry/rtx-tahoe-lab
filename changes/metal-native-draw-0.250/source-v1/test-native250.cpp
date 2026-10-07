#include "test-owner243.cpp"
namespace D=RTXDrawTransfer248;
static Bytes appRequest250(Context&c,unsigned serial,unsigned width=64,unsigned height=64){
 D::Input i;i.generation=c.root->info.w[2];i.serial=serial;i.vertexBytes=113;i.colorBytes=33041;i.vertexOffset=16;i.firstVertex=2;i.vertexCount=3;i.width=width;i.height=height;i.pitch=512;i.colorOffset=256;
 GSPDigest::SHA256 sha;sha.update(RTXGraphicsTemplate240::Program,4096);sha.finish(i.programDigest.data());
 Bytes vertices(size_t(i.vertexBytes)),color(size_t(i.colorBytes)),request;
 for(size_t n=0;n<vertices.size();++n)vertices[n]=uint8_t(n*3+9);
 for(size_t n=0;n<color.size();++n)color[n]=uint8_t(n*7+serial);
 const float values[]={-.5f,-.25f,.125f,.25f,.75f,-.5f,.875f,0.f,-.25f,.875f,.25f,.75f};std::memcpy(vertices.data()+48,values,48);
 check(D::assemble(i,vertices,color,request));return request;
}
static Bytes expected250(Context&c,const Bytes&wire){
 D::Digest digest{};std::memcpy(digest.data(),wire.data()+96,32);D::Plan p;check(D::decode(wire.data(),wire.size(),c.root->info.w[2],D::u64(wire.data()+24),digest,p));
 Bytes out(wire.begin()+D::Header,wire.end());c.kernel->expected.resize(NG::ColorBytes);
 for(unsigned n=0;n<NG::ColorBytes;++n)c.kernel->expected[n]=uint8_t(n*29+7);
 for(unsigned y=0;y<p.height;++y)for(unsigned x=0;x<p.width*4;++x){
  // Sentinel transport test: no CPU triangle rasterizer or shader arithmetic.
  auto at=p.vertexBytes+p.colorOffset+uint64_t(y)*p.pitch+x;out[size_t(at)]^=0x6d;
  c.kernel->expected[NG::Offset+y*NG::Pitch+x]=out[size_t(at)];
 }
 return out;
}
int main(int argc,char**argv)try{
 check(argc==5);const fs::path fixturePath(argv[1]),out(argv[4]);check(fs::create_directory(out));const auto boot=read(argv[2]),oracle=read(argv[3]);
 unsigned frames=0,failures=0;auto good=std::make_unique<Context>(fixturePath,boot,oracle);check(good->start());
 for(unsigned serial=1;serial<=3;++serial){auto request=appRequest250(*good,serial,serial==2?17:64,serial==2?13:64);const auto expected=expected250(*good,request);Tree tree;Bytes result{4,5,6};uint64_t completion=0;
  check(good->session.draw(*good->kernel,tree,good->data,result,completion,&request)&&completion==serial&&result==expected);++frames;
  auto&wire=tree.all.at("submitted-request.bin");check(wire.size()==320&&std::memcmp(wire.data()+256,request.data()+D::Header+48,48)==0);
  const auto dir=out/("frame-"+std::to_string(serial));check(fs::create_directory(dir));write(dir/"application-request.bin",request.data(),request.size());write(dir/"application-result.bin",result.data(),result.size());
  for(auto&row:tree.all){auto p=dir/row.first;fs::create_directories(p.parent_path());write(p,row.second.data(),row.second.size());}
 }
 for(unsigned fault=0;fault<8;++fault){auto c=std::make_unique<Context>(fixturePath,boot,oracle);check(c->start());auto request=appRequest250(*c,1);auto*p=request.data();
  if(fault==0)p[96]^=1;if(fault==1)D::put64(p+24,2);if(fault==2)D::put32(p+68,6);if(fault==3)D::put32(p+72,65);if(fault==4)D::put32(p+76,65);if(fault==5)D::put32(p+D::Header+48,0x40000000);if(fault==6)p[128]=1;if(fault==7)request.pop_back();
  const auto calls=c->kernel->hostCalls;Tree tree;Bytes result{4,5,6};uint64_t completion=99;check(!c->session.draw(*c->kernel,tree,c->data,result,completion,&request)&&result==Bytes({4,5,6})&&!completion&&c->kernel->hostCalls==calls&&c->session.ready());++failures;
 }
 for(unsigned damage=1;damage<=15;++damage){auto c=std::make_unique<Context>(fixturePath,boot,oracle);check(c->start());auto request=appRequest250(*c,1);expected250(*c,request);c->kernel->damage=damage;Tree tree;Bytes result{4,5,6};uint64_t completion=99;
  check(!c->session.draw(*c->kernel,tree,c->data,result,completion,&request)&&result==Bytes({4,5,6})&&!completion&&c->session.failed());auto calls=c->kernel->hostCalls;check(!c->session.draw(*c->kernel,tree,c->data,result,completion,&request)&&c->kernel->hostCalls==calls);++failures;
 }
 std::cout<<"{\"passed\":true,\"checks\":"<<checks+KernelFixture241::checks<<",\"synthetic_frames\":"<<frames<<",\"negative_cases\":"<<failures<<",\"actual_native_io\":false,\"gpu_executed\":false,\"pixel_arithmetic_emulated\":false}\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
