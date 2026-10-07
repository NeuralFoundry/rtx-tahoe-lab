#import "RTXDeviceIdentity.h"
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include "RTXSoftwareLimits.hpp"

namespace {
// This bundle serves the fixed internal discrete 10de:2520 / 1043:104c target
// already checked by the real-port constructor. It currently exposes compute
// without a display path, unified physical memory, peer sharing or priority bands.
BOOL headless(id,SEL) { return YES; }
BOOL absent(id,SEL) { return NO; }
BOOL unsupportedCount(id,SEL,NSUInteger) { return NO; }
uint32_t noPeerIndexOrCount(id,SEL) { return 0; }
uint64_t noPeerGroup(id,SEL) { return 0; }

// The ACPI identity establishes the internal PCI device but does not establish
// the Metal slot/built-in location numbering. Unspecified is a real SDK enum;
// do not derive a slot number from the unrelated PCI bus/device/function tuple.
MTLDeviceLocation location(id,SEL) { return MTLDeviceLocationUnspecified; }
NSUInteger locationNumber(id,SEL) { return 0; } // No index for unspecified location.

// A conservative working-set budget of the implemented application arena.
// This is not the physical VRAM capacity or a persistent GPU residency claim.
uint64_t recommendedWorkingSet(id,SEL) { return RTXSoftware039::MaxArena; }
struct Entry {const char *name;IMP imp;const char *encoding;};
const Entry entries[]={
    {"isHeadless",reinterpret_cast<IMP>(headless),"c16@0:8"},
    {"hasUnifiedMemory",reinterpret_cast<IMP>(absent),"c16@0:8"},
    {"isLowPower",reinterpret_cast<IMP>(absent),"c16@0:8"},
    {"isRemovable",reinterpret_cast<IMP>(absent),"c16@0:8"},
    {"location",reinterpret_cast<IMP>(location),"Q16@0:8"},
    {"locationNumber",reinterpret_cast<IMP>(locationNumber),"Q16@0:8"},
    {"peerGroupID",reinterpret_cast<IMP>(noPeerGroup),"Q16@0:8"},
    {"peerCount",reinterpret_cast<IMP>(noPeerIndexOrCount),"I16@0:8"},
    {"peerIndex",reinterpret_cast<IMP>(noPeerIndexOrCount),"I16@0:8"},
    {"recommendedMaxWorkingSetSize",reinterpret_cast<IMP>(recommendedWorkingSet),"Q16@0:8"},
    {"isDepth24Stencil8PixelFormatSupported",reinterpret_cast<IMP>(absent),"c16@0:8"},
    {"supportPriorityBand",reinterpret_cast<IMP>(absent),"c16@0:8"},
    {"supportsSampleCount:",reinterpret_cast<IMP>(unsupportedCount),"c24@0:8Q16"},
    {"supportsVertexAmplificationCount:",reinterpret_cast<IMP>(unsupportedCount),"c24@0:8Q16"},
};
}
unsigned RTXDeviceIdentityMethodCount046() {return sizeof(entries)/sizeof(entries[0]);}
BOOL RTXInstallDeviceIdentity046(Class cls) {
    Class base=objc_getClass("_MTLDevice");
    if(!cls||!base||class_getSuperclass(cls)!=base||objc_getClass(class_getName(cls))||class_getInstanceSize(base)!=712)return NO;
    // All fourteen entries were absent in the measured base. A changed framework
    // implementation requires review instead of silently replacing new behavior.
    for(const auto &e:entries)if(class_getInstanceMethod(base,sel_registerName(e.name)))return NO;
    for(const auto &e:entries)if(!class_addMethod(cls,sel_registerName(e.name),e.imp,e.encoding))return NO;
    return YES;
}
