#pragma once
#include "../gsp-compute-0.25/qmd3/QmdProfile.hpp"
// CPU serialization and capture validation only; no device or queue access.
namespace RtxBatch031 {
namespace Q=QmdProfile;
constexpr unsigned Jobs=4,Elements=64,CodeBytes=512,ImageBytes=24576;
constexpr unsigned CommandBytes=128,EntryBytes=32,SlotBytes=0x400;
constexpr unsigned DefaultCounts[Jobs]={1,32,61,64};
constexpr uint32_t DefaultSeeds[Jobs]={0x30603101,0x30603102,0x30603103,0x30603104};
constexpr uint64_t CommandVA=0x1020001040ULL,CommandPA=0x03402040ULL;
constexpr uint32_t completion(unsigned job){return 0x306031f0u+job;}
constexpr uint64_t commandVA(unsigned job){return CommandVA+uint64_t(job)*64;}
constexpr uint64_t entry(unsigned job){return commandVA(job)|(1ULL<<41)|(8ULL<<42);}
constexpr unsigned cbOffset(unsigned job){return 8192+job*SlotBytes;}
constexpr unsigned qmdOffset(unsigned job){return 12288+job*256;}
constexpr unsigned outOffset(unsigned job){return 16384+job*SlotBytes;}
constexpr unsigned fenceOffset(unsigned job){return 20480+job*256;}
inline uint32_t get32(const uint8_t *p){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(p[i])<<(i*8);return v;}
inline void inputs(uint32_t seed,uint32_t *a,uint32_t *b){
 constexpr uint32_t edgeA[]={0,UINT32_MAX,0x80000000,0x7fffffff,1,UINT32_MAX,0xaaaaaaaa,0x55555555};
 constexpr uint32_t edgeB[]={0,1,0x80000000,1,UINT32_MAX,UINT32_MAX,0x55555555,0xaaaaaaaa};
 for(unsigned i=0;i<Elements;++i){seed=seed*1664525u+1013904223u;a[i]=seed;seed=seed*1664525u+1013904223u;b[i]=seed;}
 for(unsigned i=0;i<8;++i){a[i]=edgeA[i];b[i]=edgeB[i];}
}
inline bool build(uint8_t *image,size_t imageBytes,uint8_t *commands,size_t commandBytes,
 uint8_t *entries,size_t entryBytes,const uint8_t *code,size_t codeBytes,
 const uint32_t *seeds,const unsigned *counts){
 if(imageBytes!=ImageBytes||commandBytes!=CommandBytes||entryBytes!=EntryBytes||codeBytes!=CodeBytes||!seeds||!counts)return false;
 const struct {const void *p;size_t n;} outs[]={{image,imageBytes},{commands,commandBytes},{entries,entryBytes}},
 ins[]={{code,codeBytes},{seeds,Jobs*sizeof(uint32_t)},{counts,Jobs*sizeof(unsigned)}};
 for(unsigned i=0;i<3;++i){
  for(unsigned j=i+1;j<3;++j)if(!Q::separate(outs[i].p,outs[i].n,outs[j].p,outs[j].n))return false;
  for(unsigned j=0;j<3;++j)if(!Q::separate(outs[i].p,outs[i].n,ins[j].p,ins[j].n))return false;
 }
 for(unsigned j=0;j<Jobs;++j)if(counts[j]>Elements)return false;
 // All external input checks precede mutation. The following schema fields are fixed.
 for(unsigned i=0;i<ImageBytes;++i)image[i]=0;
 for(unsigned i=0;i<CodeBytes;++i)image[i]=code[i];
 if(!Q::build(image+12288,256,image+8192,4096,commands,32))return false;
 for(unsigned i=0;i<4096;++i){image[16384+i]=uint8_t(0xa5^((i*13+7)&255));image[20480+i]=uint8_t(0x5a^((i*13+7)&255));}
 uint32_t a[Elements],b[Elements];
 for(unsigned j=0;j<Jobs;++j){
  auto *qmd=image+qmdOffset(j);auto *cb=image+cbOffset(j);
  if(j)for(unsigned i=0;i<256;++i)qmd[i]=image[12288+i];
  const uint64_t cbVA=Q::ConstantVA+j*SlotBytes,fenceVA=Q::FenceVA+j*256;
  const struct {Q::B::Field f;uint32_t v;} fields[]={
   {{384,32},2},{{592,16},32},{{648,9},12},{{1641,9},CodeBytes/256},
   {{768,32},uint32_t(fenceVA)},{{800,8},uint32_t(fenceVA>>32)},{{832,32},completion(j)},
   {{1024,32},uint32_t(cbVA)},{{1056,17},uint32_t(cbVA>>32)},{{1075,13},384/16}
  };
  for(const auto &f:fields)if(!Q::B::put(qmd,256,f.f,f.v))return false;
  Q::put64(cb+0x28,0xfffdc0);Q::put64(cb+0x160,Q::OutputVA+j*SlotBytes);
  Q::put64(cb+0x168,cbVA+0x200);Q::put64(cb+0x170,cbVA+0x300);Q::put32(cb+0x178,counts[j]);
  inputs(seeds[j],a,b);
  for(unsigned i=0;i<Elements;++i){Q::put32(cb+0x200+i*4,a[i]);Q::put32(cb+0x300+i*4,b[i]);Q::put32(image+outOffset(j)+i*4,~(a[i]+b[i]));}
  Q::put32(image+fenceOffset(j),0);
  if(j)for(unsigned i=0;i<32;++i)commands[j*32+i]=commands[i];
  Q::put32(commands+j*32+20,uint32_t((Q::QmdVA+j*256)>>8));Q::put64(entries+j*8,entry(j));
 }
 return true;
}
inline bool validateCapture(const uint8_t *actual,size_t actualBytes,const uint8_t *initial,size_t initialBytes,unsigned completed){
 if(actualBytes!=ImageBytes||initialBytes!=ImageBytes||completed>Jobs||!Q::separate(actual,actualBytes,initial,initialBytes))return false;
 for(unsigned j=0;j<Jobs;++j){
  const unsigned count=get32(initial+cbOffset(j)+0x178);if(count>Elements||get32(initial+fenceOffset(j)))return false;
  if(get32(actual+fenceOffset(j))!=(j<completed?completion(j):0))return false;
  for(unsigned i=0;i<Elements;++i){
   const uint32_t sum=get32(initial+cbOffset(j)+0x200+i*4)+get32(initial+cbOffset(j)+0x300+i*4);
   if(get32(initial+outOffset(j)+i*4)!=~sum||get32(actual+outOffset(j)+i*4)!=(j<completed&&i<count?sum:~sum))return false;
  }
 }
 for(unsigned i=0;i<ImageBytes;++i){
  // Only submitted QMDs may change; unused descriptors must still match.
  if(i>=12288&&i<12288+completed*256)continue;
  if(i>=16384&&i<20480&&(i-16384)%SlotBytes<Elements*4)continue;
  if(i>=20480&&i<20480+Jobs*256&&(i-20480)%256<4)continue;
  if(actual[i]!=initial[i])return false;
 }
 return true;
}
}
