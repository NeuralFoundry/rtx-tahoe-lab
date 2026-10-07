#import "RTXApplicationConstruction.h"
#import "RTXApplicationClient.h"
#import <objc/runtime.h>
@interface _MTLDevice:NSObject
@end
@interface RTXMetalApplicationDevice059:_MTLDevice
@end
@implementation RTXMetalApplicationDevice059
+ (void)initialize {
 if(self==objc_getClass("RTXMetalApplicationDevice059")&&!RTXUseApplicationDeviceClass(self))
  [NSException raise:NSInternalInconsistencyException format:@"RTX application bundle class selection failed"];
}
@end
static uint64_t entryCount;
static Class selectedClass;
__attribute__((constructor)) static void initializeApplicationBundle(){
 ++entryCount;Class cls=[RTXMetalApplicationDevice059 class];
 if(RTXUseApplicationDeviceClass(cls))selectedClass=RTXApplicationDeviceClass();
}
extern "C" Class RTXApplicationBundleClass(void){return selectedClass;}
extern "C" uint64_t RTXApplicationBundleEntryCount(void){return entryCount;}
extern "C" id<MTLDevice> RTXApplicationBundleCreate(NSString *service,NSData *container,
 uint64_t generation,uint32_t timeout,id<MTLLibrary> *library,NSError **error){
 return RTXCreateApplicationDevice(service,container,generation,timeout,library,error);
}
