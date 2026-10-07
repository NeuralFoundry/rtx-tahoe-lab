#pragma once
#include "GSPLaunchOwnership.hpp"
#include "GSPDigest.hpp"
#include "generated/gsp-seal-constants.hpp"

// Reconstruct every address-bearing byte from current native IOVM pages and
// native PCI facts. Hash immutable firmware ranges against compiled constants.
// The caller owns serialization, upload freeze, synchronization and page proof.
namespace GSPContentSeal {
using namespace GSPDmaProtocol;
struct Facts {
  U64 bar0=0,bar1=0,bar3=0,maxUserVa=0;
  U32 revision=0xffffffffU,linkCap=0;
  bool valid() const {
    return bar0 && bar0<=0xff000000ULL && !(bar0%0x1000000) &&
      bar1 && bar1<=AddressLimit-0x4000000 && !(bar1%0x4000000) &&
      bar3 && bar3<=AddressLimit-0x2000000 && !(bar3%0x2000000) &&
      !(bar0<bar1+0x4000000 && bar1<bar0+0x1000000) &&
      !(bar0<bar3+0x2000000 && bar3<bar0+0x1000000) &&
      !(bar1<bar3+0x2000000 && bar3<bar1+0x4000000) &&
      maxUserVa>=Page && maxUserVa<=(1ULL<<47) && !(maxUserVa%Page) && revision<=255;
  }
};
inline void zero(unsigned char *p,U32 n){for(U32 i=0;i<n;++i)p[i]=0;}
inline void put32(unsigned char *p,U32 v){for(U32 i=0;i<4;++i)p[i]=static_cast<unsigned char>(v>>(8*i));}
inline void put64(unsigned char *p,U64 v){for(U32 i=0;i<8;++i)p[i]=static_cast<unsigned char>(v>>(8*i));}
inline U32 get32(const unsigned char *p){return U32(p[0])|(U32(p[1])<<8)|(U32(p[2])<<16)|(U32(p[3])<<24);}
inline U64 pageAddress(const U64 *pages,U32 resource,U32 page=0){return pages[GSPLaunchOwnership::firstPage(resource)+page];}
inline void systemInfo(unsigned char *p,const Facts &f){
  zero(p,928); put64(p,f.bar0);put64(p+8,f.bar1);put64(p+16,f.bar3);put64(p+32,0x100);
  put64(p+72,f.maxUserVa);put32(p+80,0x88000);put32(p+84,0x1000);
  put32(p+88,0x252010de);put32(p+92,0x104c1043);put32(p+96,f.revision);
  // Explicit minimal PCI startup policy from tinygrad: omit unsupported ACPI
  // data and request the passthrough initialization path. Not topology evidence.
  p[840]=1;put32(p+900,f.linkCap);put64(p+920,Page);
}
inline void record(unsigned char *page,U32 function,U32 payloadSize,U32 sequence){
  // Payload is already at byte80; the caller initialized all other bytes.
  put32(page+36,sequence);put32(page+40,1);
  put32(page+48,0x03000000);put32(page+52,0x43505256);
  put32(page+56,32+payloadSize);put32(page+60,function);
  put32(page+64,0xffffffffU);put32(page+68,0xffffffffU);
  U32 checksum=0;
  for(U32 i=0;i<((80+payloadSize+7)&~7U);i+=4)checksum^=get32(page+i);
  put32(page+32,checksum);
}
inline bool expectedPage(U32 resource,U32 index,const U64 *pages,const Facts &facts,unsigned char *out){
  if(!pages || !out || resource>=ResourceCount || index>=Pages[resource] || !facts.valid())return false;
  zero(out,Page);
  if(resource==Radix3){
    if(index>=33)return false;
    if(index==0)put64(out,pageAddress(pages,Radix3,1));
    else if(index==1){for(U32 i=0;i<31;++i)put64(out+i*8,pageAddress(pages,Radix3,2+i));}
    else {const U32 start=(index-2)*512;for(U32 i=0;i<512 && start+i<15513;++i)put64(out+i*8,pageAddress(pages,Radix3,33+start+i));}
  }else if(resource==Metadata){
    for(U32 i=0;i<32;++i)put64(out+i*8,GSPSealConstants::Metadata[i]);
    put64(out+16,pageAddress(pages,Radix3));put64(out+32,pageAddress(pages,Bootloader));put64(out+72,pageAddress(pages,Signature));
  }else if(resource==Queues){
    if(index==0){for(U32 i=0;i<129;++i)put64(out+i*8,pageAddress(pages,Queues,i));}
    else if(index==1){const U32 header[8]={0,0x40000,Page,63,2,1,32,Page};for(U32 i=0;i<8;++i)put32(out+i*4,header[i]);}
    else if(index==2){systemInfo(out+80,facts);record(out,72,928,0);}
    else if(index==3){for(U32 i=0;i<sizeof(GSPSealConstants::Registry);++i)out[80+i]=GSPSealConstants::Registry[i];record(out,73,sizeof(GSPSealConstants::Registry),1);}
  }else if(resource==Rmargs){
    put64(out,pageAddress(pages,Queues));put32(out+8,129);put64(out+16,Page);put64(out+24,0x41000);out[48]=1;
  }else if(resource==LibosArgs){
    static constexpr U64 ids[6]={0x4c4f47494e4954ULL,0x4c4f47494e5452ULL,0x4c4f47524dULL,0x4c4f474d4e4f43ULL,0x4c4f474b524e4cULL,0x524d41524753ULL};
    for(U32 i=0;i<6;++i){
      auto *r=out+i*32;put64(r,ids[i]);put64(r+8,pageAddress(pages,i<5?Logs:Rmargs,i<5?i*16:0));
      put64(r+16,i<5?65536:Page);r[24]=r[25]=1;
    }
  }else if(resource!=Logs)return false;
  return true;
}
struct Result {
  GSPLaunchOwnership::ContentSeal seal;
  const char *status="not-run";
  U32 resource=0xffffffffU,page=0xffffffffU,byte=0xffffffffU;
  U32 comparedPages=0,hashedPages=0;
  U64 checkedBytes=0;
  unsigned char digests[4][32]={};
};
// scratch and expected are separate native-owned 4096-byte allocations.
template<class IO> bool run(IO &io,const U64 *pages,U64 generation,const Facts &facts,
                           unsigned char *scratch,unsigned char *expected,Result &r){
  r=Result{};r.status="seal-input-invalid";
  if(!generation || !pages || !scratch || !expected || !facts.valid())return false;
  const U64 a=reinterpret_cast<U64>(scratch),b=reinterpret_cast<U64>(expected);
  if((a<b?b-a:a-b)<Page)return false;
  r.seal.generation=generation;
  // IO must independently verify prepared maps, global uniqueness, direct
  // resource spans, exclusive provider identity, and completed publication.
  if(!io.sealInputsValid(generation))return false;
  r.seal.globalPagesValidated=true;r.seal.uploadsSynchronized=true;
  U32 digestIndex=0;
  for(U32 resource=0;resource<ResourceCount;++resource){
    const bool firmware=resource==Radix3 || resource==Bootloader || resource==Signature || resource==BooterLoad;
    GSPDigest::SHA256 digest;
    for(U32 page=0;page<Pages[resource];++page){
      if(!io.readSeal(resource,U64(page)*Page,scratch,Page)){r.status="seal-native-read-failed";r.resource=resource;r.page=page;return false;}
      if(firmware && (resource!=Radix3 || page>=33)){
        digest.update(scratch,Page);++r.hashedPages;
      }else{
        if(!expectedPage(resource,page,pages,facts,expected)){r.status="seal-expected-page-invalid";return false;}
        for(U32 i=0;i<Page;++i)if(scratch[i]!=expected[i]){
          r.status="seal-structure-mismatch";r.resource=resource;r.page=page;r.byte=i;return false;
        }
        ++r.comparedPages;
      }
      r.checkedBytes+=Page;
    }
    if(firmware){
      const unsigned char *hash=resource==Radix3?GSPSealConstants::ImageSHA256:
        resource==Bootloader?GSPSealConstants::BootloaderSHA256:resource==Signature?GSPSealConstants::SignatureSHA256:GSPSealConstants::BooterSHA256;
      digest.finish(r.digests[digestIndex]);
      for(U32 i=0;i<32;++i)if(r.digests[digestIndex][i]!=hash[i]){r.status="seal-firmware-digest-mismatch";r.resource=resource;return false;}
      ++digestIndex;r.seal.firmwareMask|=1U<<resource;
    }
    r.seal.structuralMask|=1U<<resource;
  }
  if(!io.sealInputsValid(generation)){r.status="seal-ownership-changed";return false;}
  r.seal.readbacksVerified=true;r.status="native-content-seal-complete";
  return r.seal.completeFor(generation);
}
}
