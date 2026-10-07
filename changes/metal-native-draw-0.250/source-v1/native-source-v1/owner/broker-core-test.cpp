#include "RTXBrokerWire.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
using namespace RTXBroker040;
static unsigned checks;
#define CHECK(...) do{++checks;if(!(__VA_ARGS__)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#__VA_ARGS__);std::abort();}}while(0)
struct Backend {
 std::array<uint8_t,RTXLibrary036::Bytes> image{};unsigned claims=0,calls=0;bool available=true,fail=false,wrongCompletion=false;
 std::vector<std::array<uint8_t,2112>> requests;
 bool claim(decltype(image)&out,uint64_t &gen,uint64_t &completed){++claims;if(!available)return false;out=image;gen=37;completed=0;return true;}
 bool execute(const std::array<uint8_t,2112> &request,std::array<uint8_t,2048>&result,uint64_t &completion){
  ++calls;requests.push_back(request);result.fill(0x5a);completion=P::get64(request.data()+24)+(wrongCompletion?1:0);return !fail;
 }
};
static HeaderView header(const Frame &f){HeaderView h;CHECK(read(f.bytes.data(),f.size,h)&&h.reply);return h;}
static Frame hello(Core &c,Peer &p,Backend &b,uint64_t gen=37){auto f=write({Op::Hello,Status::OK,false,0,1,gen,0});return c.receive(p,f.bytes.data(),f.size,true,b);}
static Frame execute(Peer &p,uint64_t metalSerial,uint64_t sequence,const Backend &b){
 RTXLibrary036::Catalog catalog;CHECK(RTXLibrary036::decode(b.image.data(),b.image.size(),catalog));
 RtxReusable035::Request r;r.generation=37;r.serial=metalSerial;r.program=0;r.groups=1;
 std::array<uint8_t,2112> bytes{};CHECK(RtxReusable035::encode(r,catalog.library,bytes.data(),bytes.size()));
 return write({Op::Execute,Status::OK,false,p.session,sequence,37,0},bytes.data(),bytes.size());
}
static void save(const std::string &path,const void *data,size_t n){std::ofstream f(path,std::ios::binary);f.write(static_cast<const char *>(data),n);CHECK(bool(f));}
int main(int argc,const char **argv){if(argc!=3)return 2;Backend original;std::ifstream input(argv[1],std::ios::binary);input.read(reinterpret_cast<char *>(original.image.data()),original.image.size());CHECK(input.gcount()==original.image.size()&&input.peek()==EOF);
 Backend b=original;Core c;Peer a,peerB;auto ha=hello(c,a,b),hb=hello(c,peerB,b);CHECK(header(ha).status==Status::OK&&header(hb).status==Status::OK&&a.session!=peerB.session&&b.claims==1);
 std::vector<Frame> sent,replies;
 for(unsigned i=0;i<3;++i){Peer &p=i==1?peerB:a;uint64_t serial=i==2?2:1,sequence=i==2?3:2;Frame f=execute(p,serial,sequence,b);Frame r=c.receive(p,f.bytes.data(),f.size,true,b);
  auto h=header(r);CHECK(h.status==Status::OK&&h.nativeSerial==i+1&&r.size==64+2048);CHECK(P::get64(f.bytes.data()+64+24)==serial);CHECK(P::get64(b.requests.back().data()+24)==i+1);
  CHECK(std::memcmp(f.bytes.data()+64+64,b.requests.back().data()+64,2048)==0);sent.push_back(f);replies.push_back(r);
 }
 CHECK(b.calls==3&&c.completed()==3&&c.executions()==3);
 Frame duplicate=c.receive(a,sent[2].bytes.data(),sent[2].size,true,b);CHECK(header(duplicate).status==Status::BadRequest&&a.closed&&b.calls==3&&!c.failed());
 auto end=write({Op::Close,Status::OK,false,peerB.session,3,37,0});CHECK(header(c.receive(peerB,end.bytes.data(),end.size,true,b)).status==Status::OK&&peerB.closed);
 for(unsigned i=0;i<3;++i){std::string stem=std::string(argv[2])+"/translation-"+std::to_string(i+1);save(stem+"-client.bin",sent[i].bytes.data(),sent[i].size);save(stem+"-native.bin",b.requests[i].data(),b.requests[i].size());save(stem+"-reply.bin",replies[i].bytes.data(),replies[i].size);}
 auto h=write({Op::Hello,Status::OK,false,0,1,37,0});
 {Core core;Peer p;Backend io=original;io.available=false;CHECK(header(core.receive(p,h.bytes.data(),h.size,true,io)).status==Status::NotReady&&io.calls==0&&!core.ready());}
 {Core core;Peer p;Backend io=original;CHECK(header(core.receive(p,h.bytes.data(),h.size,false,io)).status==Status::Denied&&io.claims==0);}
 for(unsigned offset: {0u,8u,12u,16u,20u,24u,32u,48u,56u,57u,58u,59u,60u,61u,62u,63u}){
  Core core;Peer p;Backend io=original;auto bad=h;bad.bytes[offset]^=0x40;CHECK(header(core.receive(p,bad.bytes.data(),bad.size,true,io)).status==Status::BadRequest&&io.claims==0);
 }
 for(size_t size: {size_t(0),size_t(63),size_t(65),size_t(MaxFrame+1)}){Core core;Peer p;Backend io=original;CHECK(header(core.receive(p,h.bytes.data(),size,true,io)).status==Status::BadRequest&&io.claims==0);}
 for(unsigned mode=0;mode<7;++mode){Core core;Peer p,q;Backend io=original;hello(core,p,io);hello(core,q,io);auto f=execute(p,1,2,io);
  if(mode==0)P::Q::put64(f.bytes.data()+24,q.session);
  if(mode==1)P::Q::put64(f.bytes.data()+40,38);
  if(mode==2)P::Q::put64(f.bytes.data()+32,3);
  if(mode==3)P::Q::put64(f.bytes.data()+64+24,2);
  if(mode==4)P::Q::put64(f.bytes.data()+64+16,38);
  if(mode==5)P::Q::put32(f.bytes.data()+64+32,99);
  if(mode==6)f.bytes[64+40]=1;
  CHECK(header(core.receive(p,f.bytes.data(),f.size,true,io)).status==Status::BadRequest&&io.calls==0&&!core.failed());
 }
 for(bool wrong:{false,true}){Core core;Peer p;Backend io=original;hello(core,p,io);io.fail=!wrong;io.wrongCompletion=wrong;auto f=execute(p,1,2,io);auto r=core.receive(p,f.bytes.data(),f.size,true,io);
  CHECK(header(r).status==Status::Backend&&r.size==64&&core.failed()&&io.calls==1);CHECK(header(core.receive(p,f.bytes.data(),f.size,true,io)).status==Status::Closed&&io.calls==1);
  Peer another;CHECK(header(hello(core,another,io)).status==Status::Closed&&io.claims==1);
 }
 std::printf("{\"passed\":true,\"checks\":%u,\"translated_requests\":3,\"native_serials\":[1,2,3],\"client_serials\":[1,1,2],\"cpu_fixture_only\":true,\"gpu_commands_submitted\":false}\n",checks);
}
