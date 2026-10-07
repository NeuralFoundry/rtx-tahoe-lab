#import "RTXDeviceConstruction.h"
#include <stdint.h>

// This private superclass symbol was resolved and matched to the actual class
// on the pinned test OS. The separate device-limit module validates the exact
// runtime layout before initializing this subclass's own inherited limits.
@interface _MTLDevice : NSObject
@end
@interface RTXMetalNativeOwner108 : _MTLDevice
@end
@implementation RTXMetalNativeOwner108
+ (void)initialize {
 if(self==objc_getClass("RTXMetalNativeOwner108")&&!RTXUseNativeDeviceClass(self))
  [NSException raise:NSInternalInconsistencyException format:@"RTX bundle class installation failed"];
}
@end

static uint64_t entryCount;
static Class nativeClass;
__attribute__((constructor)) static void initializeRTXMetalBundle(){
 ++entryCount;Class cls=[RTXMetalNativeOwner108 class];
 if(RTXUseNativeDeviceClass(cls))nativeClass=RTXNativeDeviceClass();
}
extern "C" __attribute__((visibility("default"))) Class RTXMetalBundleClass(){return nativeClass;}
extern "C" __attribute__((visibility("default"))) uint64_t RTXMetalBundleEntryCount(){return entryCount;}
extern "C" __attribute__((visibility("default"))) NSDictionary *RTXMetalBundleCopyConstructionInfo(){return RTXCopyNativeConstructionInfo();}
#import "RTXMetalBuffer.h"
extern "C" __attribute__((visibility("default"))) NSDictionary *RTXLimitsTestCopyArenaInfo(id device){return RTXCopyBufferArenaInfo(device);}
