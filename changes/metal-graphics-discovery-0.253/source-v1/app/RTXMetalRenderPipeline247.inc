// Standard Metal pipeline objects for the reviewed vertex/fragment pair.
// Construction owns immutable code and descriptors; it submits no GPU work.
@interface RTXApplication209_RTXRenderPipeline247:NSObject<MTLRenderPipelineState>{
 @public MTLRenderPipelineDescriptor *_descriptor;
}
- (instancetype)initWithDescriptor:(MTLRenderPipelineDescriptor*)descriptor;
@end
@implementation RTXApplication209_RTXRenderPipeline247
- (instancetype)init{[self release];return nil;}
- (instancetype)initWithDescriptor:(MTLRenderPipelineDescriptor*)descriptor{if((self=[super init]))_descriptor=[descriptor copy];return self;}
- (void)dealloc{[_descriptor release];[super dealloc];}
- (NSString*)label{return _descriptor.label;}
- (id<MTLDevice>)device{return _descriptor.vertexFunction.device;}
- (NSUInteger)allocatedSize{return 0;}
- (MTLRenderPipelineReflection*)reflection{return nil;}
- (NSUInteger)maxTotalThreadsPerThreadgroup{return 0;}
- (BOOL)threadgroupSizeMatchesTileSize{return NO;}
- (NSUInteger)imageblockSampleLength{return 0;}
- (NSUInteger)imageblockMemoryLengthForDimensions:(MTLSize)dimensions{(void)dimensions;return 0;}
- (BOOL)supportIndirectCommandBuffers{return NO;}
- (NSUInteger)maxTotalThreadsPerObjectThreadgroup{return 0;}
- (NSUInteger)maxTotalThreadsPerMeshThreadgroup{return 0;}
- (NSUInteger)objectThreadExecutionWidth{return 0;}
- (NSUInteger)meshThreadExecutionWidth{return 0;}
- (NSUInteger)maxTotalThreadgroupsPerMeshGrid{return 0;}
- (MTLResourceID)gpuResourceID{return MTLResourceID{0};}
- (MTLShaderValidation)shaderValidation{return MTLShaderValidationDisabled;}
- (MTLSize)requiredThreadsPerTileThreadgroup{return MTLSizeMake(0,0,0);}
- (MTLSize)requiredThreadsPerObjectThreadgroup{return MTLSizeMake(0,0,0);}
- (MTLSize)requiredThreadsPerMeshThreadgroup{return MTLSizeMake(0,0,0);}
- (id<MTLFunctionHandle>)functionHandleWithName:(NSString*)name stage:(MTLRenderStages)stage{(void)name;(void)stage;return nil;}
- (id<MTLFunctionHandle>)functionHandleWithFunction:(id<MTLFunction>)function stage:(MTLRenderStages)stage{(void)function;(void)stage;return nil;}
- (id<MTLFunctionHandle>)functionHandleWithBinaryFunction:(id<MTL4BinaryFunction>)function stage:(MTLRenderStages)stage{(void)function;(void)stage;return nil;}
- (id<MTLVisibleFunctionTable>)newVisibleFunctionTableWithDescriptor:(MTLVisibleFunctionTableDescriptor*)descriptor stage:(MTLRenderStages)stage{(void)descriptor;(void)stage;return nil;}
- (id<MTLIntersectionFunctionTable>)newIntersectionFunctionTableWithDescriptor:(MTLIntersectionFunctionTableDescriptor*)descriptor stage:(MTLRenderStages)stage{(void)descriptor;(void)stage;return nil;}
- (id<MTLRenderPipelineState>)newRenderPipelineStateWithAdditionalBinaryFunctions:(MTLRenderPipelineFunctionsDescriptor*)functions error:(NSError**)error{(void)functions;return fail(error,MTLLibraryErrorUnsupported,@"Render binary linking is not implemented");}
- (id<MTLRenderPipelineState>)newRenderPipelineStateWithBinaryFunctions:(MTL4RenderPipelineBinaryFunctionsDescriptor*)functions error:(NSError**)error{(void)functions;return fail(error,MTLLibraryErrorUnsupported,@"Render binary linking is not implemented");}
- (MTL4PipelineDescriptor*)newRenderPipelineDescriptorForSpecialization{[NSException raise:NSInvalidArgumentException format:@"Render pipeline specialization is not implemented"];return nil;}
@end

