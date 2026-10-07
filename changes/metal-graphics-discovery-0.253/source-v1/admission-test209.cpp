#include "app/ApplicationAdmission209.hpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
using namespace RTXApplication209;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::abort();}}while(0)
int main(int argc,char**argv){
 CHECK(argc==2);auto*f=std::fopen(argv[1],"rb");CHECK(f);unsigned char memory[128];CHECK(std::fread(memory,1,128,f)==128&&std::fgetc(f)==EOF);std::fclose(f);
 RTXAccelerator103::Binding b;CHECK(RTXMemory107::decode(memory,128,b.memory));b.childRegistry=101;b.parentGeneration=b.memory.generation;b.epoch=7;b.session=19;
 Ready r;r.abi=209;r.protocol=208;r.allowedUID=501;r.rootPID=1001;r.child=b.childRegistry;r.generation=b.parentGeneration;r.epoch=b.epoch;r.publicationSession=b.session;
 r.rootABI=195;r.hostABI=2;r.dataABI=181;r.dispatchABI=183;r.publicationABI=2;r.bootMatches=r.catalogMatches=r.memoryMatches=true;
 Client c{2002,501};Plan p;CHECK(prepare(b,r,c,p));
 for(auto member:{&Ready::abi,&Ready::protocol,&Ready::allowedUID,&Ready::rootPID,&Ready::child,&Ready::generation,&Ready::epoch,&Ready::publicationSession,&Ready::rootABI,&Ready::hostABI,&Ready::dataABI,&Ready::dispatchABI,&Ready::publicationABI}){
  auto bad=r;bad.*member=0;Plan out=p;CHECK(!prepare(b,bad,c,out));CHECK(out.rootPID==0&&out.binding.childRegistry==0&&out.client.pid==0);
 }
 for(auto member:{&Ready::bootMatches,&Ready::catalogMatches,&Ready::memoryMatches}){auto bad=r;bad.*member=false;Plan out;CHECK(!prepare(b,bad,c,out));}
 {auto bad=r;bad.rootPID=c.pid;Plan out;CHECK(!prepare(b,bad,c,out));}
 for(Client bad:std::initializer_list<Client>{{0,501},{2002,0},{2002,502},{0x80000000ULL,501}}){Plan out;CHECK(!prepare(b,r,bad,out));}
 Peer peer;peer.phase=3;peer.generation=b.parentGeneration;peer.session=1;peer.serverPID=r.rootPID;peer.processID=c.pid;peer.uid=c.uid;peer.exchanges=1;
 CHECK(connect(p,peer,c,b,true));
 for(uint64_t session:{1ULL,19ULL,23ULL})for(uint64_t native:{0ULL,1ULL,65ULL,0xffffffffffffffffULL}){
  auto now=peer;now.session=session;now.nativeSerial=native;CHECK(connect(p,now,c,b,true));
 }
 for(auto member:{&Peer::phase,&Peer::generation,&Peer::session,&Peer::serverPID,&Peer::processID,&Peer::uid,&Peer::exchanges}){auto bad=peer;bad.*member=0;CHECK(!connect(p,bad,c,b,true));}
 {auto bad=peer;bad.completed=1;CHECK(!connect(p,bad,c,b,true));}
 {auto bad=peer;bad.phase=2;CHECK(!connect(p,bad,c,b,true));}
 {auto bad=peer;bad.serverPID++;CHECK(!connect(p,bad,c,b,true));}
 {auto bad=peer;bad.exchanges++;CHECK(!connect(p,bad,c,b,true));}
 for(auto member:{&RTXAccelerator103::Binding::childRegistry,&RTXAccelerator103::Binding::parentGeneration,&RTXAccelerator103::Binding::epoch,&RTXAccelerator103::Binding::session}){auto changed=b;++(changed.*member);CHECK(!connect(p,peer,c,changed,true));}
 {auto changed=b;changed.memory.reportedBytes++;CHECK(!connect(p,peer,c,changed,true));}
 CHECK(!connect(p,peer,c,b,false));CHECK(!connect(p,peer,{c.pid+1,c.uid},b,true));CHECK(!connect(p,peer,{c.pid,0},b,true));
 std::printf("{\"passed\":true,\"checks\":%u,\"actual_xpc\":false,\"actual_iokit\":false,\"gpu_jobs\":0}\n",checks);
}
