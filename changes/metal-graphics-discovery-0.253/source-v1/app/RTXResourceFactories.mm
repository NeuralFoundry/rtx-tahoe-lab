#import "RTXResourceFactories.h"
#import "RTXMetalTexture224.h"
#import "RTXMetalSampler227.h"
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <cstring>

namespace {
// These remaining resource families are not implemented by the current backend.
// Their nullable creation methods must fail normally, without dispatching to
// an absent selector, claiming ownership of borrowed storage, or invoking its
// deallocator. Each IMP has the actual observed argument ABI; no generic varargs
// or objc forwarding catch-all is used.
id noDescriptorResource(id,SEL,id){return nil;}
id newSampler227(id device,SEL,MTLSamplerDescriptor*d){return RTXNewSamplerState227(device,d);}
id newSharedTexture224(id device,SEL,MTLTextureDescriptor*descriptor){return RTXNewSharedTexture224(device,descriptor);}
NSUInteger linearTextureAlignment224(id,SEL,MTLPixelFormat format){return RTXLinearTextureAlignment224(format);}
// The heap implementation currently supports buffers only. A texture sizing
// query has no supported layout, irrespective of descriptor contents. Return
// the measured failed-layout result without allocation or descriptor mutation.
// In particular, do not report a positive alignment usable for a texture view.
MTLSizeAndAlign noTextureHeapLayout119(id,SEL,id){return {0,0};}
// Match the observed AMD base's unavailable texture-buffer alignment sentinel.
// MTLTextureTypeTextureBuffer remains unavailable. Linear 2D buffer views use
// their separate minimumLinearTextureAlignmentForPixelFormat: implementation.
// NSUIntegerMax is not a usable TextureBuffer alignment.
// This is deliberately different from the {0,0} failed heap-texture layout.
NSUInteger noTextureBufferAlignment122(id,SEL,MTLPixelFormat){return NSUIntegerMax;}
id noFence(id,SEL){return nil;}
id noIndirectBuffer(id,SEL,id,NSUInteger,NSUInteger){return nil;}
id noSurfaceTexture(id,SEL,id,IOSurfaceRef,NSUInteger){return nil;}
id noTiledTexture(id,SEL,void *,NSUInteger,id,NSUInteger,NSUInteger){return nil;}
id noTiledTextureWithDeallocator(id,SEL,void *,NSUInteger,void(^)(void *,NSUInteger),id,NSUInteger,NSUInteger){return nil;}
struct Entry{const char *name;IMP imp;const char *encoding;bool overridesInherited=false;};
const Entry entries[]={
    {"minimumTextureBufferAlignmentForPixelFormat:",reinterpret_cast<IMP>(noTextureBufferAlignment122),"Q24@0:8Q16"},
    {"minimumLinearTextureAlignmentForPixelFormat:",reinterpret_cast<IMP>(linearTextureAlignment224),"Q24@0:8Q16",true},
    {"heapTextureSizeAndAlignWithDescriptor:",reinterpret_cast<IMP>(noTextureHeapLayout119),"{?=QQ}24@0:8@16"},
    {"newIndirectCommandBufferWithDescriptor:maxCommandCount:options:",reinterpret_cast<IMP>(noIndirectBuffer),"@40@0:8@16Q24Q32"},
    {"newIndirectCommandBufferWithDescriptor:maxCount:options:",reinterpret_cast<IMP>(noIndirectBuffer),"@40@0:8@16Q24Q32"},
    {"newDepthStencilStateWithDescriptor:",reinterpret_cast<IMP>(noDescriptorResource),"@24@0:8@16"},
    {"newFence",reinterpret_cast<IMP>(noFence),"@16@0:8"},
    {"newSamplerStateWithDescriptor:",reinterpret_cast<IMP>(newSampler227),"@24@0:8@16"},
    {"newTextureWithDescriptor:",reinterpret_cast<IMP>(newSharedTexture224),"@24@0:8@16"},
    {"newTextureWithDescriptor:iosurface:plane:",reinterpret_cast<IMP>(noSurfaceTexture),"@40@0:8@16^{__IOSurface=}24Q32"},
    {"newIndirectArgumentBufferLayoutWithStructType:",reinterpret_cast<IMP>(noDescriptorResource),"@24@0:8@16"},
    {"newIndirectComputeCommandEncoderWithBuffer:",reinterpret_cast<IMP>(noDescriptorResource),"@24@0:8@16"},
    {"newIndirectRenderCommandEncoderWithBuffer:",reinterpret_cast<IMP>(noDescriptorResource),"@24@0:8@16"},
    {"newIntersectionFunctionTableWithDescriptor:",reinterpret_cast<IMP>(noDescriptorResource),"@24@0:8@16"},
    {"newVisibleFunctionTableWithDescriptor:",reinterpret_cast<IMP>(noDescriptorResource),"@24@0:8@16"},
    {"newTiledTextureWithBytesNoCopy:length:descriptor:offset:bytesPerRow:",reinterpret_cast<IMP>(noTiledTexture),"@56@0:8^v16Q24@32Q40Q48"},
    {"newTiledTextureWithBytesNoCopy:length:deallocator:descriptor:offset:bytesPerRow:",reinterpret_cast<IMP>(noTiledTextureWithDeallocator),"@64@0:8^v16Q24@?32@40Q48Q56"},
};
}
unsigned RTXResourceFactoryMethodCount047(){return sizeof(entries)/sizeof(entries[0]);}
BOOL RTXInstallResourceFactories047(Class cls){
    Class base=objc_getClass("_MTLDevice");if(!cls||!base||class_getSuperclass(cls)!=base||objc_getClass(class_getName(cls))||class_getInstanceSize(base)!=712)return NO;
    const auto alignment=protocol_getMethodDescription(objc_getProtocol("MTLDevice"),sel_registerName("minimumTextureBufferAlignmentForPixelFormat:"),YES,YES);
    if(!alignment.name||!alignment.types||std::strcmp(alignment.types,"Q24@0:8Q16"))return NO;
    for(const auto &e:entries){
        Method inherited=class_getInstanceMethod(base,sel_registerName(e.name));
        if(!e.overridesInherited){if(inherited)return NO;continue;}
        const auto m=protocol_getMethodDescription(objc_getProtocol("MTLDevice"),sel_registerName(e.name),YES,YES);
        if(!m.name||!m.types||std::strcmp(m.types,e.encoding)||(inherited&&std::strcmp(method_getTypeEncoding(inherited),e.encoding)))return NO;
    }
    for(const auto &e:entries)if(!class_addMethod(cls,sel_registerName(e.name),e.imp,e.encoding))return NO;
    return YES;
}
