#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

NS_ASSUME_NONNULL_BEGIN
// Driver-side creation after compiler provenance review. This is a compiled
// backend image, not a runtime MSL compiler and not an Apple .metallib decoder.
id<MTLLibrary> _Nullable RTXNewCompiledLibrary(id<MTLDevice> device, NSData *container, NSError * _Nullable * _Nullable error) NS_RETURNS_RETAINED;
id<MTLComputePipelineState> _Nullable RTXNewComputePipeline(id<MTLDevice> device, id<MTLFunction> function, NSError * _Nullable * _Nullable error) NS_RETURNS_RETAINED;
// Install the supported standard device entry points on a newly allocated,
// unregistered driver class. The caller registers it only after full setup.
BOOL RTXInstallLibraryMethods(Class deviceClass);
unsigned RTXLibraryMethodCount247();
extern "C" NSData * _Nullable RTXCopyRenderPipelineContainer247(id<MTLRenderPipelineState> pipeline) NS_RETURNS_RETAINED;

// Driver integration helpers. Copies keep code/data private and immutable;
// neither returns a GPU address or claims that a pipeline is GPU-resident.
NSData * _Nullable RTXCopyLibraryContainer(id<MTLLibrary> library) NS_RETURNS_RETAINED;
NSData * _Nullable RTXCopyLibraryPayload(id<MTLLibrary> library) NS_RETURNS_RETAINED;
NSData * _Nullable RTXCopyLibraryDigest(id<MTLLibrary> library) NS_RETURNS_RETAINED;
BOOL RTXGetPipelineProgram(id<MTLComputePipelineState> pipeline, id<MTLLibrary> library, NSUInteger * _Nullable index);
BOOL RTXValidatePipelineDispatch(id<MTLComputePipelineState> pipeline, MTLSize groups, MTLSize threads);
NSDictionary * _Nullable RTXCopyPipelineMetadata(id<MTLComputePipelineState> pipeline) NS_RETURNS_RETAINED;
// Exact private parent identity for the command layer; never guesses by name.
id<MTLLibrary> _Nullable RTXCopyPipelineLibrary(id<MTLComputePipelineState> pipeline) NS_RETURNS_RETAINED;
NS_ASSUME_NONNULL_END
