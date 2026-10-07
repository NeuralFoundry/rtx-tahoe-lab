#define main upload_component_main
#include "upload-tests.cpp"
#undef main
#include "LibraryUploadABI.hpp"
namespace A=RtxLibraryUploadABI036;
int main(int argc,char **argv){
 CHECK(argc==3);const std::string root=argv[1],out=argv[2];Body body{};read(root+"/library.bin",body.data(),512);read(root+"/code.bin",body.data()+512,4096);
 auto s=std::make_unique<U::State>();auto h=header(body);std::array<uint8_t,1024> result{};std::array<uint64_t,4> args{};
 CHECK(!A::selector(71)&&A::selector(72)&&A::selector(76)&&!A::selector(77));
 A::Call infoCall{nullptr,0,nullptr,0,result.data(),256};CHECK(A::dispatch(*s,owner,72,infoCall)==U::Error::None);
 auto bad=infoCall;bad.scalarCount=1;CHECK(A::dispatch(*s,owner,72,bad)==U::Error::Shape);bad=infoCall;bad.output=nullptr;CHECK(A::dispatch(*s,owner,72,bad)==U::Error::Shape);
 bad=infoCall;bad.output=reinterpret_cast<uint8_t*>(s.get());CHECK(A::dispatch(*s,owner,72,bad)==U::Error::Shape);
 bad=infoCall;bad.output=reinterpret_cast<uint8_t*>(&owner);CHECK(A::dispatch(*s,owner,72,bad)==U::Error::Shape);
 bad=infoCall;bad.output=reinterpret_cast<uint8_t*>(&bad);CHECK(A::dispatch(*s,owner,72,bad)==U::Error::Shape);
 CHECK(A::dispatch(*s,owner,99,infoCall)==U::Error::Shape);
 for(unsigned selector=72;selector<=76;++selector){
  for(unsigned count=0;count<=4;++count)for(unsigned input:{0u,1u,128u,1024u,1025u})for(unsigned output:{0u,1u,256u,1024u,1025u}){
   // Distinct object per malformed-call case prevents prior state from hiding
   // an accidental upload. Only the exact begin shape may change Empty.
   auto fresh=std::make_unique<U::State>();A::Call call{args.data(),count,h.data(),input,result.data(),output};
   if(input>128)call.input=body.data();if(output>1024)call.output=nullptr;
   auto e=A::dispatch(*fresh,owner,selector,call);++scenarios;
   const bool begin=selector==73&&!count&&input==128&&!output;
   CHECK((fresh->phase()==U::Phase::Uploading)==begin);
   CHECK(e==U::Error::None?((selector==72&&!count&&!input&&output==256)||begin):true);
  }
 }
 A::Call begin{nullptr,0,h.data(),h.size(),nullptr,0};CHECK(A::dispatch(*s,owner,73,begin)==U::Error::None);
 for(unsigned off=0;off<4608;off+=1024){args[0]=off;A::Call call{args.data(),1,body.data()+off,off==4096?512u:1024u,nullptr,0};CHECK(A::dispatch(*s,owner,74,call)==U::Error::None);}
 A::Call seal{};CHECK(A::dispatch(*s,owner,75,seal)==U::Error::None);
 for(unsigned part=0;part<2;++part){const unsigned total=part?4096:512;for(unsigned off=0;off<total;off+=1024){unsigned n=total-off<1024?total-off:1024;args={part,off,n,0};
   A::Call call{args.data(),3,nullptr,0,result.data(),n};CHECK(A::dispatch(*s,owner,76,call)==U::Error::None);CHECK(U::equal(result.data(),body.data()+(part?512:0)+off,n));
  }
 }
 args={0,UINT64_MAX,512,0};A::Call readCall{args.data(),3,nullptr,0,result.data(),512};CHECK(A::dispatch(*s,owner,76,readCall)==U::Error::Shape);
 args={0,511,2,0};readCall.outputBytes=2;CHECK(A::dispatch(*s,owner,76,readCall)==U::Error::Shape);
 args={0,0,512,0};readCall.outputBytes=512;auto wrong=owner;wrong.client++;CHECK(A::dispatch(*s,wrong,76,readCall)==U::Error::State);
 CHECK(s->consume(owner)==U::Error::None);CHECK(A::dispatch(*s,owner,76,readCall)==U::Error::None);
 const auto prior=info(*s);CHECK(A::dispatch(*s,owner,73,begin)==U::Error::State);CHECK(A::dispatch(*s,owner,75,seal)==U::Error::State);unchanged(*s,prior);
 dump(out+"/entry-info.bin",prior.data(),256);CHECK(s->close(owner));CHECK(A::dispatch(*s,owner,76,readCall)==U::Error::State);
 std::printf("{\"passed\":true,\"checks\":%u,\"scenarios\":%u,\"gpu_commands_submitted\":false}\n",checks,scenarios);
}
