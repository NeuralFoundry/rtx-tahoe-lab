#pragma once
#include "RTXTextureBindings225.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace RTXRenderClear231 {
// Native ABI2's largest retained backing, including any texture-view offset.
constexpr std::size_t MaxBacking=4194305;
struct Plan {
    std::array<std::uint8_t,256> color{},descriptors{};
    std::uint32_t width=0,height=0,groupsX=0,groupsY=0;
};
inline bool make(const std::uint8_t descriptor[32],std::size_t backingBytes,
    std::size_t offset,const std::array<double,4>&rgba,Plan&out) {
    if(!descriptor||!backingBytes||backingBytes>MaxBacking)return false;
    using namespace RTXTexture225;
    if(get(descriptor)!=Magic||get(descriptor+4)!=1)return false;
    const auto width=get(descriptor+8),height=get(descriptor+12),pitch=get(descriptor+16);
    const auto pixel=get(descriptor+20),format=get(descriptor+24),extent=get(descriptor+28);
    if(!permits(FloatFormats226,format))return false;
    RTXTexture224::Layout layout;
    if(!RTXTexture224::make(static_cast<RTXTexture224::Format>(format),width,height,
        offset,pitch,backingBytes,layout)||layout.pixel!=pixel||layout.storageBytes!=extent)return false;
    Plan v;std::array<float,4> color{};
    for(unsigned i=0;i<4;++i) {
        if(!std::isfinite(rgba[i])||std::abs(rgba[i])>std::numeric_limits<float>::max())return false;
        color[i]=static_cast<float>(rgba[i]);
    }
    std::memcpy(v.color.data(),color.data(),sizeof(color));
    std::memcpy(v.descriptors.data(),descriptor,32);
    v.width=width;v.height=height;v.groupsX=(width+63)/64;v.groupsY=height;
    out=v;return true;
}
}
