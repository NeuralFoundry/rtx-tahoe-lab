#pragma once
// Immutable, little-endian preflight record. IORegistry origin and the actual
// parent registry ID must be verified separately; this is not attestation.
namespace RTXMemory107 {
using U64=unsigned long long;
static_assert(sizeof(unsigned)==4&&sizeof(U64)==8,"wire widths");
constexpr unsigned Bytes=128,Magic=0x4d585452,Version=1,Source=1;
constexpr unsigned Register=0x1183a4,ExpectedMiB=6144;
constexpr char Property[]="ProbeMemoryEvidence107";
struct Evidence {
 U64 generation=0,reportedBytes=0;
 unsigned identity=0,subsystem=0,first=0,second=0,reads=0,complete=0;
 unsigned fuseFirst=0,fuseSecond=0,fuseReads=0,bootFirst=0,bootSecond=0,bootReads=0;
 unsigned commandBefore=0,commandDuring=0,commandAfter=0,restored=0,bdf=0;
};
inline bool valid(const Evidence &e){
 return e.generation&&e.identity==0x252010de&&e.subsystem==0x104c1043&&e.bdf==0x100&&
  e.first==ExpectedMiB&&e.second==e.first&&e.reads==2&&e.complete==1&&
  e.reportedBytes==(U64(e.first)<<20)&&e.fuseFirst==1&&e.fuseSecond==1&&e.fuseReads==2&&
  e.bootFirst==0xb76000a1&&e.bootSecond==e.bootFirst&&e.bootReads==2&&
  e.commandBefore==0&&e.commandDuring==2&&e.commandAfter==0&&e.restored==1;
}
inline unsigned get32(const unsigned char *p){unsigned v=0;for(unsigned i=0;i<4;++i)v|=unsigned(p[i])<<(8*i);return v;}
inline U64 get64(const unsigned char *p){return U64(get32(p))|(U64(get32(p+4))<<32);}
inline void put32(unsigned char*p,unsigned v){for(unsigned i=0;i<4;++i)p[i]=static_cast<unsigned char>(v>>(8*i));}
inline void put64(unsigned char*p,U64 v){put32(p,unsigned(v));put32(p+4,unsigned(v>>32));}
inline bool encode(const Evidence&e,unsigned char *p,unsigned bytes){
 if(!p||bytes!=Bytes||!valid(e))return false;
 for(unsigned i=0;i<Bytes;++i)p[i]=0;
 put32(p,Magic);put32(p+4,Version);put32(p+8,Bytes);put32(p+12,Source);
 put64(p+16,e.generation);put32(p+24,e.identity);put32(p+28,e.subsystem);
 put32(p+32,e.first);put32(p+36,e.second);put32(p+40,e.reads);put32(p+44,e.complete);
 put64(p+48,e.reportedBytes);put32(p+56,e.fuseFirst);put32(p+60,e.fuseSecond);put32(p+64,e.fuseReads);
 put32(p+68,e.bootFirst);put32(p+72,e.bootSecond);put32(p+76,e.bootReads);
 put32(p+80,e.commandBefore);put32(p+84,e.commandDuring);put32(p+88,e.commandAfter);put32(p+92,e.restored);
 put32(p+96,e.bdf);put32(p+100,Register);return true;
}
// Failure leaves output unchanged. Every byte in this version has a contract.
inline bool decode(const unsigned char*p,unsigned bytes,Evidence &out){
 if(!p||bytes!=Bytes||get32(p)!=Magic||get32(p+4)!=Version||get32(p+8)!=Bytes||get32(p+12)!=Source||get32(p+100)!=Register)return false;
 for(unsigned i=104;i<Bytes;++i)if(p[i])return false;
 Evidence e;e.generation=get64(p+16);e.identity=get32(p+24);e.subsystem=get32(p+28);
 e.first=get32(p+32);e.second=get32(p+36);e.reads=get32(p+40);e.complete=get32(p+44);e.reportedBytes=get64(p+48);
 e.fuseFirst=get32(p+56);e.fuseSecond=get32(p+60);e.fuseReads=get32(p+64);e.bootFirst=get32(p+68);e.bootSecond=get32(p+72);e.bootReads=get32(p+76);
 e.commandBefore=get32(p+80);e.commandDuring=get32(p+84);e.commandAfter=get32(p+88);e.restored=get32(p+92);e.bdf=get32(p+96);
 if(!valid(e))return false;out=e;return true;
}
}
