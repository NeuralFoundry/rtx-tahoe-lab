#include "RuntimeTestFixture.hpp"
// Only a CPU fixture; uses the actual portable Runtime and dispatch core.
struct Interleaved : Backend {
 unsigned variant=0,pollRead=0;bool pending=false;
 Interleaved(Fixtures &f,unsigned value):Backend(f),variant(value){}
 bool start(){R::Runtime<Interleaved> r(*this,state);auto m=storage();if(!r.prepare(0x30603501,17,m))return false;bootstrapRestore();return r.open();}
 N::Failure run(uint64_t serial){R::Runtime<Interleaved> io(*this,state);auto r=request(serial);std::array<uint8_t,N::WireBytes> wire{};CHECK(N::encode(r,f.lib,wire.data(),wire.size()));return io.submit(17,wire.data(),wire.size());}
 bool notify(){
  if(variant==0)return Backend::notify();
  if(variant==5||variant==7){const bool ok=Backend::notify();N::P::Q::put64(device.data()+B::Fence,variant==5?0:state.core.request().serial+1);return ok;}
  CHECK(owned&&runtime&&window==0&&state.backing.phase()==B::Phase::Published);++notifications;pending=true;return op();
 }
 bool readMemory(unsigned a,uint8_t *out,unsigned n){
  const bool ok=Backend::readMemory(a,out,n);
  if(ok&&pending&&n==8&&(a==N::PutPhysical-4||a==N::FencePhysical||a==N::FencePhysical+16)){
   ++pollRead;
   if(variant!=6&&pollRead==variant){pending=false;--notifications;CHECK(Backend::notify());}
  }
  return ok;
 }
};
