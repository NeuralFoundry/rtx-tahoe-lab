#pragma once
#include "GSPPageTablesProtocol.hpp"
namespace GSPPageTablesSnapshot {
// Captures only the three leased table pages after the reserved-PDE control.
// Observed changes are evidence; this does not assert a GPU address translation.
struct Result{bool attempted=false,passed=false;unsigned words=0,bytes=0,failure=0;unsigned long long elapsedNs=0;};
template<class IO>void capture(IO&io,unsigned char*out,Result&r){
  r=Result{};if(!out||!io.ready()){r.failure=1;return;}
  const auto started=io.nowNs();r.attempted=true;
  for(unsigned i=0;i<GSPPageTables::Words;++i){
    const auto now=io.nowNs();
    if(now<started||now-started<r.elapsedNs){r.failure=2;return;}
    r.elapsedNs=now-started;
    if(r.elapsedNs>=GSPPageTables::BudgetNs||!io.ready()){r.failure=3;return;}
    unsigned value=0;if(!io.read(GSPPageTables::Start+i*4,value)){r.failure=4;return;}
    GSPPageTables::put32(out+i*4,value);r.bytes+=4;
    if(value==~0U||(value>>16)==0xbad0U||(value>>16)==0xbadfU){r.failure=5;return;}
    ++r.words;
  }
  r.passed=true;
}
}
