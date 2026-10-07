#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#ifdef __cplusplus
extern "C" {
#endif

// Installs real limit/family methods on an unpublished direct _MTLDevice
// subclass, after validating the measured base layout. Does not open IOKit,
// call MTLAddDevice, add protocol conformance or advertise a GPU family.
BOOL RTXInstallDeviceLimits(Class cls);
unsigned RTXDeviceLimitMethodCount(void);
BOOL RTXDeviceLimitsRuntimeCompatible(void);
BOOL RTXInitializeDeviceLimits(id device);
NSData *RTXCopyDeviceLimits(id device) NS_RETURNS_RETAINED;
#ifdef __cplusplus
}
#endif
