#import "RTXDeviceContract.h"
#import "RTXMetalBuffer.h"
#import <objc/runtime.h>
#include <cstring>

namespace {
// General backend pipeline-task capacity remains unavailable; the native source
// frontend owns its independent process limit in RTXDeviceCompiler.mm.
NSUInteger compilationCapacity(id, SEL) { return 0; }

// There is currently no texture allocation, sampling or render path. These
// inherited flags must not advertise facilities that applications cannot use.
BOOL unsupportedTextureFeature(id, SEL) { return NO; }

NSUInteger currentAllocatedSize(id device, SEL) {
    NSDictionary *info = RTXCopyBufferArenaInfo(device);
    const NSUInteger bytes = [info[@"allocated_bytes"] unsignedIntegerValue];
    [info release];
    return bytes;
}
struct Entry { const char *selector; IMP implementation; const char *encoding; bool inherited; };
const Entry entries[] = {
    {"maxConcurrentExecutingCompilationTasks", reinterpret_cast<IMP>(compilationCapacity), "Q16@0:8", true},
    {"maximumConcurrentCompilationTaskCount", reinterpret_cast<IMP>(compilationCapacity), "Q16@0:8", true},
    {"supportsPlacementSparse", reinterpret_cast<IMP>(unsupportedTextureFeature), "c16@0:8", true},
    {"isBCTextureCompressionSupported", reinterpret_cast<IMP>(unsupportedTextureFeature), "c16@0:8", true},
    {"isFloat32FilteringSupported", reinterpret_cast<IMP>(unsupportedTextureFeature), "c16@0:8", true},
    {"isLargeMRTSupported", reinterpret_cast<IMP>(unsupportedTextureFeature), "c16@0:8", true},
    {"isMsaa32bSupported", reinterpret_cast<IMP>(unsupportedTextureFeature), "c16@0:8", true},
    {"currentAllocatedSize", reinterpret_cast<IMP>(currentAllocatedSize), "Q16@0:8", false},
};
}
unsigned RTXDeviceContractMethodCount045() { return sizeof(entries) / sizeof(entries[0]); }
BOOL RTXInstallDeviceContract045(Class cls) {
    Class base = objc_getClass("_MTLDevice");
    if (!cls || !base || class_getSuperclass(cls) != base ||
        objc_getClass(class_getName(cls)) || class_getInstanceSize(base) != 712) return NO;
    // Validate the observed ABI before adding anything to the unpublished class.
    for (const auto &e : entries) {
        Method method = class_getInstanceMethod(base, sel_registerName(e.selector));
        if (e.inherited && (!method || std::strcmp(method_getTypeEncoding(method), e.encoding))) return NO;
        if (!e.inherited && method) return NO;
    }
    for (const auto &e : entries)
        if (!class_addMethod(cls, sel_registerName(e.selector), e.implementation, e.encoding)) return NO;
    return YES;
}
