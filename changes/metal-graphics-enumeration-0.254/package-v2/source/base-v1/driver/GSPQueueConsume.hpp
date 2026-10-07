#pragma once
#include "GSPFirstStatus.hpp"
namespace GSPQueueConsume {
struct Result {
  unsigned failure=0,readerBefore=~0U,readerAfter=~0U,producer=~0U;
  bool attempted=false,written=false,verified=false;
};
// First two captured/validated records occupy slots0..2. This operation only
// publishes consumer3 after the caller has validated the complete sequencer
// profile. No caller-supplied pointer, index, payload or queue write exists.
template<class IO> bool consume(IO &io,Result &r){
  r=Result{};unsigned char scratch[32]={};unsigned h[8]={};
  if(!io.ready() || !io.profileValidated()){r.failure=1;return false;}
  if(!io.import() || !io.read(0x1020,scratch,4)){r.failure=2;return false;}
  r.readerBefore=GSPContentSeal::get32(scratch);
  if(r.readerBefore || !io.read(0x41000,scratch,32)){r.failure=3;return false;}
  for(unsigned i=0;i<8;++i)h[i]=GSPContentSeal::get32(scratch+4*i);
  r.producer=h[4];
  if(!GSPFirstStatus::header(h) || r.producer<3){r.failure=4;return false;}
  if(!io.ready() || !io.claimConsumption()){r.failure=5;return false;}
  r.attempted=true;
  if(!io.writeConsumerThree()){r.failure=6;return false;}
  r.written=true;
  if(!io.publish() || !io.import() || !io.read(0x1020,scratch,4)){r.failure=7;return false;}
  r.readerAfter=GSPContentSeal::get32(scratch);
  r.verified=io.ready() && r.readerAfter==3;
  if(!r.verified)r.failure=8;
  return r.verified;
}
}
