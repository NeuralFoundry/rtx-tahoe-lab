#import "RTXMetalCommand.h"
NS_ASSUME_NONNULL_BEGIN
// Explicit prototype factory for an ordinary, unsandboxed user process. This
// is not system Metal discovery. The reviewed container and live generation
// must match the root owner's immutable Hello descriptor exactly.
id<MTLDevice> _Nullable RTXCreateApplicationDevice(NSString *service,NSData *reviewedContainer,
 uint64_t generation,uint32_t timeoutMilliseconds,id<MTLLibrary> _Nullable * _Nonnull library,
 NSError * _Nullable * _Nullable error) NS_RETURNS_RETAINED;
void RTXCloseApplicationDevice(id<MTLDevice> device);
NSDictionary * _Nullable RTXCopyApplicationDeviceInfo(id<MTLDevice> device) NS_RETURNS_RETAINED;
NS_ASSUME_NONNULL_END
