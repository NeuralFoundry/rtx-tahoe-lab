#import "RTXMetalLibrary.h"
#import "RTXBackendCompiler.h"
#include "RTXLibraryContainer.hpp"
#include <cstring>
#include <new>
#import <objc/runtime.h>

static NSError *libraryError(MTLLibraryError code,NSString *message){return [NSError errorWithDomain:MTLLibraryErrorDomain code:code userInfo:@{NSLocalizedDescriptionKey:message}];}
static id fail(NSError **error,MTLLibraryError code,NSString *message){if(error)*error=libraryError(code,message);return nil;}
@class RTXMetalFunction036;
@interface RTXMetalLibrary036 : NSObject <MTLLibrary> {
@public
 id<MTLDevice> _device;
 NSData *_container;
 NSArray<NSString *> *_names;
 RTXLibrary036::Catalog *_catalog;
 NSString *_label;
}
- (instancetype)initWithDevice:(id<MTLDevice>)device container:(NSData *)container error:(NSError **)error;
@property(nullable,copy,atomic) NSString *label;
@end
@interface RTXMetalFunction036 : NSObject <MTLFunction> {
@public
 RTXMetalLibrary036 *_library;
 NSUInteger _index;
 NSString *_label;
}
- (instancetype)initWithLibrary:(RTXMetalLibrary036 *)library index:(NSUInteger)index;
@property(nullable,copy,atomic) NSString *label;
@end
@interface RTXMetalPipeline036 : NSObject <MTLComputePipelineState> {
@public
 RTXMetalFunction036 *_function;
 id<MTLFunction> _nativeSource;
 NSString *_label;
}
- (instancetype)initWithFunction:(RTXMetalFunction036 *)function;
@end

@implementation RTXMetalLibrary036
@synthesize label=_label;
- (instancetype)init{[self release];return nil;}
- (instancetype)initWithDevice:(id<MTLDevice>)device container:(NSData *)container error:(NSError **)error {
 self=[super init];if(!self)return nil;if(error)*error=nil;
 if(!device||!container||container.length!=RTXLibrary036::Bytes){fail(error,MTLLibraryErrorCompileFailure,@"Invalid compiled RTX library length or device");[self release];return nil;}
 // Deep copy even for mutable NSData. All later names/code/metadata are owned.
 _container=[[NSData alloc]initWithBytes:container.bytes length:container.length];
 _catalog=new(std::nothrow) RTXLibrary036::Catalog;
 if(!_container||!_catalog||!RTXLibrary036::decode(static_cast<const uint8_t *>(_container.bytes),_container.length,*_catalog)){
  fail(error,MTLLibraryErrorCompileFailure,@"Invalid compiled RTX library container");[self release];return nil;
 }
 _device=[device retain];NSMutableArray *names=[NSMutableArray array];
 for(unsigned i=0;i<_catalog->library.count;++i)[names addObject:[NSString stringWithUTF8String:_catalog->names[i]]];
 _names=[names copy];return self;
}
- (void)dealloc {delete _catalog;[_container release];[_names release];[_device release];[_label release];[super dealloc];}
- (id<MTLDevice>)device{return _device;}
- (NSArray<NSString *> *)functionNames{return _names;}
- (MTLLibraryType)type{return MTLLibraryTypeExecutable;}
- (NSString *)installName{return nil;}
- (id<MTLFunction>)newFunctionWithName:(NSString *)name {
 if(![name isKindOfClass:[NSString class]])return nil;
 NSUInteger index=[_names indexOfObject:name];if(index==NSNotFound)return nil;
 return [[RTXMetalFunction036 alloc]initWithLibrary:self index:index];
}
- (id<MTLFunction>)newFunctionWithName:(NSString *)name constantValues:(MTLFunctionConstantValues *)values error:(NSError **)error {
 if(error)*error=nil;
 if(values)return fail(error,MTLLibraryErrorUnsupported,@"Function constant specialization is not implemented");
 id<MTLFunction> function=[self newFunctionWithName:name];
 return function?function:fail(error,MTLLibraryErrorFunctionNotFound,@"Function is absent from this RTX library");
}
- (void)newFunctionWithName:(NSString *)name constantValues:(MTLFunctionConstantValues *)values completionHandler:(void (^)(id<MTLFunction>,NSError *))handler {
 if(!handler)return;NSString *snapshot=[name copy];MTLFunctionConstantValues *constants=[values copy];
 dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{@autoreleasepool {
  NSError *error=nil;id<MTLFunction> function=[self newFunctionWithName:snapshot constantValues:constants error:&error];handler(function,error);[function release];
 }});[snapshot release];[constants release];
}
- (id<MTLFunction>)newFunctionWithDescriptor:(MTLFunctionDescriptor *)descriptor error:(NSError **)error {
 if(error)*error=nil;
 if(!descriptor)return fail(error,MTLLibraryErrorCompileFailure,@"A function descriptor is required");
 if(descriptor.specializedName||descriptor.options!=MTLFunctionOptionNone||descriptor.binaryArchives.count)return fail(error,MTLLibraryErrorUnsupported,@"Function specialization and binary linking are not implemented");
 return [self newFunctionWithName:descriptor.name constantValues:descriptor.constantValues error:error];
}
- (void)newFunctionWithDescriptor:(MTLFunctionDescriptor *)descriptor completionHandler:(void (^)(id<MTLFunction>,NSError *))handler {
 if(!handler)return;MTLFunctionDescriptor *snapshot=[descriptor copy];
 dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{@autoreleasepool {
  NSError *error=nil;id<MTLFunction> function=[self newFunctionWithDescriptor:snapshot error:&error];handler(function,error);[function release];
 }});[snapshot release];
}
- (id<MTLFunction>)newIntersectionFunctionWithDescriptor:(MTLIntersectionFunctionDescriptor *)descriptor error:(NSError **)error {
 (void)descriptor;return fail(error,MTLLibraryErrorUnsupported,@"Intersection functions are not implemented");
}
- (void)newIntersectionFunctionWithDescriptor:(MTLIntersectionFunctionDescriptor *)descriptor completionHandler:(void (^)(id<MTLFunction>,NSError *))handler {
 (void)descriptor;if(!handler)return;
 dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{@autoreleasepool {handler(nil,libraryError(MTLLibraryErrorUnsupported,@"Intersection functions are not implemented"));}});
}
- (MTLFunctionReflection *)reflectionForFunctionWithName:(NSString *)name {(void)name;return nil;}
@end

