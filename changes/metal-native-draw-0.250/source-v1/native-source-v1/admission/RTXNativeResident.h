#import "RTXMetalCommand.h"
NS_ASSUME_NONNULL_BEGIN
// Same root-owned connection and serialized transport as executeRequest.
// The broker must admit compiler provenance before passing a new payload.
@protocol RTXResidentCommandTransport058 <RTXCommandTransport>
- (BOOL)replaceLibraryPayload:(NSData *)payload expectedEpoch:(uint64_t)epoch
          expectedCompleted:(uint64_t)completed newEpoch:(uint64_t *)newEpoch
                      error:(NSError * _Nullable * _Nonnull)error;
@end
#ifdef __cplusplus
extern "C" {
#endif
// Available only after the idle command context has transferred to the broker.
// No raw selectors or GPU addresses are exposed. Caller must hold root authority
// and admit this exact compiler container before calling; no automatic retry.
uint32_t rtx_native_resident_replace(const void * _Nullable container,size_t bytes,
    uint64_t expectedEpoch,uint64_t expectedCompleted,uint64_t * _Nullable newEpoch);
#ifdef __cplusplus
}
#endif
NS_ASSUME_NONNULL_END
