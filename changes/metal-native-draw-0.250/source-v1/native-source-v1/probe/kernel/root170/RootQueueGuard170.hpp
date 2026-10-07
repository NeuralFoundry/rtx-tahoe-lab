#pragma once
#include "OwnedRootRpc169.hpp"
#include "../changes/gsp-submit-0.24/transactions/ExecutionCodec.hpp"
namespace RTXRootQueue170 {
constexpr unsigned Producer=ExecutionCodec::FinalProducer;
constexpr unsigned RequestOffset=0x2000+Producer*4096;
static_assert(Producer<62,"Root RPC must fit the fixed queue without wrapping");
// Records attempts before external writes; an uncertain write is never retried.
struct Guard {
 bool claimed=false,requestAttempted=false,requestWritten=false,producerAttempted=false,producerWritten=false;
 bool bellAttempted=false,bellWritten=false,failed=false;
 unsigned consumer=0,consumerWrites=0,consumerPages=0;
 bool claim(unsigned rx){if(claimed||failed||rx>=63)return false;claimed=true;consumer=rx;return true;}
 bool beforeWrite(unsigned off,const uint8_t*data,unsigned bytes,const RTXTreeBacking168::Info&t,uint8_t*expected){
  if(!claimed||failed||!data)return false;
  if(off==RequestOffset){ // Fixed slot immediately after the execution RPCs.
   if(bytes!=4096||requestAttempted||producerAttempted||bellAttempted||!expected||
    !RTXRootRpc169::request(t,Producer,expected,4096))return false;
   for(unsigned i=0;i<4096;++i)if(data[i]!=expected[i])return false;
   requestAttempted=true;return true;
  }
  if(off==0x1010){
   if(bytes!=4||!requestWritten||producerAttempted||bellAttempted||ExternalVAS::get32(data)!=Producer+1)return false;
   producerAttempted=true;return true;
  }
  if(off==0x1020){
   if(bytes!=4||!bellWritten||consumerWrites>=16)return false;
   const unsigned next=ExternalVAS::get32(data),pages=(next+63-consumer)%63;
   if(next>=63||!pages||pages>16||consumerPages+pages>32)return false;
   consumer=next;++consumerWrites;consumerPages+=pages;return true;
  }
  return false;
 }
 bool afterWrite(unsigned off,bool ok){
  if(!ok){failed=true;return false;}
  if(off==RequestOffset)requestWritten=true;else if(off==0x1010)producerWritten=true;
  return true;
 }
 bool beforeBell(){if(!claimed||failed||!producerWritten||bellAttempted)return false;bellAttempted=true;return true;}
 bool afterBell(bool ok){if(!ok){failed=true;return false;}bellWritten=true;return true;}
};
}
