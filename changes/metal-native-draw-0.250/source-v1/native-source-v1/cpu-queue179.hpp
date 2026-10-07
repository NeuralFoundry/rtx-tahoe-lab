#pragma once
#include "probe/kernel/root170/OwnedRootTransition170.hpp"
#include "probe/kernel/root170/RootQueueGuard170.hpp"
#include <array>
#include <cstring>
#include <vector>
namespace CPUQueue179 {namespace T=RTXRootTransition170;namespace R=GSPComputePrep;
static RTXTreeBacking168::Info table(){RTXTreeBacking168::Info s;s.phase=RTXTreeBacking168::Phase::Exposed;s.root=0x100000000ULL;s.tables=s.written=13;s.bytes=13*4096;s.mappings=3491;s.backingStarted=true;return s;}
static R::Result prior(unsigned rx=61,unsigned seq=80){R::Result r;r.passed=r.validated=r.attempted=r.prefixConsumed=true;
 r.sent=r.completed=r.doorbells=r.count=r.pages=ExecutionCodec::Steps;r.bytes=ExecutionCodec::Steps*4096;r.txWriter=r.txReader=ExecutionCodec::FinalProducer;r.rxReader=r.rxProducer=rx;r.rxSequence=seq;return r;}
static std::vector<uint8_t> packet(unsigned seq,unsigned fn,unsigned payload=0,unsigned result=0){
 unsigned bytes=(80+payload+4095)&~4095U;std::vector<uint8_t>p(bytes);auto put=[&](unsigned off,unsigned v){R::put32(p.data()+off,v);};
 put(36,seq);put(40,bytes/4096);put(48,0x3000000);put(52,0x43505256);put(56,32+payload);put(60,fn);put(64,result);
 if(fn==0x100c)put(84,payload-8);put(32,ExternalVAS::checksum(p.data(),(80+payload+7)&~7U));return p;
}
struct Fake {
 RTXTreeBacking168::Info t=table();R::Result p=prior();RTXRootQueue170::Guard guard;
 std::vector<uint8_t>queue=std::vector<uint8_t>(0x81000),expect=std::vector<uint8_t>(4096);
 unsigned calls=0,failAt=0,bells=0,async=0,mode=0,rxReads=0;uint64_t clock=1000;bool copyThenFail=false,failReady=false,regress=false;
 explicit Fake(unsigned a=0,unsigned m=0,unsigned rx=61,unsigned seq=80):p(prior(rx,seq)),async(a),mode(m){
  for(unsigned off:{0x1000U,0x41000U}){unsigned h[8]={0,0x40000,4096,63,off==0x1000?p.txWriter:rx,1,32,4096};for(unsigned i=0;i<8;++i)R::put32(queue.data()+off+i*4,h[i]);}
  R::put32(queue.data()+0x1020,rx);R::put32(queue.data()+0x41020,p.txReader);
 }
 bool op(){return ++calls!=failAt;}
 bool ready(){return !guard.failed&&(!failReady||calls<failAt);}
 uint64_t nowNs(){if(regress&&calls>5)return 0;return clock+=1000;}
 void delayUs(unsigned n){clock+=uint64_t(n)*1000;}
 bool claim(){return op()&&guard.claim(p.rxReader);}
 bool import(){return op();}bool publish(){return op();}
 bool read(unsigned off,uint8_t*out,unsigned n){if(!op())return false;if(off>queue.size()||n>queue.size()-off)return false;std::memcpy(out,queue.data()+off,n);
  if(mode==7&&off>=0x42000&&guard.bellWritten&&n==4096&&++rxReads%2==0)out[200]^=1;return true;}
 bool write(unsigned off,const uint8_t*d,unsigned n){
  if(!guard.beforeWrite(off,d,n,t,expect.data()))return false;bool good=op();
  if(good||copyThenFail)std::memcpy(queue.data()+off,d,n);return guard.afterWrite(off,good);
 }
 bool doorbell(){
  if(!guard.beforeBell())return false;bool good=op();++bells;
  if(good||copyThenFail){
   unsigned cursor=p.rxReader,seq=p.rxSequence;
   auto add=[&](std::vector<uint8_t>b){for(size_t i=0;i<b.size();i+=4096){std::memcpy(queue.data()+0x42000+cursor*4096,b.data()+i,4096);cursor=(cursor+1)%63;}};
   for(unsigned i=0;i<async;++i)add(packet(seq++,mode==3?0x1020:0x100c,mode==3?1212:mode==6?5000:8));
   if(mode!=1){auto b=packet(mode==2?32:seq,mode==4?55:54,0,mode==5?1:0);if(mode==9)R::put32(b.data()+40,17);add(b);}
   R::put32(queue.data()+0x41010,cursor);R::put32(queue.data()+0x41020,mode==8?p.txWriter:p.txWriter+1);
  }
  return guard.afterBell(good);
 }
};
struct Storage {std::array<uint8_t,4096>request{},scratch{};std::vector<uint8_t>records=std::vector<uint8_t>(R::MaxBytes);T::Result result;};
}
