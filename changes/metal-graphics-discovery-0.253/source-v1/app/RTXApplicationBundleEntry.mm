#import "RTXApplicationConstruction.h"
#import "RTXApplicationClient.h"
#import "RTXApplicationClientInternal.h"
#import <objc/runtime.h>
@interface _MTLDevice:NSObject
@end
@interface RTXMetalApplicationDevice209:_MTLDevice
@end
@implementation RTXMetalApplicationDevice209
+ (void)initialize {
 if(self==objc_getClass("RTXMetalApplicationDevice209")&&!RTXUseApplicationDeviceClass(self))
  [NSException raise:NSInternalInconsistencyException format:@"RTX application bundle class selection failed"];
}
@end
static uint64_t entryCount;
static Class selectedClass;
__attribute__((constructor)) static void initializeApplicationBundle(){
 ++entryCount;Class cls=[RTXMetalApplicationDevice209 class];
 if(RTXUseApplicationDeviceClass(cls))selectedClass=RTXApplicationDeviceClass();
}
extern "C" Class RTXApplicationBundleClass(void){return selectedClass;}
extern "C" uint64_t RTXApplicationBundleEntryCount(void){return entryCount;}
extern "C" id<MTLDevice> RTXApplicationBundleCreate(NSString *service,NSData *container,
 uint64_t generation,uint32_t timeout,id<MTLLibrary> *library,NSError **error){
 return RTXCreateApplicationDevice(service,container,generation,timeout,library,error);
}
// Explicit CPU test seam. Normal discovery still enters initWithAcceleratorPort.
extern "C" id<MTLDevice> RTXCreateOwnedApplication209(id<RTXOwnedChannel208> channel,NSData *container,uint64_t generation,id<MTLLibrary>*library,NSError**error){return RTXCreateApplicationDeviceWithChannel041(channel,container,generation,library,error);}
extern "C" void RTXCloseOwnedApplication209(id<MTLDevice> device){RTXCloseApplicationDevice(device);}
extern "C" NSDictionary*RTXCopyOwnedApplicationInfo209(id<MTLDevice> device){return RTXCopyApplicationDeviceInfo(device);}
