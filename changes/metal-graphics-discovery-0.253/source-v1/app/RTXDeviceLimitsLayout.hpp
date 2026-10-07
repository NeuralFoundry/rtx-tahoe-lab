#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "RTXSoftwareLimits.hpp"
#include "RTXTextureStorage224.hpp"

namespace RTXLimits039 {
// Exact field order from the verified Tahoe Metal runtime, not an Apple public ABI.
struct Limits {
 uint32_t maxFramebufferStorageBits;
 uint32_t linearTextureArrayAlignmentBytes;
 uint32_t linearTextureArrayAlignmentSlice;
 uint32_t maxTileBuffers;
 uint32_t maxTileTextures;
 uint32_t maxTileSamplers;
 uint32_t maxTileInlineDataSize;
 uint32_t minTilePixels;
 uint32_t maxColorAttachments;
 uint32_t maxVertexAttributes;
 uint32_t maxVertexBuffers;
 uint32_t maxVertexTextures;
 uint32_t maxVertexSamplers;
 uint32_t maxVertexInlineDataSize;
 uint32_t maxInterpolants;
 uint32_t maxFragmentBuffers;
 uint32_t maxFragmentTextures;
 uint32_t maxFragmentSamplers;
 uint32_t maxFragmentInlineDataSize;
 uint32_t maxComputeBuffers;
 uint32_t maxComputeTextures;
 uint32_t maxComputeSamplers;
 uint32_t maxComputeInlineDataSize;
 uint32_t maxComputeLocalMemorySizes;
 uint32_t maxTotalComputeThreadsPerThreadgroup;
 uint32_t maxComputeThreadgroupMemory;
 float maxLineWidth;
 float maxPointSize;
 uint32_t maxVisibilityQueryOffset;
 uint32_t padmaxBufferLength;
 uint32_t minConstantBufferAlignmentBytes;
 uint32_t minBufferNoCopyAlignmentBytes;
 uint32_t maxTextureWidth1D;
 uint32_t maxTextureWidth2D;
 uint32_t maxTextureHeight2D;
 uint32_t maxTextureWidth3D;
 uint32_t maxTextureHeight3D;
 uint32_t maxTextureDepth3D;
 uint32_t maxTextureDimensionCube;
 uint32_t maxTextureLayers;
 uint32_t linearTextureAlignmentBytes;
 uint32_t iosurfaceTextureAlignmentBytes;
 uint32_t iosurfaceReadOnlyTextureAlignmentBytes;
 uint32_t deviceLinearTextureAlignmentBytes;
 uint32_t deviceLinearReadOnlyTextureAlignmentBytes;
 uint32_t maxFunctionConstantIndices;
 uint32_t maxComputeThreadgroupMemoryAlignmentBytes;
 uint32_t maxInterpolatedComponents;
 uint32_t maxTessellationFactor;
 uint32_t maxIndirectBuffers;
 uint32_t maxIndirectTextures;
 uint32_t maxIndirectSamplers;
 uint32_t maxIndirectSamplersPerDevice;
 uint32_t maxFenceInstances;
 uint32_t maxViewportCount;
 uint32_t maxCustomSamplePositions;
 uint32_t maxVertexAmplificationFactor;
 uint32_t maxVertexAmplificationCount;
 uint32_t maxTextureBufferWidth;
 uint32_t maxComputeAttributes;
 uint32_t maxIOCommandsInFlight;
 uint32_t maxPredicatedNestingDepth;
 uint32_t maxAccelerationStructureLevels;
 uint32_t maxConstantBufferArguments;
 uint64_t maxBufferLength;
};
static_assert(sizeof(Limits)==264 && alignof(Limits)==8);
static_assert(offsetof(Limits,maxFramebufferStorageBits)==0);
static_assert(offsetof(Limits,linearTextureArrayAlignmentBytes)==4);
static_assert(offsetof(Limits,linearTextureArrayAlignmentSlice)==8);
static_assert(offsetof(Limits,maxTileBuffers)==12);
static_assert(offsetof(Limits,maxTileTextures)==16);
static_assert(offsetof(Limits,maxTileSamplers)==20);
static_assert(offsetof(Limits,maxTileInlineDataSize)==24);
static_assert(offsetof(Limits,minTilePixels)==28);
static_assert(offsetof(Limits,maxColorAttachments)==32);
static_assert(offsetof(Limits,maxVertexAttributes)==36);
static_assert(offsetof(Limits,maxVertexBuffers)==40);
static_assert(offsetof(Limits,maxVertexTextures)==44);
static_assert(offsetof(Limits,maxVertexSamplers)==48);
static_assert(offsetof(Limits,maxVertexInlineDataSize)==52);
static_assert(offsetof(Limits,maxInterpolants)==56);
static_assert(offsetof(Limits,maxFragmentBuffers)==60);
static_assert(offsetof(Limits,maxFragmentTextures)==64);
static_assert(offsetof(Limits,maxFragmentSamplers)==68);
static_assert(offsetof(Limits,maxFragmentInlineDataSize)==72);
static_assert(offsetof(Limits,maxComputeBuffers)==76);
static_assert(offsetof(Limits,maxComputeTextures)==80);
static_assert(offsetof(Limits,maxComputeSamplers)==84);
static_assert(offsetof(Limits,maxComputeInlineDataSize)==88);
static_assert(offsetof(Limits,maxComputeLocalMemorySizes)==92);
static_assert(offsetof(Limits,maxTotalComputeThreadsPerThreadgroup)==96);
static_assert(offsetof(Limits,maxComputeThreadgroupMemory)==100);
static_assert(offsetof(Limits,maxLineWidth)==104);
static_assert(offsetof(Limits,maxPointSize)==108);
static_assert(offsetof(Limits,maxVisibilityQueryOffset)==112);
static_assert(offsetof(Limits,padmaxBufferLength)==116);
static_assert(offsetof(Limits,minConstantBufferAlignmentBytes)==120);
static_assert(offsetof(Limits,minBufferNoCopyAlignmentBytes)==124);
static_assert(offsetof(Limits,maxTextureWidth1D)==128);
static_assert(offsetof(Limits,maxTextureWidth2D)==132);
static_assert(offsetof(Limits,maxTextureHeight2D)==136);
static_assert(offsetof(Limits,maxTextureWidth3D)==140);
static_assert(offsetof(Limits,maxTextureHeight3D)==144);
static_assert(offsetof(Limits,maxTextureDepth3D)==148);
static_assert(offsetof(Limits,maxTextureDimensionCube)==152);
static_assert(offsetof(Limits,maxTextureLayers)==156);
static_assert(offsetof(Limits,linearTextureAlignmentBytes)==160);
static_assert(offsetof(Limits,iosurfaceTextureAlignmentBytes)==164);
static_assert(offsetof(Limits,iosurfaceReadOnlyTextureAlignmentBytes)==168);
static_assert(offsetof(Limits,deviceLinearTextureAlignmentBytes)==172);
static_assert(offsetof(Limits,deviceLinearReadOnlyTextureAlignmentBytes)==176);
static_assert(offsetof(Limits,maxFunctionConstantIndices)==180);
static_assert(offsetof(Limits,maxComputeThreadgroupMemoryAlignmentBytes)==184);
static_assert(offsetof(Limits,maxInterpolatedComponents)==188);
static_assert(offsetof(Limits,maxTessellationFactor)==192);
static_assert(offsetof(Limits,maxIndirectBuffers)==196);
static_assert(offsetof(Limits,maxIndirectTextures)==200);
static_assert(offsetof(Limits,maxIndirectSamplers)==204);
static_assert(offsetof(Limits,maxIndirectSamplersPerDevice)==208);
static_assert(offsetof(Limits,maxFenceInstances)==212);
static_assert(offsetof(Limits,maxViewportCount)==216);
static_assert(offsetof(Limits,maxCustomSamplePositions)==220);
static_assert(offsetof(Limits,maxVertexAmplificationFactor)==224);
static_assert(offsetof(Limits,maxVertexAmplificationCount)==228);
static_assert(offsetof(Limits,maxTextureBufferWidth)==232);
static_assert(offsetof(Limits,maxComputeAttributes)==236);
static_assert(offsetof(Limits,maxIOCommandsInFlight)==240);
static_assert(offsetof(Limits,maxPredicatedNestingDepth)==244);
static_assert(offsetof(Limits,maxAccelerationStructureLevels)==248);
static_assert(offsetof(Limits,maxConstantBufferArguments)==252);
static_assert(offsetof(Limits,maxBufferLength)==256);
constexpr char Encoding[]="{?=\"maxFramebufferStorageBits\"I\"linearTextureArrayAlignmentBytes\"I\"linearTextureArrayAlignmentSlice\"I\"maxTileBuffers\"I\"maxTileTextures\"I\"maxTileSamplers\"I\"maxTileInlineDataSize\"I\"minTilePixels\"I\"maxColorAttachments\"I\"maxVertexAttributes\"I\"maxVertexBuffers\"I\"maxVertexTextures\"I\"maxVertexSamplers\"I\"maxVertexInlineDataSize\"I\"maxInterpolants\"I\"maxFragmentBuffers\"I\"maxFragmentTextures\"I\"maxFragmentSamplers\"I\"maxFragmentInlineDataSize\"I\"maxComputeBuffers\"I\"maxComputeTextures\"I\"maxComputeSamplers\"I\"maxComputeInlineDataSize\"I\"maxComputeLocalMemorySizes\"I\"maxTotalComputeThreadsPerThreadgroup\"I\"maxComputeThreadgroupMemory\"I\"maxLineWidth\"f\"maxPointSize\"f\"maxVisibilityQueryOffset\"I\"padmaxBufferLength\"I\"minConstantBufferAlignmentBytes\"I\"minBufferNoCopyAlignmentBytes\"I\"maxTextureWidth1D\"I\"maxTextureWidth2D\"I\"maxTextureHeight2D\"I\"maxTextureWidth3D\"I\"maxTextureHeight3D\"I\"maxTextureDepth3D\"I\"maxTextureDimensionCube\"I\"maxTextureLayers\"I\"linearTextureAlignmentBytes\"I\"iosurfaceTextureAlignmentBytes\"I\"iosurfaceReadOnlyTextureAlignmentBytes\"I\"deviceLinearTextureAlignmentBytes\"I\"deviceLinearReadOnlyTextureAlignmentBytes\"I\"maxFunctionConstantIndices\"I\"maxComputeThreadgroupMemoryAlignmentBytes\"I\"maxInterpolatedComponents\"I\"maxTessellationFactor\"I\"maxIndirectBuffers\"I\"maxIndirectTextures\"I\"maxIndirectSamplers\"I\"maxIndirectSamplersPerDevice\"I\"maxFenceInstances\"I\"maxViewportCount\"I\"maxCustomSamplePositions\"I\"maxVertexAmplificationFactor\"I\"maxVertexAmplificationCount\"I\"maxTextureBufferWidth\"I\"maxComputeAttributes\"I\"maxIOCommandsInFlight\"I\"maxPredicatedNestingDepth\"I\"maxAccelerationStructureLevels\"I\"maxConstantBufferArguments\"I\"maxBufferLength\"Q}";
inline bool layoutMatches(std::ptrdiff_t offset,std::ptrdiff_t next,size_t bytes,const char *type){
 return offset==8 && next==272 && bytes==712 && type && !std::strcmp(type,Encoding);
}
inline Limits supported(){
 Limits v{};
 v.maxComputeBuffers=32; // implemented binding indices 0..31; reviewed programs use a subset
 v.maxTotalComputeThreadsPerThreadgroup=RTXSoftware039::MaxDispatchThreads;
 v.minConstantBufferAlignmentBytes=4;
 v.maxBufferLength=RTXSoftware039::MaxBuffer;
 // Linear shared 2D storage now has a concrete layout. Compute texture slots,
 // samplers, render targets and multisampling remain unavailable.
 v.maxTextureWidth2D=uint32_t(RTXTexture224::MaxDimension);v.maxTextureHeight2D=uint32_t(RTXTexture224::MaxDimension);
 v.maxTextureLayers=1;v.linearTextureAlignmentBytes=uint32_t(RTXTexture224::Alignment);
 v.deviceLinearTextureAlignmentBytes=uint32_t(RTXTexture224::Alignment);v.deviceLinearReadOnlyTextureAlignmentBytes=uint32_t(RTXTexture224::Alignment);
 return v;
}
}