@implementation RTXMetalFunction036
@synthesize label=_label;
- (instancetype)init{[self release];return nil;}
- (instancetype)initWithLibrary:(RTXMetalLibrary036 *)library index:(NSUInteger)index {
 self=[super init];if(self){_library=[library retain];_index=index;}return self;
}
- (void)dealloc {[_library release];[_label release];[super dealloc];}
- (id<MTLDevice>)device{return _library.device;}
- (NSString *)name{return _library->_names[_index];}
- (MTLFunctionType)functionType{return MTLFunctionTypeKernel;}
- (MTLPatchType)patchType{return MTLPatchTypeNone;}
- (NSInteger)patchControlPointCount{return -1;}
- (NSArray<MTLVertexAttribute *> *)vertexAttributes{return nil;}
- (NSArray<MTLAttribute *> *)stageInputAttributes{return nil;}
- (NSDictionary<NSString *,MTLFunctionConstant *> *)functionConstantsDictionary{return @{};}
- (MTLFunctionOptions)options{return MTLFunctionOptionNone;}
- (id<MTLArgumentEncoder>)newArgumentEncoderWithBufferIndex:(NSUInteger)index {(void)index;return nil;}
- (id<MTLArgumentEncoder>)newArgumentEncoderWithBufferIndex:(NSUInteger)index reflection:(MTLAutoreleasedArgument *)reflection {(void)index;if(reflection)*reflection=nil;return nil;}
@end

