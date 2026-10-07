#import "RTXResourceFactories.h"
#import <Metal/Metal.h>
#import <objc/runtime.h>

namespace {
// These resource families are not implemented by the current compute backend.
// Their nullable creation methods must fail normally, without dispatching to
// an absent selector, claiming ownership of borrowed storage, or invoking its
// deallocator. Each IMP has the actual observed argument ABI; no generic varargs
// or objc forwarding catch-all is used.
id noDescriptorResource(id,SEL,id){return nil;}
id noFence(id,SEL){return nil;}
id noIndirectBuffer(id,SEL,id,NSUInteger,NSUInteger){return nil;}
id noSurfaceTexture(id,SEL,id,IOSurfaceRef,NSUInteger){return nil;}
id noTiledTexture(id,SEL,void *,NSUInteger,id,NSUInteger,NSUInteger){return nil;}
id noTiledTextureWithDeallocator(id,SEL,void *,NSUInteger,void(^)(void *,NSUInteger),id,NSUInteger,NSUInteger){return nil;}
struct Entry{const char *name;IMP imp;const char *encoding;};
const Entry entries[]={
    {"newIndirectCommandBufferWithDescriptor:maxCommandCount:options:",reinterpret_cast<IMP>(noIndirectBuffer),"@40@0:8@16Q24Q32"},
    {"newIndirectCommandBufferWithDescriptor:maxCount:options:",reinterpret_cast<IMP>(noIndirectBuffer),"@40@0:8@16Q24Q32"},
    {"newDepthStencilStateWithDescriptor:",reinterpret_cast<IMP>(noDescriptorResource),"@24@0:8@16"},
    {"newFence",reinterpret_cast<IMP>(noFence),"@16@0:8"},
    {"newHeapWithDescriptor:",reinterpret_cast<IMP>(noDescriptorResource),"@24@0:8@16"},
    {"newSamplerStateWithDescriptor:",reinterpret_cast<IMP>(noDescriptorResource),"@24@0:8@16"},
    {"newTextureWithDescriptor:",reinterpret_cast<IMP>(noDescriptorResource),"@24@0:8@16"},
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
    for(const auto &e:entries)if(class_getInstanceMethod(base,sel_registerName(e.name)))return NO;
    for(const auto &e:entries)if(!class_addMethod(cls,sel_registerName(e.name),e.imp,e.encoding))return NO;
    return YES;
}
