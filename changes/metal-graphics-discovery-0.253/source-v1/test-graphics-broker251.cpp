#include "app/OwnedGraphicsBroker251.hpp"
#include <cstdio>
#include <stdexcept>
namespace G=RTXOwnedGraphicsBroker251;namespace D=RTXDrawTransfer248;namespace C=RTXGraphicsCatalog247;
static unsigned checks=0,negative=0;
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(#x);}while(0)
struct Backend{
 unsigned claims=0,calls=0,retirements=0,mode=0;uint64_t done=0;
 uint64_t generation()const{return 251;}
 bool claim(uint64_t&gen,uint64_t&serial){++claims;gen=251;serial=0;if(mode==21)++gen;if(mode==22)serial=1;if(mode==23)throw std::runtime_error("claim uncertain");return mode!=20;}
 bool execute(const D::Bytes&r,D::Bytes&out,uint64_t&completion){
  ++calls;D::Plan p;CHECK(G::decode(r,251,done+1,p));out.assign(r.begin()+D::Header,r.end());
  // Explicit CPU transfer sentinel. Never presented as GPU rasterization.
  for(unsigned y=0;y<p.height;++y)for(unsigned x=0;x<p.width*4;++x)out[size_t(p.vertexBytes+p.colorOffset+y*p.pitch+x)]^=uint8_t(calls);
  completion=++done;if(mode==1)++completion;if(mode==2)out[0]^=1;if(mode==3)out.pop_back();if(mode==4)throw std::runtime_error("lost reply after write");return mode!=5;
 }
 void retire(){++retirements;}
};
static D::Bytes request(uint64_t serial){
 D::Input i;i.generation=251;i.serial=serial;i.vertexBytes=113;i.colorBytes=33041;i.vertexOffset=16;i.firstVertex=2;i.vertexCount=3;i.colorOffset=256;i.width=i.height=64;i.pitch=512;i.programDigest=G::digest();
 D::Bytes v(113,7),c(33041,9),out;float f[]={-.75f,-.75f,0,0,.75f,-.75f,1,0,0,.75f,.5f,1};std::memcpy(v.data()+48,f,sizeof(f));CHECK(D::assemble(i,v,c,out));return out;
}
struct Fixture{
 Backend b;G::Core core;G::Peer peer;G::Client client{C::Image,C::Bytes,251};
 void hello(){G::Frame in;D::Bytes out;uint64_t complete=99;CHECK(client.hello(in));auto r=core.receive(peer,in.bytes.data(),in.size(),true,b);CHECK(client.accept(r.bytes.data(),r.size(),0,123,out,complete));CHECK(!complete&&out.empty());}
 G::Frame draw(uint64_t s=1){G::Frame in;auto r=request(s);CHECK(client.execute(r.data(),r.size(),C::Image,C::Bytes,in));return in;}
};
int main(){try{
 // Publication preparation must claim exactly once, including failure paths.
 {Fixture t;CHECK(t.core.prepare(t.b)&&t.core.ready()&&t.b.claims==1&&!t.b.calls);CHECK(t.core.prepare(t.b)&&t.b.claims==1);t.hello();CHECK(t.b.claims==1);t.core.stop(t.b);CHECK(!t.core.prepare(t.b)&&t.b.claims==1&&t.b.retirements==1);}
 for(unsigned mode=20;mode<=23;++mode){Fixture t;t.b.mode=mode;CHECK(!t.core.prepare(t.b)&&t.core.failed()&&!t.core.ready());CHECK(!t.core.prepare(t.b)&&t.b.claims==1&&t.b.retirements==1);t.core.stop(t.b);CHECK(t.b.retirements==1);++negative;}
 // Interleaving peers must allocate global native serials but return local ones.
 Fixture f;f.hello();G::Client second(C::Image,C::Bytes,251);G::Peer peer2;G::Frame in;D::Bytes output;uint64_t completion=0;
 CHECK(second.hello(in));auto r=f.core.receive(peer2,in.bytes.data(),in.size(),true,f.b);CHECK(second.accept(r.bytes.data(),r.size(),0,123,output,completion));CHECK(f.b.claims==1);
 for(unsigned i=0;i<6;++i){auto&client=(i%2)?second:f.client;auto&peer=(i%2)?peer2:f.peer;auto req=request(i/2+1);CHECK(client.execute(req.data(),req.size(),C::Image,C::Bytes,in));r=f.core.receive(peer,in.bytes.data(),in.size(),true,f.b);CHECK(client.accept(r.bytes.data(),r.size(),0,123,output,completion));CHECK(completion==i/2+1&&client.nativeSerial()==i+1&&f.b.calls==i+1);D::Plan p;CHECK(G::decode(req,251,completion,p)&&D::readback(p,req,output.data(),output.size(),completion));}
 CHECK(f.client.close(in));r=f.core.receive(f.peer,in.bytes.data(),in.size(),true,f.b);CHECK(f.client.accept(r.bytes.data(),r.size(),0,123,output,completion));CHECK(f.b.retirements==0);f.core.stop(f.b);f.core.stop(f.b);CHECK(f.b.retirements==1);
 // Corrupt reply header, guards, size, PID and UID. Never retry a failed client.
 for(unsigned mode=0;mode<11;++mode){Fixture t;t.hello();auto q=t.draw();auto reply=t.core.receive(t.peer,q.bytes.data(),q.size(),true,t.b);uint32_t uid=0;int64_t pid=123;
  switch(mode){case 0:reply.bytes[0]^=1;break;case 1:reply.bytes[24]^=1;break;case 2:reply.bytes[32]^=1;break;case 3:reply.bytes[40]^=1;break;case 4:reply.bytes[48]=0;break;case 5:reply.bytes[64]^=1;break;case 6:reply.bytes[G::Header]^=1;break;case 7:reply.bytes.pop_back();break;case 8:uid=501;break;case 9:pid=124;break;case 10:reply.bytes[56]=1;break;}
  D::Bytes out{42};completion=999;CHECK(!t.client.accept(reply.bytes.data(),reply.size(),uid,pid,out,completion));CHECK(out==D::Bytes{42}&&!completion);CHECK(!t.client.execute(request(2).data(),request(2).size(),C::Image,C::Bytes,in));CHECK(t.b.calls==1);++negative;
 }
 // Native failure after possibly effective I/O retires the shared owner once.
 for(unsigned mode=1;mode<=5;++mode){Fixture t;t.hello();t.b.mode=mode;auto q=t.draw();auto reply=t.core.receive(t.peer,q.bytes.data(),q.size(),true,t.b);D::Bytes out;CHECK(!t.client.accept(reply.bytes.data(),reply.size(),0,123,out,completion));CHECK(t.core.failed()&&t.b.calls==1&&t.b.retirements==1);t.core.receive(t.peer,q.bytes.data(),q.size(),true,t.b);CHECK(t.b.calls==1);t.core.stop(t.b);CHECK(t.b.retirements==1);++negative;}
 // Bypass client validation: server must independently reject these requests.
 for(unsigned mode=0;mode<9;++mode){Fixture t;t.hello();auto q=t.draw();switch(mode){case 0:q.bytes[G::Header+24]=2;break;case 1:D::put32(q.bytes.data()+G::Header+72,65);break;case 2:D::put32(q.bytes.data()+G::Header+68,6);break;case 3:q.bytes[G::Header+96]^=1;break;case 4:D::put32(q.bytes.data()+G::Header+D::Header+48,0x40000000);break;case 5:D::put32(q.bytes.data()+G::Header+D::Header+56,0x7f800000);break;case 6:q.bytes[24]^=1;break;case 7:q.bytes[32]^=1;break;case 8:q.bytes[40]^=1;break;}
  t.core.receive(t.peer,q.bytes.data(),q.size(),true,t.b);CHECK(t.peer.closed&&t.b.calls==0&&!t.core.failed());++negative;
 }
 for(unsigned mode=0;mode<3;++mode){Fixture t;G::Frame q;CHECK(t.client.hello(q));if(mode==1)D::put64(q.bytes.data()+40,999);if(mode==2)q.bytes[64]^=1;t.core.receive(t.peer,q.bytes.data(),q.size(),mode!=0,t.b);CHECK(t.b.claims==0&&t.b.calls==0&&t.peer.closed);++negative;}
 std::printf("{\"passed\":true,\"checks\":%u,\"negative_cases\":%u,\"interleaved_draws\":6,\"gpu_executed\":false}\n",checks,negative);return 0;
}catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