@implementation RTXMetalPipeline036
- (instancetype)init{[self release];return nil;}
- (instancetype)initWithFunction:(RTXMetalFunction036 *)function {
 self=[super init];if(self){_function=[function retain];_label=[function.label copy];}return self;
}
- (void)dealloc {[_nativeSource release];[_function release];[_label release];[super dealloc];}
- (NSString *)label{return _label;}
- (id<MTLDevice>)device{return _function.device;}
- (NSUInteger)allocatedSize{return 0;} // No GPU allocation until the owning transport uploads this catalog.
- (NSUInteger)maxTotalThreadsPerThreadgroup{return _function->_library->_catalog->library.programs[_function->_index].localX;}
- (NSUInteger)threadExecutionWidth{return 32;}
- (NSUInteger)staticThreadgroupMemoryLength{return 0;}
- (BOOL)supportIndirectCommandBuffers{return NO;}
- (MTLResourceID)gpuResourceID{return MTLResourceID{0};}
- (MTLShaderValidation)shaderValidation{return MTLShaderValidationDisabled;}
- (MTLSize)requiredThreadsPerThreadgroup{return MTLSizeMake(self.maxTotalThreadsPerThreadgroup,1,1);}
- (MTLComputePipelineReflection *)reflection{return nil;}
- (NSUInteger)imageblockMemoryLengthForDimensions:(MTLSize)dimensions {(void)dimensions;return 0;}
- (id<MTLFunctionHandle>)functionHandleWithName:(NSString *)name {(void)name;return nil;}
- (id<MTLFunctionHandle>)functionHandleWithFunction:(id<MTLFunction>)function {(void)function;return nil;}
- (id<MTLFunctionHandle>)functionHandleWithBinaryFunction:(id<MTL4BinaryFunction>)function {(void)function;return nil;}
- (id<MTLComputePipelineState>)newComputePipelineStateWithAdditionalBinaryFunctions:(NSArray<id<MTLFunction>> *)functions error:(NSError **)error {(void)functions;return fail(error,MTLLibraryErrorUnsupported,@"Binary linking is not implemented");}
- (id<MTLComputePipelineState>)newComputePipelineStateWithBinaryFunctions:(NSArray<id<MTL4BinaryFunction>> *)functions error:(NSError **)error {(void)functions;return fail(error,MTLLibraryErrorUnsupported,@"Binary linking is not implemented");}
- (id<MTLVisibleFunctionTable>)newVisibleFunctionTableWithDescriptor:(MTLVisibleFunctionTableDescriptor *)descriptor {(void)descriptor;return nil;}
- (id<MTLIntersectionFunctionTable>)newIntersectionFunctionTableWithDescriptor:(MTLIntersectionFunctionTableDescriptor *)descriptor {(void)descriptor;return nil;}
@end

