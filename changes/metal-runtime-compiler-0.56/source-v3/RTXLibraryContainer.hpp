#pragma once
#include "changes/gsp-program-library-0.33/ProgramLibrary.hpp"
#include "driver/GSPDigest.hpp"
#include "RTXSoftwareLimits.hpp"

// Backend-specific precompiled library, not Apple's metallib/AIR format.
// Code admission still requires reviewed compiler provenance. This container's
// hash detects corruption; it does not authenticate or validate SASS semantics.
namespace RTXLibrary036 {
namespace P=RtxProgram033;
constexpr unsigned Header=128,Names=512,Payload=4608,Bytes=Header+Names+Payload;
constexpr uint64_t Magic=UINT64_C(0x5254584d4c423336);
struct Catalog {P::Library library;char names[4][128]={};uint8_t payloadDigest[32]={};};
inline bool decode(const uint8_t *bytes,size_t n,Catalog &result){
 if(n!=Bytes||!P::separate(bytes,n,&result,sizeof(result)))return false;
 if(P::get64(bytes)!=Magic||P::get32(bytes+8)!=1||P::get32(bytes+12)!=Bytes||
    P::get32(bytes+16)!=0x86||P::get32(bytes+24)!=Names||P::get32(bytes+28)!=Payload||!P::zero(bytes,64,128))return false;
 uint8_t digest[32];GSPDigest::SHA256 hash;hash.update(bytes+Header,Names+Payload);hash.finish(digest);
 for(unsigned i=0;i<32;++i)if(digest[i]!=bytes[32+i])return false;
 Catalog parsed;
 if(!P::decode(bytes+Header+Names,512,bytes+Header+Names+512,4096,parsed.library)||P::get32(bytes+20)!=parsed.library.count)return false;
 for(unsigned i=0;i<4;++i){
  const auto *name=bytes+Header+i*128;
  if(i>=parsed.library.count){if(!P::zero(name,0,128))return false;continue;}
  if(parsed.library.programs[i].localX>RTXSoftware039::MaxDispatchThreads)return false;
  unsigned length=0;
  for(;length<128&&name[length];++length){
   const auto c=name[length];const bool letter=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';
   if(!letter&&!(length&&c>='0'&&c<='9'))return false;
   parsed.names[i][length]=char(c);
  }
  if(!length||length==128||!P::zero(name,length,128))return false;
  for(unsigned j=0;j<i;++j){bool same=true;for(unsigned k=0;k<128;++k)if(parsed.names[i][k]!=parsed.names[j][k])same=false;if(same)return false;}
 }
 GSPDigest::SHA256 payload;payload.update(bytes+Header+Names,Payload);payload.finish(parsed.payloadDigest);
 result=parsed;return true;
}
inline bool dispatch(const Catalog &catalog,unsigned program,size_t gx,size_t gy,size_t gz,size_t tx,size_t ty,size_t tz){
 if(program>=catalog.library.count||gy!=1||gz!=1||ty!=1||tz!=1||!gx)return false;
 const auto &p=catalog.library.programs[program];
 if(!p.localX||p.localX>RTXSoftware039::MaxDispatchThreads)return false;
 return tx==p.localX&&gx<=RTXSoftware039::MaxDispatchThreads/p.localX;
}
}
