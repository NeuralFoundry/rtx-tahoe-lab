#pragma once
#include "OwnedPageTree167.hpp"

// Decodes a freshly captured and coordinator-validated legacy VRAM tree.
// Capturing/owning VRAM and rejecting untrusted callers are outside this parser.
// It preserves all data PTEs; it does not acquire or extend any VRAM lease.
namespace RTXLegacy169 {
constexpr size_t RootBytes=12288,MaxChildBytes=45056,MaxPages=5120;
constexpr uint64_t ChildBase=0x1005000ULL,VaBase=0x1020000000ULL;
enum class Error:uint32_t {None,Shape,Root,Directory,LeafBits,Physical,Orphan,Empty,Capacity,Aliasing};
struct Result {Error error=Error::None;uint32_t pages=0,leaves=0;};
inline uint64_t get64(const uint8_t*p){uint64_t v=0;for(unsigned i=0;i<8;++i)v|=uint64_t(p[i])<<(i*8);return v;}
inline Result inspect(const uint8_t*root,size_t rootBytes,const uint8_t*child,size_t childBytes){
    if(rootBytes!=RootBytes||childBytes<8192||childBytes>MaxChildBytes||childBytes%4096||
       !RTXPageTree167::validRange(root,rootBytes)||!RTXPageTree167::validRange(child,childBytes))return{Error::Shape,0,0};
    for(size_t i=0;i<RootBytes;i+=8){uint64_t expected=i==0?0x100322ULL:i==4096?0x100422ULL:i==9224?0x100522ULL:0;
        if(get64(root+i)!=expected)return{Error::Root,0,0};}
    bool used[11]={};Result r;
    for(unsigned group=0;group<256;++group){
        const uint64_t lo=get64(child+group*16),hi=get64(child+group*16+8);if(!lo&&!hi)continue;
        if(lo!=0x20||(hi&255)!=2||(hi>>33))return{Error::Directory,0,0};
        const uint64_t physical=(hi&0x1ffffff00ULL)<<4;
        if(physical<ChildBase+4096||physical-ChildBase>childBytes-4096)return{Error::Directory,0,0};
        const unsigned index=unsigned((physical-ChildBase)/4096);if(used[index])return{Error::Directory,0,0};used[index]=true;
        unsigned count=0;
        for(unsigned p=0;p<512;++p){uint64_t bits=get64(child+size_t(index)*4096+p*8);if(!bits)continue;
            const uint64_t address=(bits&0x1ffffff00ULL)<<4;
            if(bits!=((6ULL<<56)|(address>>4)|1ULL))return{Error::LeafBits,0,0};
            if(!((address>=0x1100000ULL&&address<0x1101000ULL)||(address>=0x1200000ULL&&address<0x4000000ULL)))return{Error::Physical,0,0};
            ++count;
        }
        if(!count)return{Error::Empty,0,0};r.pages+=count;++r.leaves;
    }
    for(unsigned i=1;i<childBytes/4096;++i)if(!used[i])return{Error::Orphan,0,0};
    if(!r.pages)return{Error::Empty,0,0};return r;
}
inline Result decode(const uint8_t*root,size_t rootBytes,const uint8_t*child,size_t childBytes,
                     uint64_t owner,RTXPageTree167::Mapping*out,size_t capacity){
    if(!owner)return{Error::Shape,0,0};const auto r=inspect(root,rootBytes,child,childBytes);if(r.error!=Error::None)return r;
    if(capacity<r.pages||capacity>MaxPages)return{Error::Capacity,0,0};const size_t bytes=capacity*sizeof(*out);
    if(!RTXPageTree167::validRange(out,bytes)||reinterpret_cast<uintptr_t>(out)%alignof(RTXPageTree167::Mapping))return{Error::Shape,0,0};
    if(RTXPageTree167::overlap(out,bytes,root,rootBytes)||RTXPageTree167::overlap(out,bytes,child,childBytes))return{Error::Aliasing,0,0};
    unsigned n=0;
    for(unsigned group=0;group<256;++group){uint64_t hi=get64(child+group*16+8);if(!hi)continue;const size_t offset=size_t(((hi&0x1ffffff00ULL)<<4)-ChildBase);
        for(unsigned p=0;p<512;++p){const uint64_t bits=get64(child+offset+p*8);if(!bits)continue;
            out[n++]={VaBase+uint64_t(group)*(1ULL<<21)+uint64_t(p)*4096,(bits&0x1ffffff00ULL)<<4,owner,
                      RTXPageTree167::Aperture::Video,RTXPageTree167::Access::ReadWriteAtomic,1,0};}
    }
    return r;
}
}
