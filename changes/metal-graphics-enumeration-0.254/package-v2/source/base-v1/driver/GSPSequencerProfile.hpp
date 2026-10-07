#pragma once
#include "GSPContentSeal.hpp"

// Initial GA106/570.144 request observed on this exact board. Validate the
// complete request before any write, including all addresses, DMA geometry,
// values and core-transition order. No client-selectable MMIO is accepted.
namespace GSPSequencer {
using U64=unsigned long long;
constexpr unsigned CapacityWords=16354,UsedWords=1564,OperationCount=420;
constexpr unsigned PayloadBytes=40+UsedWords*4;
constexpr U64 WorkspaceStart=0x173c40000ULL,WorkspaceEnd=0x173e00000ULL;
constexpr U64 SourceStart=WorkspaceStart+0x100,SourceEnd=WorkspaceStart+0x6500;
constexpr unsigned ImemBlocks=64,DmemBlocks=36;
enum Op : unsigned { Write=0,Modify,Poll,Delay,Store,CoreReset,CoreStart,CoreWait,CoreResume };
struct Operation { unsigned op=0,index=0,word=0,count=0,args[5]={}; };
struct Profile {
  bool passed=false;unsigned operations=0,words=0,failedWord=~0U;
  unsigned imemBlocks=0,dmemBlocks=0;
};
constexpr unsigned Arity[9]={2,3,5,1,2,0,0,0,0};
// The sink makes the same bounded canonical program usable by an independent
// comparison, a test encoder and the executor. It never reads a GPU register.
template<class Sink> bool program(Sink &s){
  if(!s.poll(0x110040,0x80000000U,0x80000000U,0,3) || !s.write(0x110040,0) ||
     !s.core(CoreReset) || !s.write(0x110600,0x114) || !s.poll(0x110118,1,0,0,2))return false;
  for(unsigned segment=0;segment<2;++segment){
    if(!s.write(0x110110,segment?0x173c441:0x173c400) || !s.write(0x110128,0))return false;
    const unsigned blocks=segment?DmemBlocks:ImemBlocks;
    for(unsigned i=0;i<blocks;++i){
      if(!s.poll(0x110118,1,0,0,2) || !s.write(0x110114,i*256) ||
         !s.write(0x11011c,(i+(segment?0:1))*256) || !s.write(0x110118,segment?0x600:0x614))return false;
    }
    if(!s.poll(0x110118,2,2,0,2))return false;
  }
  return s.write(0x111210,0x1f10) && s.write(0x11119c,0x400) && s.write(0x111198,1) &&
    s.write(0x111180,1) && s.write(0x110040,0xfe) && s.write(0x110104,0x100) &&
    s.core(CoreStart) && s.core(CoreWait) && s.core(CoreResume);
}
struct Compare {
  const unsigned char *p;Profile &r;
  bool word(unsigned v){
    if(r.words>=UsedWords || GSPContentSeal::get32(p+40+4*r.words)!=v){r.failedWord=r.words;return false;}
    ++r.words;return true;
  }
  bool write(unsigned a,unsigned v){++r.operations;return word(Write)&&word(a)&&word(v);}
  bool poll(unsigned a,unsigned m,unsigned v,unsigned t,unsigned e){
    ++r.operations;return word(Poll)&&word(a)&&word(m)&&word(v)&&word(t)&&word(e);
  }
  bool core(unsigned op){++r.operations;return word(op);}
};
inline bool profile(const unsigned char *payload,unsigned bytes,Profile &r){
  r=Profile{};
  if(!payload || bytes!=PayloadBytes || GSPContentSeal::get32(payload)!=CapacityWords ||
     GSPContentSeal::get32(payload+4)!=UsedWords)return false;
  for(unsigned i=8;i<40;++i)if(payload[i])return false;
  Compare compare{payload,r};
  r.passed=program(compare) && r.words==UsedWords && r.operations==OperationCount;
  if(r.passed){r.imemBlocks=ImemBlocks;r.dmemBlocks=DmemBlocks;}
  return r.passed;
}
// Decode only within a previously validated immutable payload. Rechecked here
// so a caller cannot turn malformed spans into an out-of-bounds read.
inline bool next(const unsigned char *p,unsigned bytes,unsigned &cursor,unsigned index,Operation &op){
  if(!p || bytes!=PayloadBytes || cursor>=UsedWords)return false;
  op=Operation{};op.op=GSPContentSeal::get32(p+40+cursor*4);op.word=cursor;op.index=index;
  if(op.op>CoreResume)return false;
  op.count=Arity[op.op];
  if(op.count>=UsedWords-cursor)return false;
  ++cursor;
  for(unsigned i=0;i<op.count;++i)op.args[i]=GSPContentSeal::get32(p+40+4*cursor++);
  return true;
}
}
