// Included in the backing implementation so one mutex protects all resources
// in a transaction. The device owns a borrowing arena; each buffer retains the
// device. The arena does not retain its buffers, avoiding a device/arena cycle.
static char metalArenaKey;
static NSError *bufferError(NSString *reason){return [NSError errorWithDomain:@"RTXBufferSubmission" code:1 userInfo:@{NSLocalizedDescriptionKey:reason}];}
static id bufferFailure(NSError **error,NSString *reason){if(error)*error=bufferError(reason);return nil;}
static RTXApplication209_RTXBufferArena *metalArena(id device,bool create){
 @synchronized(device){
  RTXApplication209_RTXBufferArena *arena=objc_getAssociatedObject(device,&metalArenaKey);
  if(!arena&&create){
   arena=[[RTXApplication209_RTXBufferArena alloc]initWithDevice:device budget:MaxArena];
   if(!arena)return nil;
   [arena->_device release];arena->_borrowsDevice=true;arena->_metalBuffers=true;
   objc_setAssociatedObject(device,&metalArenaKey,arena,OBJC_ASSOCIATION_RETAIN_NONATOMIC);[arena release];
  }
  return arena;
 }
}
#include "RTXMetalHeap117.inc"
@implementation RTXApplication209_RTXMetalBuffer036
- (id<MTLDevice>)device{return _retainedDevice;}
- (void *)contents{std::lock_guard<std::mutex> lock(_arena->_state->mutex);return _heap&&!bufferAvailable(self)?nullptr:_bytes;}
- (MTLCPUCacheMode)cpuCacheMode{return MTLCPUCacheModeDefaultCache;}
- (MTLStorageMode)storageMode{return MTLStorageModeShared;}
- (MTLHazardTrackingMode)hazardTrackingMode{return MTLHazardTrackingModeTracked;}
- (MTLResourceOptions)resourceOptions{return MTLResourceStorageModeShared|MTLResourceHazardTrackingModeTracked;}
- (MTLPurgeableState)setPurgeableState:(MTLPurgeableState)value{
 if(value<MTLPurgeableStateKeepCurrent||value>MTLPurgeableStateEmpty)[NSException raise:NSInvalidArgumentException format:@"Invalid purgeable state"];
 std::lock_guard<std::mutex> lock(_arena->_state->mutex);const auto prior=_purge;
 if(_heap){
  if(value!=MTLPurgeableStateKeepCurrent)[NSException raise:NSInvalidArgumentException format:@"Heap resource purgeability belongs to its heap"];
  return _heap->_purge;
 }
 if(value!=MTLPurgeableStateKeepCurrent){
  if(value==MTLPurgeableStateEmpty){std::memset(_bytes,0,_allocated);if(_revision!=std::numeric_limits<uint64_t>::max())++_revision;}
  _purge=value;
 }
 return value==MTLPurgeableStateKeepCurrent?_purge:prior;
}
- (id<MTLHeap>)heap{return _heap;}
- (NSUInteger)heapOffset{return _heap?_heapAllocation.offset:0;}
- (void)makeAliasable{
 std::lock_guard<std::mutex> lock(_arena->_state->mutex);
 if(!_heap)[NSException raise:NSInvalidArgumentException format:@"This buffer is not allocated from a heap"];
 if(_aliasable)return;
 if(_pendingOutput)[NSException raise:NSInvalidArgumentException format:@"Pending GPU output cannot be aliased"];
 if(!_heap->_layout->release(_heapAllocation))[NSException raise:NSInternalInconsistencyException format:@"Heap allocation identity mismatch"];
 _aliasable=true;
}
- (BOOL)isAliasable{std::lock_guard<std::mutex> lock(_arena->_state->mutex);return _aliasable;}
- (kern_return_t)setOwnerWithIdentity:(task_id_token_t)token{(void)token;return KERN_NOT_SUPPORTED;}
- (void)didModifyRange:(NSRange)range{(void)range;[NSException raise:NSInvalidArgumentException format:@"didModifyRange applies to managed storage, not this shared buffer"];}
- (id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor offset:(NSUInteger)offset bytesPerRow:(NSUInteger)row{return textureFromBuffer224(self,descriptor,offset,row,true);}
- (id<MTLTensor>)newTensorWithDescriptor:(MTLTensorDescriptor *)descriptor offset:(NSUInteger)offset error:(NSError **)error{
 (void)descriptor;(void)offset;return bufferFailure(error,@"Tensor views are not implemented");
}
- (void)addDebugMarker:(NSString *)marker range:(NSRange)range{
 if(!marker||!rangeOK(range,_length))[NSException raise:NSInvalidArgumentException format:@"Invalid buffer debug marker range"];
 // Debug annotations are optional and are not submitted to this backend.
}
- (void)removeAllDebugMarkers{}
- (id<MTLBuffer>)remoteStorageBuffer{return nil;}
- (id<MTLBuffer>)newRemoteBufferViewForDevice:(id<MTLDevice>)device{(void)device;return nil;}
- (MTLGPUAddress)gpuAddress{return 0;} // Staging backing has no persistent GPU VA.
- (MTLBufferSparseTier)sparseBufferTier{return MTLBufferSparseTierNone;}
@end
static bool sharedOptions(MTLResourceOptions options){return options==MTLResourceStorageModeShared||options==(MTLResourceStorageModeShared|MTLResourceHazardTrackingModeTracked);}
static id<MTLBuffer> metalNewBuffer(id device,SEL,NSUInteger length,MTLResourceOptions options){
 if(!sharedOptions(options))return nil;RTXApplication209_RTXBufferArena *arena=metalArena(device,true);return (id<MTLBuffer>)[arena newBufferWithLength:length];
}
static id<MTLBuffer> metalNewBufferBytes(id device,SEL,const void *bytes,NSUInteger length,MTLResourceOptions options){
 if(!bytes||!sharedOptions(options))return nil;RTXApplication209_RTXBufferArena *arena=metalArena(device,true);return (id<MTLBuffer>)[arena newBufferWithBytes:bytes length:length];
}
static id<MTLBuffer> metalNewBufferNoCopy(id,SEL,void *,NSUInteger,MTLResourceOptions,void (^)(void *,NSUInteger)){return nil;}
namespace {
 struct BufferMethodEntry117{const char *name;IMP implementation;const char *types;};
 const BufferMethodEntry117 bufferMethods117[]={
  {"newBufferWithLength:options:",reinterpret_cast<IMP>(metalNewBuffer),"@32@0:8Q16Q24"},
  {"newBufferWithBytes:length:options:",reinterpret_cast<IMP>(metalNewBufferBytes),"@40@0:8r^v16Q24Q32"},
  {"newBufferWithBytesNoCopy:length:options:deallocator:",reinterpret_cast<IMP>(metalNewBufferNoCopy),"@48@0:8^v16Q24Q32@?40"},
  {"newHeapWithDescriptor:",reinterpret_cast<IMP>(metalNewHeap117),"@24@0:8@16"},
  {"heapBufferSizeAndAlignWithLength:options:",reinterpret_cast<IMP>(metalHeapBufferLayout117),nullptr}};
}
unsigned RTXBufferMethodCount117(){return sizeof(bufferMethods117)/sizeof(bufferMethods117[0]);}
BOOL RTXInstallBufferMethods(Class cls){
 if(!cls||objc_getClass(class_getName(cls)))return NO;
 // Resolve the struct encoding on this call, after construction of this
 // local string. The bundle entry may run before other module initializers.
 const std::string heapQueryEncoding117=std::string(@encode(MTLSizeAndAlign))+"32@0:8Q16Q24";
 // Validate the actual loaded Metal protocol before mutating this new class.
 // Const pointer metadata must match the public method declaration even though
 // const and non-const pointers use the same machine calling convention.
 Protocol *protocol=objc_getProtocol("MTLDevice");if(!protocol)return NO;
 for(const auto &e:bufferMethods117){
  const auto m=protocol_getMethodDescription(protocol,sel_registerName(e.name),YES,YES);
  const char *types=e.types?e.types:heapQueryEncoding117.c_str();
  if(!m.name||!m.types||std::strcmp(m.types,types))return NO;
 }
 for(const auto &e:bufferMethods117){
  const char *types=e.types?e.types:heapQueryEncoding117.c_str();
  if(!class_addMethod(cls,sel_registerName(e.name),e.implementation,types))return NO;
 }
 return YES;
}
NSDictionary *RTXCopyBufferArenaInfo(id<MTLDevice> device){
 RTXApplication209_RTXBufferArena *arena=metalArena(device,false);if(!arena)return [@{@"allocated_bytes":@0,@"live_buffers":@0,@"closed":@NO,@"exists":@NO}copy];
 std::lock_guard<std::mutex> lock(arena->_state->mutex);
 return [@{@"allocated_bytes":@(arena->_state->charged),@"live_buffers":@(arena->_state->count),@"closed":@(arena->_state->closed),@"exists":@YES}copy];
}
void RTXCloseBufferArena(id<MTLDevice> device){[metalArena(device,false) close];}

@interface RTXApplication209_RTXBufferSubmission036 : NSObject {
@public
 RTXApplication209_RTXBufferArena *_arena;
 id<MTLComputePipelineState> _pipeline;
 RTXApplication209_RTXMetalBuffer036 *_buffers[8];
 NSUInteger _offsets[8];
 uint64_t _revisions[8];
 uint64_t _heapEpochs[8];
 unsigned _count,_writeParameters,_invocations;
 uint64_t _completion;
 NSData *_request;
 bool _terminal;
}
- (instancetype)initTransaction;
- (void)cancel;
- (BOOL)publish:(NSData *)result request:(NSData *)request completion:(uint64_t)completion;
@end
@implementation RTXApplication209_RTXBufferSubmission036
- (instancetype)init{[self release];return nil;}
- (instancetype)initTransaction{return [super init];}
- (void)dealloc{[self cancel];for(unsigned i=0;i<_count;++i)[_buffers[i] release];[_arena release];[_pipeline release];[_request release];[super dealloc];}
- (void)cancel{
 if(!_arena)return;std::lock_guard<std::mutex> lock(_arena->_state->mutex);
 if(!_terminal){_terminal=true;for(unsigned i=0;i<_count;++i)if(_writeParameters&(1u<<i))_buffers[i]->_pendingOutput=false;}
}
- (BOOL)publish:(NSData *)data request:(NSData *)request completion:(uint64_t)completion{
 if(!data||data.length!=2048||!request||![request isEqualToData:_request]||completion!=_completion)return NO;
 std::array<uint8_t,2048> result{};[data getBytes:result.data() length:result.size()];const auto *input=static_cast<const uint8_t *>(_request.bytes)+64;
 // Preserve every read-only region, unused parameter and inactive output.
 for(unsigned p=0;p<8;++p){unsigned unchanged=(_writeParameters&(1u<<p))?_invocations*4:0;
  if(std::memcmp(result.data()+p*256+unchanged,input+p*256+unchanged,256-unchanged))return NO;
 }
 std::lock_guard<std::mutex> lock(_arena->_state->mutex);if(_terminal)return NO;
 _terminal=true;for(unsigned i=0;i<_count;++i)if(_writeParameters&(1u<<i))_buffers[i]->_pendingOutput=false;
 if(_arena->_state->closed)return NO;
 // Validate ALL outputs before writing any of them. Raw contents writes are
 // the caller's synchronization responsibility; changed output bytes are also
 // checked here so a visible CPU edit is not overwritten by an old capture.
 for(unsigned i=0;i<_count;++i)if(_writeParameters&(1u<<i)){
  auto *b=_buffers[i];if(b->_revision!=_revisions[i]||heapEpoch(b)!=_heapEpochs[i]||b->_revision==std::numeric_limits<uint64_t>::max()||!bufferAvailable(b)||
   std::memcmp(static_cast<const uint8_t *>(b->_bytes)+_offsets[i],input+i*256,256))return NO;
 }
 for(unsigned i=0;i<_count;++i)if(_writeParameters&(1u<<i)){
  auto *b=_buffers[i];std::memcpy(static_cast<uint8_t *>(b->_bytes)+_offsets[i],result.data()+i*256,256);++b->_revision;
 }
 return YES;
}
@end
id RTXNewBufferSubmission(id<MTLComputePipelineState> pipeline,id<MTLLibrary> library,
 const id<MTLBuffer> *buffers,const NSUInteger *indices,const NSUInteger *offsets,NSUInteger count,
 MTLSize groups,MTLSize threads,uint64_t generation,uint64_t serial,NSError **error){
 if(error)*error=nil;NSUInteger program=0;
 if(!generation||!serial||!count||count>8||!buffers||!indices||!offsets||!RTXGetPipelineProgram(pipeline,library,&program)||!RTXValidatePipelineDispatch(pipeline,groups,threads))return bufferFailure(error,@"Invalid pipeline, generation or dispatch geometry");
 NSDictionary *metadata=RTXCopyPipelineMetadata(pipeline);
 if([metadata[@"abi"]unsignedIntValue]==1||[metadata[@"abi"]unsignedIntValue]==2){[metadata release];return RTXNewOwnedBufferSubmission187(pipeline,library,buffers,indices,offsets,count,groups,threads,generation,serial,error);}
 NSArray *wanted=metadata[@"bindings"];
 if(wanted.count!=count){[metadata release];return bufferFailure(error,@"All reflected shader bindings are required");}
 RTXApplication209_RTXMetalBuffer036 *ordered[8]={};NSUInteger positions[8]={};unsigned writes=0;
 const uint32_t writeMask=[metadata[@"write_mask"]unsignedIntValue];
 for(unsigned p=0;p<count;++p){NSUInteger index=[wanted[p]unsignedIntegerValue];unsigned found=0;
  for(unsigned j=0;j<count;++j)if(indices[j]==index){ordered[p]=(RTXApplication209_RTXMetalBuffer036 *)buffers[j];positions[p]=offsets[j];++found;}
  if(found!=1||[ordered[p]class]!=[RTXApplication209_RTXMetalBuffer036 class]||ordered[p].device!=pipeline.device||positions[p]%4||!rangeOK(NSMakeRange(positions[p],256),ordered[p]->_length)){
   [metadata release];return bufferFailure(error,@"Foreign, missing, duplicate or out-of-range buffer binding");
  }
  for(unsigned j=0;j<p;++j)if(ordered[j]==ordered[p]){[metadata release];return bufferFailure(error,@"Aliased binding regions are not supported by this staging transport");}
  if(writeMask&(1u<<index))writes|=1u<<p;
 }
 [metadata release];RTXApplication209_RTXBufferArena *arena=ordered[0]->_arena;
 for(unsigned p=1;p<count;++p)if(ordered[p]->_arena!=arena)return bufferFailure(error,@"Bindings must share one device arena");
 std::lock_guard<std::mutex> lock(arena->_state->mutex);
 if(arena->_state->closed)return bufferFailure(error,@"Device buffer arena is closed");
 for(unsigned p=0;p<count;++p)if(ordered[p]->_pendingOutput||!bufferAvailable(ordered[p])||((writes&(1u<<p))&&ordered[p]->_revision==std::numeric_limits<uint64_t>::max()))return bufferFailure(error,@"Buffer is unavailable or already pending");
 std::array<uint8_t,2112> wire{};auto word=[&](unsigned at,uint64_t value,unsigned n){for(unsigned i=0;i<n;++i)wire[at+i]=uint8_t(value>>(8*i));};
 word(0,UINT64_C(0x5254585245513335),8);word(8,1,4);word(12,2112,4);word(16,generation,8);word(24,serial,8);word(32,program,4);word(36,groups.width,4);
 for(unsigned p=0;p<count;++p)std::memcpy(wire.data()+64+p*256,static_cast<const uint8_t *>(ordered[p]->_bytes)+positions[p],256);
 NSData *request=[[NSData alloc]initWithBytes:wire.data() length:wire.size()];if(!request)return bufferFailure(error,@"Unable to allocate request snapshot");
 RTXApplication209_RTXBufferSubmission036 *submission=[[RTXApplication209_RTXBufferSubmission036 alloc]initTransaction];if(!submission){[request release];return bufferFailure(error,@"Unable to allocate submission");}
 submission->_arena=[arena retain];submission->_pipeline=[pipeline retain];submission->_request=request;submission->_count=unsigned(count);submission->_writeParameters=writes;
 submission->_invocations=unsigned(groups.width*threads.width);submission->_completion=serial;
 for(unsigned p=0;p<count;++p){submission->_buffers[p]=[ordered[p] retain];submission->_offsets[p]=positions[p];submission->_revisions[p]=ordered[p]->_revision;submission->_heapEpochs[p]=heapEpoch(ordered[p]);if(writes&(1u<<p))ordered[p]->_pendingOutput=true;}
 return submission;
}
NSData *RTXCopyBufferSubmissionRequest(id object){if(RTXIsOwnedBufferSubmission187(object))return RTXCopyOwnedBufferRequest187(object);if([object class]!=[RTXApplication209_RTXBufferSubmission036 class])return nil;return [((RTXApplication209_RTXBufferSubmission036 *)object)->_request copy];}
BOOL RTXBufferSubmissionTerminal(id object){if(RTXIsOwnedBufferSubmission187(object))return RTXOwnedBufferTerminal187(object);if([object class]!=[RTXApplication209_RTXBufferSubmission036 class])return YES;auto *s=(RTXApplication209_RTXBufferSubmission036 *)object;std::lock_guard<std::mutex> lock(s->_arena->_state->mutex);return s->_terminal;}
void RTXCancelBufferSubmission(id object){if(RTXIsOwnedBufferSubmission187(object)){RTXCancelOwnedBufferSubmission187(object);return;}if([object class]==[RTXApplication209_RTXBufferSubmission036 class])[(RTXApplication209_RTXBufferSubmission036 *)object cancel];}
BOOL RTXPublishBufferSubmission(id object,NSData *request,NSData *result,uint64_t completion){if(RTXIsOwnedBufferSubmission187(object))return RTXPublishOwnedBufferSubmission187(object,request,result,completion);return [object class]==[RTXApplication209_RTXBufferSubmission036 class]&&[(RTXApplication209_RTXBufferSubmission036 *)object publish:result request:request completion:completion];}
