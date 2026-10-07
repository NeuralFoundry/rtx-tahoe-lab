#import "RTXDeviceIdentity.h"
#import "RTXDeviceMemory107.h"
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include "RTXSoftwareLimits.hpp"
#include <cstring>
#include <initializer_list>

namespace {
// This bundle serves the fixed internal discrete 10de:2520 / 1043:104c target
// already checked by the real-port constructor. It currently exposes compute
// without a display path, unified physical memory, peer sharing or priority bands.
BOOL headless(id,SEL) { return YES; }
BOOL absent(id,SEL) { return NO; }
BOOL unsupportedCount(id,SEL,NSUInteger) { return NO; }
uint32_t noPeerIndexOrCount(id,SEL) { return 0; }
uint64_t noPeerGroup(id,SEL) { return 0; }
// Match the measured MTLIOAccelDevice fallback for LocationUnspecified: its
// updateGPUSelectionProperties leaves the initialized transfer rate at zero.
// This reports no classified transfer rate, not measured zero bandwidth and
// not a claim that the PCI device is marked built-in by the operating system.
uint64_t unclassifiedTransferRate(id,SEL) { return 0; }

// The ACPI identity establishes the internal PCI device but does not establish
// the Metal slot/built-in location numbering. Unspecified is a real SDK enum;
// do not derive a slot number from the unrelated PCI bus/device/function tuple.
MTLDeviceLocation location(id,SEL) { return MTLDeviceLocationUnspecified; }
NSUInteger locationNumber(id,SEL) { return 0; } // No index for unspecified location.

// A conservative working-set budget of the implemented application arena.
// This is not the physical VRAM capacity or a persistent GPU residency claim.
uint64_t recommendedWorkingSet(id,SEL) { return RTXSoftware039::MaxArena; }
// The installed GFX9AMD_MtlDevice base returns zero for all three private
// FP masks (reference120, exact machine-code review). Publish no extra private
// FP guarantees. Their bit semantics are not established; do not reuse OpenCL
// flags or the concrete AMD device's 60/60/126 masks. This does not change the
// separately implemented and tested restricted FP32 compiler/backend.
NSUInteger noPublishedFPConfig122(id,SEL) { return 0; }
// No allocatable GPU shared-memory pool is published by this backend.
// Application "shared" buffers are CPU arena storage: command submission takes
// a byte snapshot and the native backend copies it into its private GPU backing.
// Report this unavailable pool as zero. Neither system RAM, the CPU arena budget
// nor firmware DMA bookkeeping is an allocatable Metal GPU shared pool.
// This is not a measurement that the RTX hardware has zero host-memory access.
NSUInteger unpublishedSharedMemory124(id,SEL) { return 0; }
struct Entry {const char *name;IMP imp;const char *encoding;};
const Entry entries[]={
    {"sharedMemorySize",reinterpret_cast<IMP>(unpublishedSharedMemory124),"Q16@0:8"},
    {"halfFPConfig",reinterpret_cast<IMP>(noPublishedFPConfig122),"Q16@0:8"},
    {"singleFPConfig",reinterpret_cast<IMP>(noPublishedFPConfig122),"Q16@0:8"},
    {"doubleFPConfig",reinterpret_cast<IMP>(noPublishedFPConfig122),"Q16@0:8"},
    {"dedicatedMemorySize",reinterpret_cast<IMP>(RTXDedicatedMemory107),"Q16@0:8"},
    {"isHeadless",reinterpret_cast<IMP>(headless),"c16@0:8"},
    {"hasUnifiedMemory",reinterpret_cast<IMP>(absent),"c16@0:8"},
    {"isLowPower",reinterpret_cast<IMP>(absent),"c16@0:8"},
    {"isRemovable",reinterpret_cast<IMP>(absent),"c16@0:8"},
    {"maxTransferRate",reinterpret_cast<IMP>(unclassifiedTransferRate),"Q16@0:8"},
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
    // All entries were absent in the measured base. A changed framework
    // implementation requires review instead of silently replacing new behavior.
    Protocol *protocol=objc_getProtocol("MTLDevice");if(!protocol)return NO;
    const auto method=protocol_getMethodDescription(protocol,sel_registerName("maxTransferRate"),YES,YES);
    if(!method.name||!method.types||std::strcmp(method.types,"Q16@0:8"))return NO;
    Protocol *spi=objc_getProtocol("MTLDeviceSPI");if(!spi)return NO;
    const auto memory=protocol_getMethodDescription(spi,sel_registerName("dedicatedMemorySize"),YES,YES);
    if(!memory.name||!memory.types||std::strcmp(memory.types,"Q16@0:8"))return NO;
    for(const char *name:{"halfFPConfig","singleFPConfig","doubleFPConfig","sharedMemorySize"}){
        const auto fp=protocol_getMethodDescription(spi,sel_registerName(name),YES,YES);
        if(!fp.name||!fp.types||std::strcmp(fp.types,"Q16@0:8"))return NO;
    }
    for(const auto &e:entries)if(class_getInstanceMethod(base,sel_registerName(e.name)))return NO;
    for(const auto &e:entries)if(!class_addMethod(cls,sel_registerName(e.name),e.imp,e.encoding))return NO;
    return YES;
}
