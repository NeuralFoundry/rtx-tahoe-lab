#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

NS_ASSUME_NONNULL_BEGIN
@class RTXApplication209_RTXHostBuffer, RTXApplication209_RTXHostSubmission;

// Internal CPU backing for the future Metal resource. It is not declared to
// conform to MTLBuffer: no GPU mapping or stable GPU address exists yet.
// An arena belongs to one actual device object and survives while buffers do.
@interface RTXApplication209_RTXBufferArena : NSObject
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
- (nullable instancetype)initWithDevice:(nullable id)device budget:(NSUInteger)budget;
- (nullable RTXApplication209_RTXHostBuffer *)newBufferWithLength:(NSUInteger)length;
- (nullable RTXApplication209_RTXHostBuffer *)newBufferWithBytes:(nullable const void *)bytes length:(NSUInteger)length;
// Internal copy-transport request for the existing 64-element, three-program
// ABI. All buffers must belong to this arena and be distinct. This does not
// allocate GPU memory or claim the standard Metal resource/command protocols.
- (nullable RTXApplication209_RTXHostSubmission *)newSubmissionWithInputA:(RTXApplication209_RTXHostBuffer *)a offsetA:(NSUInteger)oa
    inputB:(RTXApplication209_RTXHostBuffer *)b offsetB:(NSUInteger)ob output:(RTXApplication209_RTXHostBuffer *)output offset:(NSUInteger)oo
    generation:(uint64_t)generation requestID:(uint64_t)requestID program:(uint32_t)program;
- (void)close;
@property(readonly,retain) id device;
@property(readonly) BOOL closed;
@property(readonly) NSUInteger allocatedBytes;
@property(readonly) NSUInteger liveBuffers;
@end

@interface RTXApplication209_RTXHostBuffer : NSObject
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@property(readonly) NSUInteger length;
@property(readonly) NSUInteger allocatedSize;
@property(readonly,retain) id device;
@property(nullable,copy,atomic) NSString *label;
// Copies are synchronized and reject closed arenas or overflowed ranges.
// Upload snapshots are independent immutable copies, so later CPU writes and
// early release cannot alter bytes already handed to a submission builder.
- (BOOL)writeBytes:(nullable const void *)bytes range:(NSRange)range;
- (nullable NSData *)newUploadSnapshotForDevice:(id)device range:(NSRange)range;
// Publish a complete verified output atomically from the CPU transport. No GPU
// completion is inferred by this method; the caller must supply that evidence.
- (BOOL)publishReadback:(NSData *)data range:(NSRange)range;
- (nullable NSData *)newCPUReadbackForRange:(NSRange)range;
@end

@interface RTXApplication209_RTXHostSubmission : NSObject
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@property(readonly,retain) id device;
@property(readonly,copy) NSData *request;
@property(readonly) BOOL terminal;
// The caller must independently verify the GPU capture before publishing.
// Identity/fence checks here bind that transport result to its retained output;
// they are not themselves proof of GPU execution. A concurrent CPU write or
// arena close makes publication fail without overwriting the newer contents.
- (BOOL)publishTransportReadback:(NSData *)data request:(NSData *)request completion:(uint64_t)completion;
- (void)cancel;
@end
NS_ASSUME_NONNULL_END
