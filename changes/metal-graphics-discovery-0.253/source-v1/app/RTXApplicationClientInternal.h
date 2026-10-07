#import "RTXApplicationClient.h"
#import "RTXOwnedBrokerTransport208.h"
NS_ASSUME_NONNULL_BEGIN
// Internal exchange seam. Production uses only the real privileged-namespace
// XPC channel. Tests provide an explicitly labelled CPU channel, never GPU proof.
id<MTLDevice> _Nullable RTXCreateApplicationDeviceWithChannel041(id<RTXOwnedChannel208> channel,
 NSData *container,uint64_t generation,id<MTLLibrary> _Nullable * _Nonnull library,
 NSError * _Nullable * _Nullable error) NS_RETURNS_RETAINED;
NS_ASSUME_NONNULL_END
