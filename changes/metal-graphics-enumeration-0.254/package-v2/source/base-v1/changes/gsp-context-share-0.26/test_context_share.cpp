#include "ContextSharePlan.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <initializer_list>
namespace P=ContextSharePlan;
using Op=P::Operation;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(2);}}while(0)
static std::vector<unsigned char> load(const char *path){
 FILE *f=std::fopen(path,"rb");CHECK(f);CHECK(std::fseek(f,0,SEEK_END)==0);const long n=std::ftell(f);CHECK(n>0&&n<100000);
 CHECK(std::fseek(f,0,SEEK_SET)==0);std::vector<unsigned char> b(static_cast<size_t>(n));CHECK(std::fread(b.data(),1,b.size(),f)==b.size());CHECK(std::fclose(f)==0);return b;
}
static void save(const char *path,const std::vector<unsigned char> &b){
 FILE *f=std::fopen(path,"wb");CHECK(f);CHECK(std::fwrite(b.data(),1,b.size(),f)==b.size());CHECK(std::fclose(f)==0);
}
int main(int argc,char **argv){
 CHECK(argc==5);const auto oracle=load(argv[1]),actualRequests=load(argv[2]),actualRecords=load(argv[3]);
 CHECK(oracle.size()==402&&actualRequests.size()==40960&&actualRecords.size()==65536);
 std::vector<unsigned char> generated;unsigned offset=0;
 for(Op op:{Op::GroupAlloc,Op::ShareAlloc,Op::ChannelAlloc,Op::Schedule}){
  const unsigned n=P::bytes(op);unsigned char guarded[370];std::memset(guarded,0xa5,sizeof(guarded));
  CHECK(P::parameters(op,guarded+1,n));CHECK(guarded[0]==0xa5&&guarded[n+1]==0xa5);
  CHECK(std::memcmp(guarded+1,oracle.data()+offset,n)==0);
  generated.insert(generated.end(),guarded+1,guarded+1+n);offset+=n;
  for(unsigned shortSize=0;shortSize<n;++shortSize){
   unsigned char rejected[370];std::memset(rejected,0xa5,sizeof(rejected));CHECK(!P::parameters(op,rejected+1,shortSize));
   for(unsigned char c:rejected)CHECK(c==0xa5);
  }
  CHECK(!P::parameters(op,nullptr,n));
  P::ReplyParameters reply;CHECK(P::replyParameters(op,guarded+1,n,reply)&&reply.accepted);
  CHECK(!P::replyParameters(op,guarded+1,n-1,reply));CHECK(!P::replyParameters(op,guarded+1,n+1,reply));
  for(unsigned i=0;i<n;++i){
   if(op==Op::ShareAlloc&&i>=8)continue;
   if(op==Op::ChannelAlloc&&((i>=132&&i<140)||(i>=240&&i<244)))continue;
   guarded[i+1]^=0x80;CHECK(!P::replyParameters(op,guarded+1,n,reply)&&!reply.accepted);guarded[i+1]^=0x80;
  }
 }
 CHECK(offset==402);unsigned char channel[368];CHECK(P::parameters(Op::ChannelAlloc,channel,sizeof(channel)));
 // hContextShare replaces the direct hVASpace in the payload; parent handle
 // changes in its separate RM envelope. This is a proposal, not a live reply.
 for(unsigned i=0;i<368;++i){if(i<24||i>=32)CHECK(channel[i]==actualRequests[112+i]);}
 CHECK(P::get32(actualRequests.data()+84)==P::Device);CHECK(P::parent(Op::ChannelAlloc)==P::Group);
 CHECK(P::get32(actualRequests.data()+136)==0);CHECK(P::get32(channel+24)==P::Share);
 CHECK(P::get32(actualRecords.data()+244)==5);P::ReplyParameters reply;
 CHECK(!P::replyParameters(Op::ChannelAlloc,actualRecords.data()+112,368,reply));
 std::memcpy(channel,actualRecords.data()+112,368);P::put32(channel+24,P::Share);P::put32(channel+28,0);
 CHECK(P::replyParameters(Op::ChannelAlloc,channel,sizeof(channel),reply)&&reply.sessionCid==5);
 static_assert(P::HardwareChannelId==4,"CID is not the hardware channel");
 for(uint32_t cid:{0U,3U,0xffffffffU}){P::put32(channel+132,cid);CHECK(!P::replyParameters(Op::ChannelAlloc,channel,sizeof(channel),reply));}
 for(uint32_t cid:{4U,5U,4096U,0xfffffffeU}){P::put32(channel+132,cid);CHECK(P::replyParameters(Op::ChannelAlloc,channel,sizeof(channel),reply)&&reply.sessionCid==cid);}
 P::put32(channel+136,2);CHECK(!P::replyParameters(Op::ChannelAlloc,channel,sizeof(channel),reply));
 unsigned char share[12];CHECK(P::parameters(Op::ShareAlloc,share,sizeof(share)));
 for(uint32_t veid:{0U,1U,31U,63U}){P::put32(share+8,veid);CHECK(P::replyParameters(Op::ShareAlloc,share,sizeof(share),reply)&&reply.subcontext==veid);}
 for(uint32_t veid:{64U,0x80000001U,0xffffffffU}){P::put32(share+8,veid);CHECK(!P::replyParameters(Op::ShareAlloc,share,sizeof(share),reply));}
 const auto invalid=static_cast<Op>(99);CHECK(!P::parameters(invalid,channel,sizeof(channel)));CHECK(!P::replyParameters(invalid,channel,sizeof(channel),reply));
 const auto previous=reply;CHECK(!P::replyParameters(Op::ShareAlloc,reinterpret_cast<unsigned char*>(&reply),12,reply));
 CHECK(std::memcmp(&previous,&reply,sizeof(reply))==0);CHECK(!P::replyParameters(Op::ShareAlloc,nullptr,12,reply));
 CHECK(generated==oracle);save(argv[4],generated);
 std::printf("{\"passed\":true,\"checks\":%u,\"sdk_payloads_equal\":true,\"actual_channel_baseline_equal\":true,\"hardware_accessed\":false,\"native_integrated\":false,\"metal_verified\":false}\n",checks);
}
