#pragma once
#import <Metal/Metal.h>

// Creates real shared storage resources. Texture shader binding is a separate
// compiler/encoder capability and is not implied by these resource factories.
id<MTLTexture> RTXNewSharedTexture224(id<MTLDevice> device,MTLTextureDescriptor*descriptor) NS_RETURNS_RETAINED;
NSUInteger RTXLinearTextureAlignment224(MTLPixelFormat format);
// Retained backing and immutable descriptor, validated under the buffer arena lock.
BOOL RTXCopyTextureBinding225(id<MTLTexture> texture,id<MTLDevice> device,NSUInteger access,uint32_t formatContract,id<MTLBuffer>*backing,NSUInteger*offset,uint8_t descriptor[32]);

// Internal render-target write access does not require shader-write usage.
BOOL RTXCopyRenderTargetBinding231(id<MTLTexture>texture,id<MTLDevice>device,id<MTLBuffer>*backing,NSUInteger*offset,uint8_t descriptor[32]);
