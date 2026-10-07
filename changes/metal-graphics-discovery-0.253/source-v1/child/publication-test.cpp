#include "RTXPublication200.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <atomic>
using namespace RTXPublication200;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::abort();}}while(0)
struct Sink {
 unsigned mask=0,writes=0,hides=0,failAt=5;
 bool publish(const Request&){for(unsigned i=0;i<5;++i){if(i==failAt)return false;mask|=1U<<i;++writes;}return true;}
 void hide(){mask=0;++hides;}
};
static Request request(){return {0x100000002ULL,0x100000001ULL,4,9,Operation::Publish};}
static Observation observation(){Observation o;o.child=request().child;o.parent=request().parent;o.epoch=4;o.eligible=o.providerOpen=o.programReady=o.hostFence=true;o.root={242,2,181,183,true,true,true,242,true,false};return o;}
static void invalidate(Observation&o,unsigned which){
 switch(which){case 0:++o.child;break;case 1:++o.parent;break;case 2:o.eligible=false;break;case 3:o.providerOpen=false;break;case 4:o.programReady=false;break;case 5:o.hostFence=false;break;case 6:o.epoch=0;break;case 7:++o.epoch;break;case 8:o.root.rootABI=170;break;case 9:o.root.hostABI=1;break;case 10:o.root.dataABI=0;break;case 11:o.root.dispatchABI=0;break;case 12:o.root.acknowledged=false;break;case 13:o.root.runtimeEnabled=false;break;case 14:o.root.dispatchActive=false;break;case 15:o.root.graphicsABI=0;break;case 16:o.root.graphicsActive=false;break;case 17:o.root.graphicsRetained=true;break;}
}
int main(){
 const auto r=request();auto withdraw=r;withdraw.operation=Operation::Withdraw;const auto good=observation();
 // Independent wire vector, including operation and reserved bytes.
 uint8_t vector[Bytes]={'R','T','X','P',3,0,1,0,2,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,4,0,0,0,0,0,0,0,9};
 uint8_t raw[Bytes];CHECK(encode(r,raw,sizeof(raw)));CHECK(std::memcmp(raw,vector,Bytes)==0);
 Request q;CHECK(decode(vector,Bytes,q)&&sameIdentity(q,r)&&q.operation==Operation::Publish);
 CHECK(encode(withdraw,raw,Bytes));CHECK(raw[6]==2&&decode(raw,Bytes,q)&&q.operation==Operation::Withdraw);
 for(size_t n=0;n<Bytes+2;++n){CHECK(decode(vector,n,q)==(n==Bytes));CHECK(encode(r,raw,n)==(n==Bytes));}
 CHECK(!decode(nullptr,Bytes,q)&&!encode(r,nullptr,Bytes));
 for(unsigned i=0;i<Bytes;++i)for(unsigned bit=0;bit<8;++bit){
  std::memcpy(raw,vector,Bytes);raw[i]^=uint8_t(1U<<bit);
  if(i<8||i>=40){CHECK(!decode(raw,Bytes,q));CHECK(q.child==0&&q.parent==0&&q.epoch==0&&q.session==0);}
  else if(decode(raw,Bytes,q))CHECK(!sameIdentity(q,r));
 }
 for(unsigned i=8;i<40;i+=8){std::memcpy(raw,vector,Bytes);std::memset(raw+i,0,8);CHECK(!decode(raw,Bytes,q));}
 for(auto member:{&Request::child,&Request::parent,&Request::epoch,&Request::session}){auto v=r;v.*member=uint64_t(1)<<63;CHECK(!encode(v,raw,Bytes));}
 for(unsigned bad:{0U,3U,255U}){auto v=r;v.operation=Operation(bad);CHECK(!encode(v,raw,Bytes));Controller c;CHECK(c.begin(true,v,good)==Error::Encoding);}
 // Every readiness field must hold at admission AND after unlocked preparation.
 for(unsigned i=0;i<18;++i){
  auto bad=good;invalidate(bad,i);Controller c;Sink s;
  CHECK(c.begin(true,r,bad)!=Error::Ok);CHECK(c.phase()==Phase::Cold&&s.mask==0);
  CHECK(c.begin(false,r,good)==Error::Privilege);CHECK(c.begin(true,r,good)==Error::Ok);
  CHECK(c.withdraw(s,true,withdraw)==Error::Busy);CHECK(c.phase()==Phase::Publishing&&s.mask==0);
  CHECK(!c.commit(s,true,true,bad));CHECK(c.phase()==Phase::Failed&&s.mask==0&&s.writes==0);
  CHECK(c.withdraw(s,true,withdraw)==Error::Ok);CHECK(c.phase()==Phase::Withdrawn);
  CHECK(!c.commit(s,true,true,good));CHECK(c.begin(true,r,good)==Error::Busy);
 }
 // Failure at each property write must remove every partial property.
 for(unsigned i=0;i<=5;++i){
  Controller c;Sink s;s.failAt=i;CHECK(c.begin(true,r,good)==Error::Ok);
  CHECK(c.commit(s,true,true,good)==(i==5));CHECK(s.mask==(i==5?31U:0U));
  CHECK(c.withdraw(s,false,withdraw)==Error::Privilege);
  for(auto member:{&Request::child,&Request::parent,&Request::epoch,&Request::session}){
   auto other=withdraw;++(other.*member);if(other.child==other.parent)++other.child;
   CHECK(c.withdraw(s,true,other)==Error::Identity);CHECK(s.mask==(i==5?31U:0U));
  }
  CHECK(c.withdraw(s,true,withdraw)==Error::Ok);CHECK(s.mask==0&&c.phase()==Phase::Withdrawn);
  auto hides=s.hides;CHECK(c.withdraw(s,true,withdraw)==Error::Ok&&s.hides==hides);
  CHECK(c.begin(true,r,good)==Error::Busy);CHECK(!c.commit(s,true,true,good));CHECK(s.mask==0);
 }
 for(unsigned bits=0;bits<4;++bits){Controller c;Sink s;CHECK(c.begin(true,r,good)==Error::Ok);CHECK(c.commit(s,bits&1,bits&2,good)==(bits==3));CHECK(s.mask==(bits==3?31U:0U));}
 // Regression: a delayed, previously admitted writer after stop().
 for(unsigned at=0;at<3;++at){
  Controller c;Sink s;if(at>0)CHECK(c.begin(true,r,good)==Error::Ok);
  if(at>1)CHECK(c.commit(s,true,true,good));c.stop(s);auto writes=s.writes;
  CHECK(!c.commit(s,true,true,good));CHECK(c.begin(true,r,good)==Error::Stopped);
  CHECK(c.withdraw(s,true,withdraw)==Error::Stopped);CHECK(s.mask==0&&s.writes==writes&&c.phase()==Phase::Stopped);
 }
 {Controller c;Sink s;CHECK(c.withdraw(s,true,withdraw)==Error::NotReady);CHECK(s.hides==0);}
 // Same external lock discipline as the kernel adapter.
 constexpr unsigned races=256;
 for(unsigned i=0;i<races;++i){
  Controller c;Sink s;std::mutex mutex;std::atomic<bool> go{false};CHECK(c.begin(true,r,good)==Error::Ok);
  auto wait=[&](){while(!go.load(std::memory_order_acquire))std::this_thread::yield();};
  std::thread writer([&](){wait();std::lock_guard<std::mutex> lock(mutex);c.commit(s,true,true,good);});
  std::thread closer([&](){wait();std::lock_guard<std::mutex> lock(mutex);c.withdraw(s,true,withdraw);});
  std::thread stopper([&](){wait();std::lock_guard<std::mutex> lock(mutex);c.stop(s);});
  go.store(true,std::memory_order_release);writer.join();closer.join();stopper.join();
  CHECK(c.phase()==Phase::Stopped&&s.mask==0);
 }
 std::printf("{\"passed\":true,\"checks\":%u,\"concurrent_races\":%u,\"gpu_jobs\":0,\"kernel_publication\":false}\n",checks,races);
}
