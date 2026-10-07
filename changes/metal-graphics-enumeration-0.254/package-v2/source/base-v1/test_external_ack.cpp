#include "changes/gsp-external-vas-0.27/ExternalVAS.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <initializer_list>
namespace X=ExternalVAS;
using Bytes=std::vector<unsigned char>;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %u: %s\n",unsigned(__LINE__),#x);return 1;}}while(0)
static Bytes load(const char *name){std::ifstream f(name,std::ios::binary);return Bytes(std::istreambuf_iterator<char>(f),{});}
static void fix(Bytes &p){X::put32(p.data()+32,0);X::put32(p.data()+32,X::checksum(p.data(),48+X::get32(p.data()+56)));}
int main(int argc,char **argv){
 CHECK(argc==3);const auto actual=load(argv[1]),requests=load(argv[2]);CHECK(actual.size()==20480&&requests.size()==20480);
 X::Journal journal;journal.nextSequence=21;unsigned char canonical[4096];
 for(unsigned step=0;step<5;++step){
  CHECK(X::request(step,canonical,4096));CHECK(!std::memcmp(canonical,requests.data()+step*4096,4096));
  CHECK(X::consume(journal,actual.data()+step*4096,4096));
 }
 CHECK(journal.next==5&&journal.nextSequence==26&&journal.vas.accepted);
 Bytes ack(actual.begin()+16384,actual.end());X::Reply reply;
 CHECK(X::get32(ack.data()+56)==32&&X::get32(ack.data()+60)==54);
 CHECK(X::reply(4,25,ack.data(),4096,reply)&&reply.accepted);
 for(unsigned off:{80U,84U,96U,108U,127U,128U,4095U}){auto bad=ack;bad[off]^=0x5a;CHECK(X::reply(4,25,bad.data(),4096,reply));}
 for(unsigned off:{0U,32U,36U,40U,44U,48U,52U,60U,64U,68U,72U,76U}){
  auto bad=ack;bad[off]^=1;if(off!=32)fix(bad);CHECK(!X::reply(4,25,bad.data(),4096,reply)&&!reply.accepted);
 }
 for(unsigned length:{0U,31U,33U,36U,80U,4096U}){
  auto bad=ack;X::put32(bad.data()+56,length);if(length<=80)fix(bad);
  CHECK(!X::reply(4,25,bad.data(),4096,reply));
 }
 for(unsigned step=0;step<4;++step)CHECK(!X::reply(step,25,ack.data(),4096,reply));
 CHECK(!X::reply(4,26,ack.data(),4096,reply));CHECK(!X::consume(journal,ack.data(),4096));
 for(unsigned size:{0U,79U,80U,4095U,4097U})CHECK(!X::reply(4,25,ack.data(),size,reply));
 std::printf("{\"passed\":true,\"checks\":%u,\"actual_replies_replayed\":5,\"directory_reply_payload_bytes\":0,\"hardware_accessed\":false,\"native_post_ack_directory_check_was_not_run_in_old_capture\":true,\"compute_verified\":false,\"metal_verified\":false}\n",checks);
}
