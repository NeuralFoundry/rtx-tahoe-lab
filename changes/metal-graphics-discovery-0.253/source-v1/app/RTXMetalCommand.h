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
@protocol RTXGraphicsCommandTransport248 <NSObject>
- (BOOL)executeGraphicsRequest:(NSData*)request libraryContainer:(NSData*)container
 result:(NSData* _Nullable * _Nonnull)result completion:(uint64_t*)completion
 error:(NSError* _Nullable * _Nonnull)error;
@end
// Private integration seam; one pristine command context, no device retain.
extern "C" BOOL RTXConfigureGraphicsCommandTransport248(id<MTLDevice>device,id<RTXGraphicsCommandTransport248>transport);
BOOL RTXConfigureGraphicsDevice251(id<MTLDevice>device,uint64_t generation,id<RTXGraphicsCommandTransport248>transport);
BOOL RTXInstallCommandMethods(Class unpublishedDeviceClass);
// One configuration for a device lifetime. Copies the reviewed library bytes;
// does not retain the library/device, open IOKit, upload code, or start firmware.
BOOL RTXConfigureCommandDevice(id<MTLDevice> device,id<MTLLibrary> library,
    uint64_t generation,id<RTXCommandTransport> transport,NSError * _Nullable * _Nullable error);
// Only a transport using the atomic resident-code/job protocol may opt in.
BOOL RTXConfigureResidentCommandDevice059(id<MTLDevice> device,id<MTLLibrary> library,
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
