#import "RTXDeviceRegistration.h"
#import <Metal/Metal.h>

namespace {
char assertionsKey;
BOOL assertions(id device,SEL) {
    @synchronized(device) {
        return [objc_getAssociatedObject(device,&assertionsKey)boolValue];
    }
}
void setAssertions(id device,SEL,BOOL enabled) {
    // An object-local compilation preference, not a capability bit or proof
    // that the selected precompiled integer programs contain assertions.
    // NSObject owns association teardown; no inherited layout is extended.
    @synchronized(device) {
        objc_setAssociatedObject(device,&assertionsKey,@(enabled!=NO),OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
}
MTLSize sparseTile(id,SEL,MTLTextureType,MTLPixelFormat,NSUInteger) {
    // This device has no sparse texture resources or supported sparse tile.
    // The measured GFX9AMD implementation uses this same empty-size contract.
    return MTLSizeMake(0,0,0);
}
MTLSize sparseTilePage(id,SEL,MTLTextureType,MTLPixelFormat,NSUInteger,MTLSparsePageSize) {
    return MTLSizeMake(0,0,0);
}
struct Entry {const char *name;IMP imp;const char *encoding;};
const Entry entries[]={
    {"metalAssertionsEnabled",reinterpret_cast<IMP>(assertions),"c16@0:8"},
    {"setMetalAssertionsEnabled:",reinterpret_cast<IMP>(setAssertions),"v20@0:8c16"},
    {"sparseTileSizeWithTextureType:pixelFormat:sampleCount:",reinterpret_cast<IMP>(sparseTile),"{?=QQQ}40@0:8Q16Q24Q32"},
    {"sparseTileSizeWithTextureType:pixelFormat:sampleCount:sparsePageSize:",reinterpret_cast<IMP>(sparseTilePage),"{?=QQQ}48@0:8Q16Q24Q32q40"},
};
}
unsigned RTXDeviceRegistrationMethodCount051(void) {return sizeof(entries)/sizeof(entries[0]);}
BOOL RTXInstallDeviceRegistration051(Class cls) {
    Class base=objc_getClass("_MTLDevice");
    if(!cls||!base||class_getSuperclass(cls)!=base||objc_getClass(class_getName(cls))||class_getInstanceSize(base)!=712)return NO;
    for(const auto &entry:entries)if(class_getInstanceMethod(base,sel_registerName(entry.name)))return NO;
    for(const auto &entry:entries)if(!class_addMethod(cls,sel_registerName(entry.name),entry.imp,entry.encoding))return NO;
    return YES;
}
