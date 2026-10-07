#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import "RTXMetalLibrary.h"
NS_ASSUME_NONNULL_BEGIN
// Adds standard shared-buffer creation methods to an unpublished driver class.
// No device registration, GPU VA or GPU mapping is created here.
BOOL RTXInstallBufferMethods(Class deviceClass);
NSDictionary *RTXCopyBufferArenaInfo(id<MTLDevice> device) NS_RETURNS_RETAINED;
void RTXCloseBufferArena(id<MTLDevice> device);

// Commit-time immutable staging transaction. The command layer calls this
// after earlier commands complete, retaining all bound resources until finish.
// Sparse Metal binding indices are resolved through the selected pipeline.
id _Nullable RTXNewBufferSubmission(id<MTLComputePipelineState> pipeline,id<MTLLibrary> library,
    const id<MTLBuffer> _Nonnull * _Nullable buffers,const NSUInteger * _Nullable indices,
    const NSUInteger * _Nullable offsets,NSUInteger count,MTLSize groups,MTLSize threads,
    uint64_t generation,uint64_t serial,NSError * _Nullable * _Nullable error) NS_RETURNS_RETAINED;
NSData * _Nullable RTXCopyBufferSubmissionRequest(id submission) NS_RETURNS_RETAINED;
BOOL RTXBufferSubmissionTerminal(id submission);
void RTXCancelBufferSubmission(id submission);
// The transport must establish actual GPU completion and verify the result
// before calling this helper. Identity checks here are not GPU execution proof.
BOOL RTXPublishBufferSubmission(id submission,NSData *request,NSData *result,uint64_t completion);
NS_ASSUME_NONNULL_END
