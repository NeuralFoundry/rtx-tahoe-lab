#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

// Application/broker wire contract, independent of kernel pointers and GPU VA.
// This plans retained snapshots only. A transport must separately establish
// native GPU completion before publishing a validated readback.
namespace RTXDrawTransfer248 {
using Bytes=std::vector<uint8_t>;using Digest=std::array<uint8_t,32>;
constexpr uint64_t Magic=0x5254584452573438ULL;
constexpr unsigned ABI=248,Header=160,VertexStride=16,MaxVertices=3072;
constexpr uint64_t MaxVertexBytes=65536,MaxColorBytes=1048576,MaxPacket=Header+MaxVertexBytes+MaxColorBytes;
struct Input {
 uint64_t generation=0,serial=0,vertexBytes=0,colorBytes=0,vertexOffset=0,colorOffset=0;
 uint32_t firstVertex=0,vertexCount=0,width=0,height=0,pitch=0;
 Digest programDigest{};
};
struct Plan:Input {uint64_t vertexBegin=0,vertexEnd=0,colorEnd=0,payloadBytes=0,packetBytes=0;};
inline uint32_t u32(const uint8_t*p){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(p[i])<<(i*8);return v;}
inline uint64_t u64(const uint8_t*p){return uint64_t(u32(p))|(uint64_t(u32(p+4))<<32);}
inline void put32(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
inline void put64(uint8_t*p,uint64_t v){put32(p,uint32_t(v));put32(p+4,uint32_t(v>>32));}
inline bool zero(const uint8_t*p,size_t n){for(size_t i=0;i<n;++i)if(p[i])return false;return true;}
inline bool plan(const Input&i,Plan&out){
 if(!i.generation||!i.serial||!i.vertexBytes||i.vertexBytes>MaxVertexBytes||!i.colorBytes||i.colorBytes>MaxColorBytes||
    i.vertexOffset>i.vertexBytes||i.colorOffset>i.colorBytes||(i.vertexOffset&3)||(i.colorOffset&127)||
    !i.vertexCount||i.vertexCount>MaxVertices||i.vertexCount%3||!i.width||!i.height||i.width>512||i.height>512||
    !i.pitch||(i.pitch&127)||i.pitch<uint64_t(i.width)*4||zero(i.programDigest.data(),i.programDigest.size()))return false;
 const uint64_t skip=uint64_t(i.firstVertex)*VertexStride,used=uint64_t(i.vertexCount)*VertexStride;
 if(skip>i.vertexBytes-i.vertexOffset||used>i.vertexBytes-i.vertexOffset-skip)return false;
 const uint64_t footprint=uint64_t(i.pitch)*i.height;if(footprint>i.colorBytes-i.colorOffset)return false;
 Plan next;static_cast<Input&>(next)=i;next.vertexBegin=i.vertexOffset+skip;next.vertexEnd=next.vertexBegin+used;
 next.colorEnd=i.colorOffset+footprint;next.payloadBytes=i.vertexBytes+i.colorBytes;next.packetBytes=Header+next.payloadBytes;
 out=next;return true;
}
inline bool finiteVertices(const Plan&p,const uint8_t*vertex,size_t n){
 if(!vertex||n!=p.vertexBytes)return false;
 for(uint64_t at=p.vertexBegin;at<p.vertexEnd;at+=4){const auto bits=u32(vertex+at);float value;static_assert(sizeof(value)==4&&std::numeric_limits<float>::is_iec559);std::memcpy(&value,&bits,4);if(!std::isfinite(value))return false;}
 return true;
}
inline bool assemble(const Input&i,const Bytes&vertex,const Bytes&color,Bytes&out){
 Plan p;if(!plan(i,p)||vertex.size()!=p.vertexBytes||color.size()!=p.colorBytes||!finiteVertices(p,vertex.data(),vertex.size()))return false;
 Bytes next(size_t(p.packetBytes),0);auto*w=next.data();put64(w,Magic);put32(w+8,ABI);put32(w+12,Header);
 put64(w+16,p.generation);put64(w+24,p.serial);put64(w+32,p.vertexBytes);put64(w+40,p.colorBytes);put64(w+48,p.vertexOffset);put64(w+56,p.colorOffset);
 put32(w+64,p.firstVertex);put32(w+68,p.vertexCount);put32(w+72,p.width);put32(w+76,p.height);put32(w+80,p.pitch);
 put32(w+84,3);put32(w+88,70);put32(w+92,1); // triangle, RGBA8Unorm, Metal upper-left viewport
 std::memcpy(w+96,p.programDigest.data(),32);std::memcpy(w+Header,vertex.data(),vertex.size());std::memcpy(w+Header+vertex.size(),color.data(),color.size());out.swap(next);return true;
}
inline bool decode(const void*input,size_t bytes,uint64_t generation,uint64_t serial,const Digest&digest,Plan&out){
 if(!input||bytes<Header||bytes>MaxPacket)return false;const auto*w=static_cast<const uint8_t*>(input);
 if(u64(w)!=Magic||u32(w+8)!=ABI||u32(w+12)!=Header||u64(w+16)!=generation||u64(w+24)!=serial||
    u32(w+84)!=3||u32(w+88)!=70||u32(w+92)!=1||!zero(w+128,32)||std::memcmp(w+96,digest.data(),32))return false;
 Input i;i.generation=generation;i.serial=serial;i.vertexBytes=u64(w+32);i.colorBytes=u64(w+40);i.vertexOffset=u64(w+48);i.colorOffset=u64(w+56);
 i.firstVertex=u32(w+64);i.vertexCount=u32(w+68);i.width=u32(w+72);i.height=u32(w+76);i.pitch=u32(w+80);i.programDigest=digest;
 Plan next;if(!plan(i,next)||next.packetBytes!=bytes||!finiteVertices(next,w+Header,size_t(next.vertexBytes)))return false;out=next;return true;
}
// Result is the entire snapshotted vertex/color payload. Vertex bytes, color
// prefix/suffix and row padding must survive. Pixel arithmetic is audited by
// the GPU worker; this function makes no execution or pixel-conformance claim.
inline bool readback(const Plan&p,const Bytes&request,const void*result,size_t size,uint64_t completion){
 Plan checked;if(!decode(request.data(),request.size(),p.generation,p.serial,p.programDigest,checked)||checked.vertexBegin!=p.vertexBegin||checked.vertexEnd!=p.vertexEnd||checked.colorEnd!=p.colorEnd||checked.payloadBytes!=p.payloadBytes||
    checked.vertexBytes!=p.vertexBytes||checked.colorBytes!=p.colorBytes||checked.colorOffset!=p.colorOffset||checked.width!=p.width||checked.height!=p.height||checked.pitch!=p.pitch||
    !result||size!=checked.payloadBytes||completion!=p.serial)return false;
 const auto*before=request.data()+Header;const auto*after=static_cast<const uint8_t*>(result);
 if(std::memcmp(before,after,size_t(checked.vertexBytes)))return false;before+=checked.vertexBytes;after+=checked.vertexBytes;
 if(std::memcmp(before,after,size_t(checked.colorOffset))||std::memcmp(before+checked.colorEnd,after+checked.colorEnd,size_t(checked.colorBytes-checked.colorEnd)))return false;
 const size_t active=size_t(checked.width)*4;
 for(unsigned y=0;y<checked.height;++y){const auto off=checked.colorOffset+uint64_t(y)*checked.pitch+active;if(std::memcmp(before+off,after+off,checked.pitch-active))return false;}
 return true;
}
}
