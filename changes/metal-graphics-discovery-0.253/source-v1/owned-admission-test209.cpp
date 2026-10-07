#include "app/ApplicationAdmission209.hpp"
#include "app/OwnedBroker208.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
namespace W=RTXOwnedBroker208;namespace B=RTXBatch187;namespace A=RTXApplication209;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::abort();}}while(0)
struct Model {
 std::array<uint8_t,RTXCatalog187::Bytes>image{};uint64_t generation=0;unsigned claims=0,executions=0,retirements=0;
 bool claim(decltype(image)&out,uint64_t&gen,uint64_t&completed){++claims;out=image;gen=generation;completed=0;return true;}
 bool admit(const uint8_t*payload,size_t n){return n==4608&&!std::memcmp(payload,image.data()+640,n);}
 bool execute(const B::Bytes&request,const uint8_t*payload,size_t n,B::Bytes&result,uint64_t&completion){if(!admit(payload,n))return false;++executions;result.assign(request.begin()+B::Header,request.end());completion=W::P::get64(request.data()+24);return true;}
 void retire(){++retirements;}
};
static std::vector<uint8_t>read(const char*path){std::ifstream f(path,std::ios::binary);CHECK(bool(f));return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char**argv){
 CHECK(argc==3);auto image=read(argv[1]),memory=read(argv[2]);RTXCatalog187::Catalog catalog;CHECK(RTXCatalog187::decode(image.data(),image.size(),catalog)&&catalog.abi==2);
 RTXAccelerator103::Binding b;CHECK(RTXMemory107::decode(memory.data(),unsigned(memory.size()),b.memory));b.childRegistry=101;b.parentGeneration=b.memory.generation;b.epoch=1;b.session=77;
 A::Ready ready;ready.abi=209;ready.protocol=208;ready.allowedUID=501;ready.rootPID=222;ready.child=b.childRegistry;ready.generation=b.parentGeneration;ready.epoch=b.epoch;ready.publicationSession=b.session;ready.rootABI=195;ready.hostABI=2;ready.dataABI=181;ready.dispatchABI=183;ready.publicationABI=2;ready.bootMatches=ready.catalogMatches=ready.memoryMatches=true;
 Model backend;backend.generation=b.parentGeneration;std::copy(image.begin(),image.end(),backend.image.begin());W::Core core;
 W::Peer peer1,peer2;W::Client client1(image.data(),image.size(),b.parentGeneration),client2(image.data(),image.size(),b.parentGeneration);
 auto exchange=[&](W::Peer&peer,W::Client&client,const W::Frame&request,B::Bytes&out,uint64_t&completion){auto reply=core.receive(peer,request.bytes.data(),request.size(),true,backend);return client.accept(reply.bytes.data(),reply.size(),0,222,out,completion);};
 W::Frame frame;B::Bytes result;uint64_t completion=0;CHECK(client1.hello(frame)&&exchange(peer1,client1,frame,result,completion));CHECK(client1.session()==1&&client1.nativeSerial()==0);
 const B::Input inputs[]={{101,4097,4,0},{102,8193,4092,1},{103,16385,12284,2}};B::Plan batch;
 CHECK(B::plan(catalog.library.programs[0],0,inputs,3,{1,1,1},{64,1,1},b.parentGeneration,1,batch));
 std::array<B::Bytes,4>snapshots;for(unsigned i=0;i<batch.resources;++i)snapshots[i].resize(batch.resource[i].bytes,uint8_t(13+i));B::Bytes request;CHECK(B::assemble(batch,snapshots,request));
 CHECK(client1.execute(request.data(),request.size(),image.data()+640,4608,frame)&&exchange(peer1,client1,frame,result,completion));CHECK(completion==1&&client1.nativeSerial()==1&&core.completed()==1);
 CHECK(client2.hello(frame)&&exchange(peer2,client2,frame,result,completion));CHECK(client2.session()==2&&client2.completed()==0&&client2.nativeSerial()==1);
 A::Client app{333,501};A::Plan admitted;CHECK(A::prepare(b,ready,app,admitted));
 A::Peer connection;connection.phase=unsigned(client2.phase());connection.generation=client2.generation();connection.session=client2.session();connection.completed=client2.completed();connection.nativeSerial=client2.nativeSerial();connection.serverPID=client2.serverPID();connection.processID=app.pid;connection.uid=app.uid;connection.exchanges=1;
 CHECK(A::connect(admitted,connection,app,b,true));CHECK(connection.session!=b.session&&connection.nativeSerial!=0);
 {auto wrong=connection;wrong.serverPID++;CHECK(!A::connect(admitted,wrong,app,b,true));}
 CHECK(client2.close(frame)&&exchange(peer2,client2,frame,result,completion));CHECK(client1.close(frame)&&exchange(peer1,client1,frame,result,completion));core.stop(backend);
 CHECK(backend.claims==1&&backend.executions==1&&backend.retirements==1);
 std::printf("{\"passed\":true,\"checks\":%u,\"cpu_model_jobs\":1,\"actual_xpc\":false,\"gpu_jobs\":0,\"peer_session\":2,\"publication_session\":77,\"native_serial_floor\":1}\n",checks);
}
