#import "RTXMetalBuffer.h"
NS_ASSUME_NONNULL_BEGIN
// A synchronous, bounded native owner transport. Implementations must establish
// completion and independently validate the full GPU capture before returning.
// The command layer never computes shader outputs or infers GPU proof from YES.
// Implementations must not retain this device (the device owns the transport).
@protocol RTXCommandTransport <NSObject>
- (BOOL)executeRequest:(NSData *)request libraryPayload:(NSData *)payload
               result:(NSData * _Nullable * _Nonnull)result
           completion:(uint64_t *)completion error:(NSError * _Nullable * _Nonnull)error;
@end
BOOL RTXInstallCommandMethods(Class unpublishedDeviceClass);
// One configuration for a device lifetime. Copies the reviewed library bytes;
// does not retain the library/device, open IOKit, upload code, or start firmware.
BOOL RTXConfigureCommandDevice(id<MTLDevice> device,id<MTLLibrary> library,
    uint64_t generation,id<RTXCommandTransport> transport,NSError * _Nullable * _Nullable error);
// Revokes new work and schedules draining, including uncommitted commands.
// This call does not wait for the worker or callbacks to finish.
// An in-flight callback must return before its resources can drain. This is
// host cancellation, not a proof of GPU quiescence or permission to free DMA.
void RTXCloseCommandDevice(id<MTLDevice> device);
// Atomically revoke only a pristine context with no queues or admitted calls.
// Unlike close, this proves there is no old command worker to race the broker.
BOOL RTXClaimIdleCommandDeviceForBroker(id<MTLDevice> device);
NSDictionary *RTXCopyCommandDeviceInfo(id<MTLDevice> device) NS_RETURNS_RETAINED;
NS_ASSUME_NONNULL_END
