#import "RTXMetalBuffer.h"
NS_ASSUME_NONNULL_BEGIN
id _Nullable RTXNewOwnedBufferSubmission187(id<MTLComputePipelineState> pipeline,id<MTLLibrary> library,const id<MTLBuffer> _Nonnull * _Nullable buffers,const NSUInteger* _Nullable indices,const NSUInteger* _Nullable offsets,NSUInteger count,MTLSize groups,MTLSize threads,uint64_t generation,uint64_t serial,NSError* _Nullable * _Nullable error) NS_RETURNS_RETAINED;
BOOL RTXIsOwnedBufferSubmission187(id object);
NSData* _Nullable RTXCopyOwnedBufferRequest187(id object) NS_RETURNS_RETAINED;
BOOL RTXOwnedBufferTerminal187(id object);
void RTXCancelOwnedBufferSubmission187(id object);
BOOL RTXPublishOwnedBufferSubmission187(id object,NSData*request,NSData*result,uint64_t completion);
NS_ASSUME_NONNULL_END