static bool noLinkedFunctions247(MTLLinkedFunctions*f){return !f||(!f.functions.count&&!f.binaryFunctions.count&&!f.groups.count&&!f.privateFunctions.count);}
static bool layout247(MTLVertexDescriptor*v){
 if(!v)return false;
 for(NSUInteger n=0;n<31;++n){auto*a=v.attributes[n];auto*b=v.layouts[n];
  if(n<2){if(a.format!=MTLVertexFormatFloat2||a.offset!=n*8||a.bufferIndex!=0)return false;}
  else if(a.format!=MTLVertexFormatInvalid)return false;
  if(!n){if(b.stride!=16||b.stepFunction!=MTLVertexStepFunctionPerVertex||b.stepRate!=1)return false;}
  else if(b.stride)return false;
 }return true;
}
static id<MTLRenderPipelineState> newRenderPipeline247(id<MTLDevice>device,MTLRenderPipelineDescriptor*input,NSError**error){
 if(error)*error=nil;
 if(!device||![input isKindOfClass:[MTLRenderPipelineDescriptor class]])return fail(error,MTLLibraryErrorCompileFailure,@"A render pipeline descriptor and device are required");
 // Metal descriptors are mutable. Every decision and retained property uses
 // one snapshot, including nested vertex and color attachment descriptors.
 MTLRenderPipelineDescriptor*d=[[input copy]autorelease];
 auto*v=(RTXApplication209_RTXMetalFunction036*)d.vertexFunction;auto*f=(RTXApplication209_RTXMetalFunction036*)d.fragmentFunction;
 if([v class]!=[RTXApplication209_RTXMetalFunction036 class]||[f class]!=[RTXApplication209_RTXMetalFunction036 class]||
    !v->_library->_graphics||!f->_library->_graphics||v->_index!=0||f->_index!=1||v.device!=device||f.device!=device||v->_library!=f->_library)
  return fail(error,MTLLibraryErrorCompileFailure,@"The render pipeline needs the reviewed vertex/fragment pair from one library on this device");
 if(!layout247(d.vertexDescriptor))return fail(error,MTLLibraryErrorUnsupported,@"This graphics program requires float2 position and UV attributes in a 16-byte vertex record");
 if(d.rasterSampleCount!=1||d.alphaToCoverageEnabled||d.alphaToOneEnabled||!d.rasterizationEnabled||d.maxVertexAmplificationCount!=1||d.supportIndirectCommandBuffers||
    d.depthAttachmentPixelFormat!=MTLPixelFormatInvalid||d.stencilAttachmentPixelFormat!=MTLPixelFormatInvalid||
    (d.inputPrimitiveTopology!=MTLPrimitiveTopologyClassUnspecified&&d.inputPrimitiveTopology!=MTLPrimitiveTopologyClassTriangle)||d.shaderValidation==MTLShaderValidationEnabled)
  return fail(error,MTLLibraryErrorUnsupported,@"This render pipeline requires single-sample triangles without depth, stencil, amplification or indirect commands");
 if(d.binaryArchives.count||d.vertexPreloadedLibraries.count||d.fragmentPreloadedLibraries.count||d.supportAddingVertexBinaryFunctions||d.supportAddingFragmentBinaryFunctions||
    !noLinkedFunctions247(d.vertexLinkedFunctions)||!noLinkedFunctions247(d.fragmentLinkedFunctions))return fail(error,MTLLibraryErrorUnsupported,@"Render function linking is not implemented");
 for(NSUInteger n=0;n<8;++n){auto*a=d.colorAttachments[n];
  if((n==0?(a.pixelFormat!=MTLPixelFormatRGBA8Unorm||a.writeMask!=MTLColorWriteMaskAll):(a.pixelFormat!=MTLPixelFormatInvalid))||a.blendingEnabled)
   return fail(error,MTLLibraryErrorUnsupported,@"The reviewed render path requires one RGBA8Unorm color attachment with all channels enabled and blending disabled");
 }
 return [[RTXApplication209_RTXRenderPipeline247 alloc]initWithDescriptor:d];
}
extern "C" NSData *RTXCopyRenderPipelineContainer247(id<MTLRenderPipelineState>pipeline){
 if([pipeline class]!=[RTXApplication209_RTXRenderPipeline247 class])return nil;
 auto*f=(RTXApplication209_RTXMetalFunction036*)((RTXApplication209_RTXRenderPipeline247*)pipeline)->_descriptor.vertexFunction;
 return [f->_library->_container copy];
}
static id<MTLRenderPipelineState> deviceRender247(id self,SEL,MTLRenderPipelineDescriptor*d,NSError**error){return newRenderPipeline247(self,d,error);}
static id<MTLRenderPipelineState> deviceRenderOptions247(id self,SEL,MTLRenderPipelineDescriptor*d,MTLPipelineOption options,MTLAutoreleasedRenderPipelineReflection*reflection,NSError**error){
 if(reflection)*reflection=nil;if(error)*error=nil;
 if(options!=MTLPipelineOptionNone)return fail(error,MTLLibraryErrorUnsupported,@"Render reflection and pipeline options are not implemented");
 return newRenderPipeline247(self,d,error);
}
static void deviceRenderAsync247(id self,SEL,MTLRenderPipelineDescriptor*d,MTLNewRenderPipelineStateCompletionHandler handler){
 if(!handler)return;MTLRenderPipelineDescriptor*snapshot=[d copy];
 dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{@autoreleasepool{NSError*error=nil;id<MTLRenderPipelineState>p=newRenderPipeline247(self,snapshot,&error);handler(p,error);[p release];}});[snapshot release];
}
static void deviceRenderOptionsAsync247(id self,SEL,MTLRenderPipelineDescriptor*d,MTLPipelineOption options,MTLNewRenderPipelineStateWithReflectionCompletionHandler handler){
 if(!handler)return;MTLRenderPipelineDescriptor*snapshot=[d copy];
 dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{@autoreleasepool{NSError*error=nil;id<MTLRenderPipelineState>p=options==MTLPipelineOptionNone?newRenderPipeline247(self,snapshot,&error):fail(&error,MTLLibraryErrorUnsupported,@"Render reflection and pipeline options are not implemented");handler(p,nil,error);[p release];}});[snapshot release];
}
static BOOL RTXInstallRenderPipelineMethods247(Class cls){
 struct Entry{const char*name;IMP imp;};
 const Entry entries[]={
  {"newRenderPipelineStateWithDescriptor:error:",reinterpret_cast<IMP>(deviceRender247)},
  {"newRenderPipelineStateWithDescriptor:options:reflection:error:",reinterpret_cast<IMP>(deviceRenderOptions247)},
  {"newRenderPipelineStateWithDescriptor:completionHandler:",reinterpret_cast<IMP>(deviceRenderAsync247)},
  {"newRenderPipelineStateWithDescriptor:options:completionHandler:",reinterpret_cast<IMP>(deviceRenderOptionsAsync247)}};
 Class base=objc_getClass("_MTLDevice");if(!base)return NO;
 for(const auto&e:entries){SEL selector=sel_registerName(e.name);Method source=class_getInstanceMethod(base,selector);if(!source||!class_addMethod(cls,selector,e.imp,method_getTypeEncoding(source)))return NO;}
 return YES;
}
