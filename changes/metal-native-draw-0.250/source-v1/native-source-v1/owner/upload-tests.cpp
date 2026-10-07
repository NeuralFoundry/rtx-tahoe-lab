#define _CRT_SECURE_NO_WARNINGS
#include "LibraryUpload.hpp"
#include "ReusableProgram.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <memory>
namespace U=RtxLibraryUpload036;namespace N=RtxReusable035;namespace P=U::P;
static unsigned checks=0,scenarios=0,rejections=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"check %u failed at %u: %s\n",checks,unsigned(__LINE__),#x);std::exit(2);}}while(0)
using Body=std::array<uint8_t,U::PayloadBytes>;using Header=std::array<uint8_t,U::HeaderBytes>;using Info=std::array<uint8_t,U::InfoBytes>;
static U::Scope owner{UINT64_C(0x130603601),UINT64_C(0x2036036036),true};
static void read(const std::string &path,void *out,size_t n){auto *f=std::fopen(path.c_str(),"rb");CHECK(f);CHECK(std::fread(out,1,n,f)==n);CHECK(std::fgetc(f)==EOF);std::fclose(f);}
static void dump(const std::string &path,const void *out,size_t n){auto *f=std::fopen(path.c_str(),"wb");CHECK(f);CHECK(std::fwrite(out,1,n,f)==n);CHECK(std::fclose(f)==0);}
static Header header(const Body &body,uint64_t gen=owner.generation){
 Header h{};P::Q::put64(h.data(),U::HeaderMagic);P::Q::put32(h.data()+8,1);P::Q::put32(h.data()+12,128);P::Q::put64(h.data()+16,gen);
 P::Q::put32(h.data()+24,4608);P::Q::put32(h.data()+28,1024);P::Q::put32(h.data()+32,5);P::Q::put32(h.data()+36,0x86);
 GSPDigest::SHA256 d;d.update(body.data(),4608);d.finish(h.data()+40);return h;
}
static Info info(const U::State &s){Info v{};CHECK(s.info(v.data(),v.size()));return v;}
static void append(U::State &s,const Body &b,const U::Scope &scope=owner){
 for(unsigned off=0;off<4608;off+=1024){const unsigned n=4608-off<1024?4608-off:1024;CHECK(s.append(scope,off,b.data()+off,n)==U::Error::None);}
}
static void prepared(U::State &s,const Body &b){++scenarios;const auto h=header(b);CHECK(s.begin(owner,h.data(),h.size())==U::Error::None);append(s,b);}
static void unchanged(U::State &s,const Info &before){CHECK(info(s)==before);++rejections;}
static void basic(const Body &body,const std::string &out){
 auto state=std::make_unique<U::State>();auto &s=*state;auto h=header(body);++scenarios;
 const auto empty=info(s);CHECK(s.seal(owner)==U::Error::Identity);CHECK(s.consume(owner)==U::Error::Identity);CHECK(!s.close(owner));unchanged(s,empty);
 const uint8_t *p=reinterpret_cast<const uint8_t*>(1);unsigned n=7;CHECK(!s.data(owner,0,p,n)&&!p&&!n);
 CHECK(!s.info(nullptr,256));CHECK(!s.info(reinterpret_cast<uint8_t*>(state.get()),256));
 U::Scope invalid=owner;invalid.generation=0;CHECK(s.begin(invalid,h.data(),128)==U::Error::Identity);invalid=owner;invalid.client=0;CHECK(s.begin(invalid,h.data(),128)==U::Error::Identity);
 invalid=owner;invalid.preparationAllowed=false;CHECK(s.begin(invalid,h.data(),128)==U::Error::Scope);unchanged(s,empty);
 CHECK(s.begin(owner,nullptr,128)==U::Error::Shape);CHECK(s.begin(owner,h.data(),127)==U::Error::Shape);CHECK(s.begin(owner,reinterpret_cast<uint8_t*>(state.get()),128)==U::Error::Shape);unchanged(s,empty);
 CHECK(s.begin(owner,h.data(),128)==U::Error::None);h.fill(0); // No retained caller header.
 dump(out+"/begin-info.bin",info(s).data(),256);
 for(unsigned i=0,off=0;off<4608;++i,off+=1024){
  const auto prior=info(s);const unsigned amount=4608-off<1024?4608-off:1024;
  CHECK(s.seal(owner)==U::Error::Incomplete);unchanged(s,prior);
  CHECK(s.consume(owner)==U::Error::State);unchanged(s,prior);
  CHECK(s.begin(owner,h.data(),128)==U::Error::State);unchanged(s,prior);
  invalid=owner;invalid.client++;CHECK(s.append(invalid,off,body.data()+off,amount)==U::Error::Identity);CHECK(s.seal(invalid)==U::Error::Identity);unchanged(s,prior);
  invalid=owner;invalid.generation++;CHECK(s.append(invalid,off,body.data()+off,amount)==U::Error::Identity);CHECK(!s.close(invalid));unchanged(s,prior);
  invalid=owner;invalid.preparationAllowed=false;CHECK(s.append(invalid,off,body.data()+off,amount)==U::Error::Scope);CHECK(s.seal(invalid)==U::Error::Scope);unchanged(s,prior);
  CHECK(s.append(owner,UINT64_MAX,body.data()+off,amount)==U::Error::Order);CHECK(s.append(owner,off+1,body.data()+off,amount)==U::Error::Order);unchanged(s,prior);
  CHECK(s.append(owner,off,body.data()+off,0)==U::Error::Shape);CHECK(s.append(owner,off,body.data()+off,amount-1)==U::Error::Shape);CHECK(s.append(owner,off,body.data()+off,amount+1)==U::Error::Shape);
  CHECK(s.append(owner,off,nullptr,amount)==U::Error::Shape);CHECK(s.append(owner,off,reinterpret_cast<const uint8_t*>(state.get()),amount)==U::Error::Shape);unchanged(s,prior);
  CHECK(!s.data(owner,0,p,n));Body copy=body;CHECK(s.append(owner,off,copy.data()+off,amount)==U::Error::None);copy.fill(0);
  CHECK(s.chunks()==i+1&&s.written()==off+amount);CHECK(s.append(owner,off,body.data()+off,amount)==U::Error::Order);
  dump(out+"/chunk-"+std::to_string(i)+"-info.bin",info(s).data(),256);
 }
 const auto full=info(s);CHECK(s.append(owner,4608,body.data(),1)==U::Error::Shape);unchanged(s,full);
 CHECK(s.seal(owner)==U::Error::None);CHECK(s.phase()==U::Phase::Ready);const auto ready=info(s);dump(out+"/ready-info.bin",ready.data(),256);
 CHECK(s.data(owner,0,p,n)&&n==512&&U::equal(p,body.data(),n));const uint8_t *library=p;
 CHECK(s.data(owner,1,p,n)&&n==4096&&U::equal(p,body.data()+512,n));const uint8_t *code=p;
 CHECK(!s.data(owner,2,p,n)&&!p&&!n);CHECK(s.seal(owner)==U::Error::State);CHECK(s.append(owner,0,body.data(),1024)==U::Error::State);unchanged(s,ready);
 invalid=owner;invalid.preparationAllowed=false;CHECK(s.consume(invalid)==U::Error::Scope);invalid=owner;invalid.generation++;CHECK(s.consume(invalid)==U::Error::Identity);unchanged(s,ready);
 CHECK(s.consume(owner)==U::Error::None);CHECK(s.phase()==U::Phase::Consumed);dump(out+"/consumed-info.bin",info(s).data(),256);
 CHECK(s.consume(owner)==U::Error::State);CHECK(s.begin(owner,h.data(),128)==U::Error::State);CHECK(s.append(owner,0,body.data(),1024)==U::Error::State);
 CHECK(s.close(owner));CHECK(s.phase()==U::Phase::Closed);CHECK(!s.close(owner));CHECK(!s.data(owner,0,p,n));
 CHECK(U::equal(library,body.data(),512)&&U::equal(code,body.data()+512,4096));dump(out+"/closed-info.bin",info(s).data(),256);
}
static void corruptions(const Body &body,const std::string &out){
 const auto original=header(body);
 for(unsigned i=0;i<128;++i){auto s=std::make_unique<U::State>();auto h=original;h[i]^=1;++scenarios;
  const auto e=s->begin(owner,h.data(),128);
  if(i>=40&&i<72){CHECK(e==U::Error::None);append(*s,body);CHECK(s->seal(owner)==U::Error::Digest);CHECK(s->phase()==U::Phase::Rejected);}
  else {CHECK(e==U::Error::Shape);CHECK(s->phase()==U::Phase::Empty);}++rejections;
 }
 for(unsigned i=0;i<4608;++i){auto s=std::make_unique<U::State>();Body b=body;b[i]^=1;++scenarios;
  CHECK(s->begin(owner,original.data(),128)==U::Error::None);append(*s,b);CHECK(s->seal(owner)==U::Error::Digest);
  const auto rejected=info(*s);CHECK(s->terminalError()==U::Error::Digest);CHECK(s->seal(owner)==U::Error::State);CHECK(s->append(owner,0,body.data(),1024)==U::Error::State);unchanged(*s,rejected);
  if(i==512)dump(out+"/bad-digest-info.bin",rejected.data(),256);
 }
 // Correct transport hashes cannot make unsupported/malformed ABI safe.
 const unsigned offsets[]={0,8,12,16,20,24,28,64,68,72,84,88,92,100,104,108,112,116,120,140,400,511,512+2303,4607};
 for(auto off:offsets){Body b=body;b[off]^=0x80;auto s=std::make_unique<U::State>();prepared(*s,b);CHECK(s->seal(owner)==U::Error::Library);++rejections;}
 for(unsigned local:{65u,128u,1024u}){Body b=body;P::Q::put32(b.data()+80,local);auto s=std::make_unique<U::State>();prepared(*s,b);CHECK(s->seal(owner)==U::Error::Profile);++rejections;}
 // Close before sealing must not report a validated package, including an
 // all-zero requested digest (actual digest storage has not yet been written).
 for(unsigned count=0;count<=5;++count){auto s=std::make_unique<U::State>();auto h=original;for(unsigned i=40;i<72;++i)h[i]=0;++scenarios;
  CHECK(s->begin(owner,h.data(),128)==U::Error::None);for(unsigned i=0;i<count;++i){unsigned off=i*1024,n=off==4096?512:1024;CHECK(s->append(owner,off,body.data()+off,n)==U::Error::None);}
  CHECK(s->close(owner));auto v=info(*s);CHECK(P::get32(v.data()+56)==0&&P::get32(v.data()+128)==0);dump(out+"/unsealed-close-"+std::to_string(count)+".bin",v.data(),v.size());
 }
}
static void package(const Body &b,const std::string &out,unsigned index){
 auto s=std::make_unique<U::State>();prepared(*s,b);CHECK(s->seal(owner)==U::Error::None);CHECK(s->consume(owner)==U::Error::None);
 const uint8_t *lib=nullptr,*code=nullptr;unsigned n=0;CHECK(s->data(owner,0,lib,n)&&n==512);CHECK(s->data(owner,1,code,n)&&n==4096);
 P::Library decoded;CHECK(P::decode(lib,512,code,4096,decoded));dump(out+"/package-"+std::to_string(index)+"-info.bin",info(*s).data(),256);
 // Real runtime plan builder consumes uploaded immutable pointers directly.
 // No arithmetic simulation or GPU result is manufactured in these tests.
 for(unsigned program=0;program<decoded.count;++program){N::Request req;req.generation=owner.generation;req.serial=program+1;req.program=program;req.groups=1;
  for(unsigned i=0;i<decoded.programs[program].parameters*256;++i)req.data[i]=uint8_t(i*31+program*7);
  auto plan=std::make_unique<N::Plan>();CHECK(N::build(lib,code,req,*plan));CHECK(plan->launch.programVA==P::Q::ProgramVA+decoded.programs[program].offset);
  const std::string prefix=out+"/package-"+std::to_string(index)+"-program-"+std::to_string(program);
  dump(prefix+"-qmd.bin",plan->qmd,256);dump(prefix+"-cb.bin",plan->constant,4096);dump(prefix+"-command.bin",plan->command,56);
 }
}
int main(int argc,char **argv){
 CHECK(argc==3);const std::string root=argv[1],out=argv[2];Body body{};read(root+"/library.bin",body.data(),512);read(root+"/code.bin",body.data()+512,4096);
 basic(body,out);corruptions(body,out);
 for(unsigned i=0;i<4;++i){Body b{};read(root+"/fixtures/package-"+std::to_string(i)+".bin",b.data(),b.size());package(b,out,i);}
 for(uint64_t gen:{UINT64_C(1),UINT64_C(0xffffffff),UINT64_C(0x100000000),UINT64_MAX}){auto s=std::make_unique<U::State>();auto h=header(body,gen);U::Scope scope{gen,owner.client,true};++scenarios;CHECK(s->begin(scope,h.data(),128)==U::Error::None);append(*s,body,scope);CHECK(s->seal(scope)==U::Error::None);CHECK(s->consume(scope)==U::Error::None);CHECK(s->generation()==gen);}
 std::printf("{\"passed\":true,\"checks\":%u,\"scenarios\":%u,\"rejections\":%u,\"state_bytes\":%zu,\"packages\":4,\"gpu_commands_submitted\":false}\n",checks,scenarios,rejections,sizeof(U::State));
 return 0;
}