id<MTLLibrary> RTXNewCompiledLibrary(id<MTLDevice> device,NSData *container,NSError **error){return [[RTXMetalLibrary036 alloc]initWithDevice:device container:container error:error];}
id<MTLComputePipelineState> RTXNewComputePipeline(id<MTLDevice> device,id<MTLFunction> function,NSError **error){
 if(error)*error=nil;
 if([function class]==[RTXMetalFunction036 class]){
  if(!device||function.device!=device)return fail(error,MTLLibraryErrorCompileFailure,@"Function belongs to another device");
  return [[RTXMetalPipeline036 alloc]initWithFunction:(RTXMetalFunction036 *)function];
 }
 NSData *container=RTXCopyCompiledNativeLibrary056(device,function,error);if(!container)return nil;
 id<MTLLibrary> library=RTXNewCompiledLibrary(device,container,error);[container release];if(!library)return nil;
 id<MTLFunction> compiled=[library newFunctionWithName:function.name];[library release];
 if(!compiled)return fail(error,MTLLibraryErrorFunctionNotFound,@"Backend entry does not match native function");
 RTXMetalPipeline036 *pipeline=[[RTXMetalPipeline036 alloc]initWithFunction:(RTXMetalFunction036 *)compiled];[compiled release];
 if(pipeline)pipeline->_nativeSource=[function retain];return pipeline;
}
NSData *RTXCopyLibraryPayload(id<MTLLibrary> library){
 if([library class]!=[RTXMetalLibrary036 class])return nil;
 auto *l=(RTXMetalLibrary036 *)library;return [[NSData alloc]initWithBytes:static_cast<const uint8_t *>(l->_container.bytes)+640 length:4608];
}
extern "C" NSData *RTXCopyNativePipelineContainer056(id<MTLComputePipelineState> pipeline){
 if([pipeline class]!=[RTXMetalPipeline036 class])return nil;
 auto *p=(RTXMetalPipeline036 *)pipeline;if(!p->_nativeSource)return nil;
 return [p->_function->_library->_container copy];
}
NSData *RTXCopyLibraryDigest(id<MTLLibrary> library){
 if([library class]!=[RTXMetalLibrary036 class])return nil;
 return [[NSData alloc]initWithBytes:((RTXMetalLibrary036 *)library)->_catalog->payloadDigest length:32];
}
BOOL RTXGetPipelineProgram(id<MTLComputePipelineState> pipeline,id<MTLLibrary> library,NSUInteger *index){
 if(!index||[pipeline class]!=[RTXMetalPipeline036 class])return NO;auto *p=(RTXMetalPipeline036 *)pipeline;
 if(p->_function->_library!=library)return NO;*index=p->_function->_index;return YES;
}
BOOL RTXValidatePipelineDispatch(id<MTLComputePipelineState> pipeline,MTLSize groups,MTLSize threads){
 if([pipeline class]!=[RTXMetalPipeline036 class])return NO;auto *f=((RTXMetalPipeline036 *)pipeline)->_function;
 return RTXLibrary036::dispatch(*f->_library->_catalog,unsigned(f->_index),groups.width,groups.height,groups.depth,threads.width,threads.height,threads.depth);
}
id<MTLLibrary> RTXCopyPipelineLibrary(id<MTLComputePipelineState> pipeline){
 if([pipeline class]!=[RTXMetalPipeline036 class])return nil;
 return [((RTXMetalPipeline036 *)pipeline)->_function->_library retain];
}
NSDictionary *RTXCopyPipelineMetadata(id<MTLComputePipelineState> pipeline){
 if([pipeline class]!=[RTXMetalPipeline036 class])return nil;auto *f=((RTXMetalPipeline036 *)pipeline)->_function;const auto &p=f->_library->_catalog->library.programs[f->_index];
 NSMutableArray *bindings=[NSMutableArray array];for(unsigned i=0;i<p.parameters;++i)[bindings addObject:@(p.bindings[i])];
 return [@{@"program":@(f->_index),@"name":f.name,@"code_offset":@(p.offset),@"code_bytes":@(p.bytes),@"registers":@(p.registers),@"local_x":@(p.localX),@"bindings":bindings,@"read_mask":@(p.readMask),@"write_mask":@(p.writeMask),@"constant_bytes":@(p.constantBytes)} copy];
}
static id<MTLLibrary> deviceLibrary(id self,SEL,dispatch_data_t data,NSError **error){
 if(error)*error=nil;
 if(!data||dispatch_data_get_size(data)!=RTXLibrary036::Bytes)return fail(error,MTLLibraryErrorCompileFailure,@"Expected a compiled RTX backend library");
 const void *bytes=nullptr;size_t size=0;dispatch_data_t mapped=dispatch_data_create_map(data,&bytes,&size);
 if(!mapped||!bytes||size!=RTXLibrary036::Bytes){if(mapped)dispatch_release(mapped);return fail(error,MTLLibraryErrorInternal,@"Unable to map the compiled library");}
 NSData *copy=[[NSData alloc]initWithBytes:bytes length:size];dispatch_release(mapped);
 id<MTLLibrary> library=RTXNewCompiledLibrary(self,copy,error);[copy release];return library;
}
static id<MTLComputePipelineState> devicePipeline(id self,SEL,id<MTLFunction> function,NSError **error){return RTXNewComputePipeline(self,function,error);}
static void devicePipelineAsync(id self,SEL,id<MTLFunction> function,void (^handler)(id<MTLComputePipelineState>,NSError *)){
 if(!handler)return;
 dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{@autoreleasepool {
  NSError *error=nil;id<MTLComputePipelineState> pipeline=RTXNewComputePipeline(self,function,&error);handler(pipeline,error);[pipeline release];
 }});
}
BOOL RTXInstallLibraryMethods(Class deviceClass){
 if(!deviceClass||objc_getClass(class_getName(deviceClass)))return NO;
 struct Entry{const char *name;IMP implementation;const char *types;};
 const Entry entries[]={
  {"newLibraryWithData:error:",reinterpret_cast<IMP>(deviceLibrary),"@32@0:8@16^@24"},
  {"newComputePipelineStateWithFunction:error:",reinterpret_cast<IMP>(devicePipeline),"@32@0:8@16^@24"},
  {"newComputePipelineStateWithFunction:completionHandler:",reinterpret_cast<IMP>(devicePipelineAsync),"v32@0:8@16@?24"}};
 // The class is unpublished. A failed installation is discarded by its owner.
 for(const auto &entry:entries)if(!class_addMethod(deviceClass,sel_registerName(entry.name),entry.implementation,entry.types))return NO;
 return YES;
}

NSData *RTXCopyLibraryContainer(id<MTLLibrary> library){return [library class]==[RTXMetalLibrary036 class]?[((RTXMetalLibrary036 *)library)->_container copy]:nil;}
