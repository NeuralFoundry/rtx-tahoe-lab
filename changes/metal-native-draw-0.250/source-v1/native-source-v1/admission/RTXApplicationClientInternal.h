#import "RTXApplicationClient.h"
NS_ASSUME_NONNULL_BEGIN
// Internal exchange seam. Production uses only the real privileged-namespace
// XPC channel. Tests provide an explicitly labelled CPU channel, never GPU proof.
@protocol RTXBrokerChannel041 <NSObject>
- (BOOL)exchange:(NSData *)request reply:(NSData * _Nullable * _Nonnull)reply
 serverUID:(uint32_t *)uid serverPID:(int64_t *)pid;
- (void)cancel;
@end
id<RTXBrokerChannel041> _Nullable RTXNewXPCChannel041(NSString *service,uint32_t timeoutMilliseconds) NS_RETURNS_RETAINED;
id<MTLDevice> _Nullable RTXCreateApplicationDeviceWithChannel041(id<RTXBrokerChannel041> channel,
 NSData *container,uint64_t generation,id<MTLLibrary> _Nullable * _Nonnull library,
 NSError * _Nullable * _Nullable error) NS_RETURNS_RETAINED;
NS_ASSUME_NONNULL_END
