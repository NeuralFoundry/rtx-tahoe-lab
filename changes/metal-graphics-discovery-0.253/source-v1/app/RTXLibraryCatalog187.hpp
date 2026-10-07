#pragma once
#include "RTXLibraryContainer.hpp"
#include "RTXTextureBindings225.hpp"
#include "../probe/kernel/root179/DispatchGeometry164.hpp"
#include "../probe/kernel/root179/changes/gsp-program-library-0.33/ProgramLibrary.hpp"
namespace RTXCatalog187 {
namespace P=RtxProgram164;
constexpr unsigned Bytes=RTXLibrary036::Bytes;
struct Catalog {unsigned abi=0,containerABI=0;RTXTexture225::Resources resources{};P::Library library{};RTXLibrary036::Catalog legacy{};char names[4][128]{};uint8_t payloadDigest[32]{};};
inline bool decode(const uint8_t*bytes,size_t n,Catalog&out){
 if(n!=Bytes||!P::separate(bytes,n,&out,sizeof(out)))return false;
 Catalog c;const auto abi=P::get32(bytes+8);
 if(abi==1){
  if(!RTXLibrary036::decode(bytes,n,c.legacy))return false;c.abi=1;c.containerABI=1;c.library.count=c.legacy.library.count;c.library.usedCodeBytes=c.legacy.library.usedCodeBytes;
  for(unsigned i=0;i<4;++i){const auto&a=c.legacy.library.programs[i];auto&b=c.library.programs[i];
   b.offset=a.offset;b.bytes=a.bytes;b.registers=a.registers;b.localX=a.localX;b.localY=b.localZ=i<c.library.count?1:0;b.parameters=a.parameters;b.readMask=a.readMask;b.writeMask=a.writeMask;b.constantBytes=a.constantBytes;
   for(unsigned j=0;j<8;++j)b.bindings[j]=a.bindings[j];for(unsigned j=0;j<128;++j)c.names[i][j]=c.legacy.names[i][j];
  }for(unsigned i=0;i<32;++i)c.payloadDigest[i]=c.legacy.payloadDigest[i];out=c;return true;
 }
 if((abi!=2&&abi!=3)||P::get64(bytes)!=RTXLibrary036::Magic||P::get32(bytes+12)!=Bytes||P::get32(bytes+16)!=0x86||P::get32(bytes+24)!=512||P::get32(bytes+28)!=4608||!P::zero(bytes,64,128))return false;
 uint8_t digest[32];GSPDigest::SHA256 hash;hash.update(bytes+128,512+4608);hash.finish(digest);
 for(unsigned i=0;i<32;++i)if(digest[i]!=bytes[32+i])return false;
 if(!P::decode(bytes+640,512,bytes+1152,4096,c.library)||P::get32(bytes+20)!=c.library.count)return false;
 if(abi==3){
  const auto&p=c.library.programs[0];
  if(c.library.count!=1||!P::zero(bytes,384,640)||!RTXTexture225::decode(bytes+256,p.parameters,p.readMask,p.writeMask,c.resources))return false;
  for(unsigned i=0;i<p.parameters;++i)if(p.bindings[i]!=i)return false;
 }
 for(unsigned i=0;i<(abi==3?1u:4u);++i){const auto*name=bytes+128+i*128;if(i>=c.library.count){if(!P::zero(name,0,128))return false;continue;}
  unsigned len=0;for(;len<128&&name[len];++len){const auto ch=name[len];const bool letter=(ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||ch=='_';if(!letter&&!(len&&ch>='0'&&ch<='9'))return false;c.names[i][len]=char(ch);}
  if(!len||len==128||!P::zero(name,len,128))return false;
  for(unsigned j=0;j<i;++j){bool same=true;for(unsigned k=0;k<128;++k)if(c.names[i][k]!=c.names[j][k])same=false;if(same)return false;}
 }
 GSPDigest::SHA256 payload;payload.update(bytes+640,4608);payload.finish(c.payloadDigest);c.abi=2;c.containerABI=abi;out=c;return true;
}
inline bool dispatch(const Catalog&c,unsigned program,size_t gx,size_t gy,size_t gz,size_t tx,size_t ty,size_t tz){
 if(c.abi==1)return RTXLibrary036::dispatch(c.legacy,program,gx,gy,gz,tx,ty,tz);
 if(c.abi!=2||program>=c.library.count)return false;const auto&p=c.library.programs[program];RTXGeometry164::Shape shape;
 return tx==p.localX&&ty==p.localY&&tz==p.localZ&&RTXGeometry164::dispatch({gx,gy,gz},{tx,ty,tz},shape);
}
}
