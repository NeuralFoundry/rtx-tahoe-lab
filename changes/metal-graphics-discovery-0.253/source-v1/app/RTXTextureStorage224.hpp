#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

// Linear shared texture storage, before texture-unit/shader admission.
// These are software allocation bounds, not measurements of GPU capability.
namespace RTXTexture224 {
constexpr std::size_t Alignment=256;
constexpr std::size_t MaxDimension=16384;
constexpr std::size_t MaxStorage=16*1024*1024;
enum class Format:std::uint32_t {
    Invalid, R32Uint, R32Sint, R32Float, RGBA8Unorm, RGBA8Uint,
    RGBA8Sint, BGRA8Unorm, RGBA32Uint, RGBA32Sint, RGBA32Float
};
inline std::size_t pixelBytes(Format f) {
    switch(f) {
    case Format::R32Uint:case Format::R32Sint:case Format::R32Float:
    case Format::RGBA8Unorm:case Format::RGBA8Uint:case Format::RGBA8Sint:
    case Format::BGRA8Unorm:return 4;
    case Format::RGBA32Uint:case Format::RGBA32Sint:case Format::RGBA32Float:return 16;
    default:return 0;
    }
}
inline bool add(std::size_t a,std::size_t b,std::size_t&out) {
    if(b>std::numeric_limits<std::size_t>::max()-a)return false;
    out=a+b;return true;
}
inline bool multiply(std::size_t a,std::size_t b,std::size_t&out) {
    if(a&&b>std::numeric_limits<std::size_t>::max()/a)return false;
    out=a*b;return true;
}
struct Layout {
    Format format=Format::Invalid;
    std::size_t width=0,height=0,pixel=0,offset=0,rowPitch=0,storageBytes=0;
};
inline bool make(Format format,std::size_t width,std::size_t height,
    std::size_t offset,std::size_t rowPitch,std::size_t backingBytes,Layout&out) {
    Layout v;v.format=format;v.width=width;v.height=height;v.pixel=pixelBytes(format);
    std::size_t rowBytes=0;
    if(!v.pixel||!width||!height||width>MaxDimension||height>MaxDimension||
       !multiply(width,v.pixel,rowBytes)||offset%Alignment||rowPitch%Alignment||
       rowPitch<rowBytes||!multiply(rowPitch,height,v.storageBytes)||
       v.storageBytes>MaxStorage||backingBytes>MaxStorage||offset>backingBytes||v.storageBytes>backingBytes-offset)return false;
    v.offset=offset;v.rowPitch=rowPitch;out=v;return true;
}
inline bool allocate(Format format,std::size_t width,std::size_t height,Layout&out) {
    const auto pixel=pixelBytes(format);std::size_t row=0;
    if(!pixel||!multiply(width,pixel,row)||!add(row,Alignment-1,row))return false;
    row&=~(Alignment-1);
    return make(format,width,height,0,row,MaxStorage,out);
}
struct Region {std::size_t x=0,y=0,width=0,height=0;};
struct Transfer {
    std::size_t imageOffset=0,imageStride=0,rowBytes=0,rows=0;
    std::size_t hostStride=0,hostExtent=0,compactBytes=0,imageExtent=0;
};
inline bool plan(const Layout&layout,const Region&region,std::size_t hostStride,Transfer&out) {
    // Reject invented/corrupt layouts even when the requested region is empty.
    Layout checked;std::size_t backing=0;
    if(!add(layout.offset,layout.storageBytes,backing)||
       !make(layout.format,layout.width,layout.height,layout.offset,layout.rowPitch,backing,checked)||
       checked.pixel!=layout.pixel||checked.storageBytes!=layout.storageBytes||
       region.x>layout.width||region.width>layout.width-region.x||
       region.y>layout.height||region.height>layout.height-region.y)return false;
    Transfer v;
    if(!region.width||!region.height){out=v;return true;}
    v.rows=region.height;v.imageStride=layout.rowPitch;
    if(!multiply(region.width,layout.pixel,v.rowBytes))return false;
    v.hostStride=hostStride?hostStride:(v.rows==1?v.rowBytes:0);
    if(v.hostStride<v.rowBytes||v.hostStride%layout.pixel)return false;
    std::size_t rowOffset=0,columnOffset=0,span=0;
    if(!multiply(region.y,layout.rowPitch,rowOffset)||!multiply(region.x,layout.pixel,columnOffset)||
       !add(layout.offset,rowOffset,v.imageOffset)||!add(v.imageOffset,columnOffset,v.imageOffset)||
       !multiply(v.rows-1,v.hostStride,span)||!add(span,v.rowBytes,v.hostExtent)||
       !multiply(v.rows,v.rowBytes,v.compactBytes)||v.compactBytes>MaxStorage||
       !multiply(v.rows-1,v.imageStride,span)||!add(span,v.rowBytes,span)||
       !add(v.imageOffset,span,v.imageExtent)||v.imageExtent>backing)return false;
    out=v;return true;
}
inline bool pointerExtent(const void*pointer,std::size_t extent) {
    if(!extent)return true;
    return pointer&&reinterpret_cast<std::uintptr_t>(pointer)<=
        std::numeric_limits<std::uintptr_t>::max()-(extent-1);
}
inline bool copy(const Transfer&v,bool upload,void*image,std::size_t imageBytes,
    void*host,std::size_t hostBytes) {
    if(!v.rows)return !v.rowBytes&&!v.hostExtent&&!v.compactBytes&&!v.imageExtent;
    std::size_t compact=0,span=0,hostExtent=0,imageExtent=0;
    if(!v.rowBytes||v.rowBytes>v.hostStride||v.rowBytes>v.imageStride||
       !multiply(v.rows,v.rowBytes,compact)||compact!=v.compactBytes||compact>MaxStorage||
       !multiply(v.rows-1,v.hostStride,span)||!add(span,v.rowBytes,hostExtent)||hostExtent!=v.hostExtent||
       !multiply(v.rows-1,v.imageStride,span)||!add(span,v.rowBytes,span)||
       !add(v.imageOffset,span,imageExtent)||imageExtent!=v.imageExtent||
       imageExtent>imageBytes||hostExtent>hostBytes||
       !pointerExtent(image,imageExtent)||!pointerExtent(host,hostExtent))return false;
    // Snapshot every source row before writing any destination row. This also
    // handles transfers whose host pointer aliases the same underlying buffer.
    std::vector<std::uint8_t> snapshot(compact);
    auto*imageStart=static_cast<std::uint8_t*>(image)+v.imageOffset;
    auto*hostStart=static_cast<std::uint8_t*>(host);
    for(std::size_t row=0;row<v.rows;++row)
        std::memcpy(snapshot.data()+row*v.rowBytes,
            upload?hostStart+row*v.hostStride:imageStart+row*v.imageStride,v.rowBytes);
    for(std::size_t row=0;row<v.rows;++row)
        std::memcpy(upload?imageStart+row*v.imageStride:hostStart+row*v.hostStride,
            snapshot.data()+row*v.rowBytes,v.rowBytes);
    return true;
}
}
