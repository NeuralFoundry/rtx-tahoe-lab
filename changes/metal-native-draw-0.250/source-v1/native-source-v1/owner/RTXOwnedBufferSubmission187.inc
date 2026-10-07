// Included beside RTXMetalBuffer.inc: one arena mutex protects the complete
// retained-resource transaction, including aliased shader bindings.
@interface RTXOwnedBufferSubmission187 : NSObject {
@public
 RTXBufferArena*_arena;RTXMetalBuffer036*_buffers[4];uint64_t _revisions[4];
 RTXBatch187::Plan _plan;NSData*_request;BOOL _terminal;
}
- (instancetype)initTransaction;
- (void)cancel;
- (BOOL)publish:(NSData*)result request:(NSData*)request completion:(uint64_t)completion;
@end
@implementation RTXOwnedBufferSubmission187
- (instancetype)init{[self release];return nil;}
- (instancetype)initTransaction{return [super init];}
- (void)dealloc{[self cancel];for(unsigned i=0;i<_plan.resources;++i)[_buffers[i]release];[_request release];[_arena release];[super dealloc];}
- (void)cancel{if(!_arena)return;std::lock_guard<std::mutex>lock(_arena->_state->mutex);if(!_terminal){_terminal=YES;for(unsigned i=0;i<_plan.resources;++i)_buffers[i]->_pendingOutput=false;}}
- (BOOL)publish:(NSData*)data request:(NSData*)request completion:(uint64_t)completion{
 if(![data isKindOfClass:[NSData class]]||data.length!=_plan.payloadBytes||![request isKindOfClass:[NSData class]]||![request isEqualToData:_request]||completion!=_plan.serial)return NO;
 RTXBatch187::Bytes copy(data.length);[data getBytes:copy.data()length:copy.size()];
 const auto*before=static_cast<const uint8_t*>(_request.bytes);
 if(!RTXBatch187::readback(_plan,before,_request.length,copy.data(),copy.size()))return NO;
 std::lock_guard<std::mutex>lock(_arena->_state->mutex);if(_terminal)return NO;_terminal=YES;
 for(unsigned i=0;i<_plan.resources;++i)_buffers[i]->_pendingOutput=false;
 if(_arena->_state->closed)return NO;
 // Validate every output before mutating any resource. Aliases share a single
 // snapshot, revision and publication, preserving their native pointer relation.
 for(unsigned i=0;i<_plan.resources;++i){const auto&r=_plan.resource[i];auto*b=_buffers[i];
  if(b->_purge==MTLPurgeableStateEmpty)return NO;
  if((r.access&2)&&(b->_revision!=_revisions[i]||b->_revision==UINT64_MAX||std::memcmp(b->_bytes,before+RTXBatch187::Header+r.payloadOffset,size_t(r.bytes))))return NO;
 }
 for(unsigned i=0;i<_plan.resources;++i){const auto&r=_plan.resource[i];if(r.access&2){auto*b=_buffers[i];std::memcpy(b->_bytes,copy.data()+r.payloadOffset,size_t(r.bytes));++b->_revision;}}
 return YES;
}
@end
static id newOwnedSubmission187(id<MTLComputePipelineState> pipeline,id<MTLLibrary> library,const id<MTLBuffer>*buffers,const NSUInteger*indices,const NSUInteger*offsets,NSUInteger count,MTLSize groups,MTLSize threads,uint64_t generation,uint64_t serial,NSError**error){
 if(error)*error=nil;NSUInteger program=0;
 if(!count||count>8||!buffers||!indices||!offsets||!RTXGetPipelineProgram(pipeline,library,&program))return bufferFailure(error,@"Invalid owned buffer submission");
 NSData*container=RTXCopyLibraryContainer(library);RTXCatalog187::Catalog catalog;
 const bool valid=container&&RTXCatalog187::decode(static_cast<const uint8_t*>(container.bytes),container.length,catalog)&&catalog.abi==2&&program<catalog.library.count;
 [container release];if(!valid)return bufferFailure(error,@"Owned submission requires a compiled ABI2 library");
 std::array<RTXBatch187::Input,8>inputs{};
 RTXBufferArena*arena=nil;
 for(unsigned i=0;i<count;++i){
  if(indices[i]>=32||object_getClass(buffers[i])!=[RTXMetalBuffer036 class]||buffers[i].device!=pipeline.device)return bufferFailure(error,@"Foreign or invalid owned buffer");
  auto*b=(RTXMetalBuffer036*)buffers[i];if(!arena)arena=b->_arena;else if(arena!=b->_arena)return bufferFailure(error,@"Owned buffers must share a device arena");
  inputs[i]={uint64_t(reinterpret_cast<uintptr_t>(b)),b->_length,offsets[i],uint32_t(indices[i])};
 }
 RTXBatch187::Plan plan;
 if(!RTXBatch187::plan(catalog.library.programs[program],uint32_t(program),inputs.data(),count,{groups.width,groups.height,groups.depth},{threads.width,threads.height,threads.depth},generation,serial,plan))return bufferFailure(error,@"Bindings or geometry exceed the current owned mappings");
 std::lock_guard<std::mutex>lock(arena->_state->mutex);if(arena->_state->closed)return bufferFailure(error,@"Device buffer arena is closed");
 RTXMetalBuffer036*unique[4]={};std::array<RTXBatch187::Bytes,4>snapshots;
 for(unsigned i=0;i<plan.resources;++i){const auto&r=plan.resource[i];for(unsigned j=0;j<count;++j)if(inputs[j].key==r.key){unique[i]=(RTXMetalBuffer036*)buffers[j];break;}
  auto*b=unique[i];if(!b||b->_pendingOutput||b->_purge==MTLPurgeableStateEmpty||((r.access&2)&&b->_revision==UINT64_MAX))return bufferFailure(error,@"Owned buffer is unavailable or pending");
  snapshots[i].assign(static_cast<const uint8_t*>(b->_bytes),static_cast<const uint8_t*>(b->_bytes)+r.bytes);
 }
 RTXBatch187::Bytes wire;if(!RTXBatch187::assemble(plan,snapshots,wire))return bufferFailure(error,@"Unable to snapshot owned buffers");
 NSData*request=[[NSData alloc]initWithBytes:wire.data()length:wire.size()];if(!request)return bufferFailure(error,@"Unable to allocate owned request");
 auto*transaction=[[RTXOwnedBufferSubmission187 alloc]initTransaction];if(!transaction){[request release];return bufferFailure(error,@"Unable to allocate owned transaction");}
 transaction->_plan=plan;transaction->_arena=[arena retain];transaction->_request=request;
 for(unsigned i=0;i<plan.resources;++i){auto*b=unique[i];transaction->_buffers[i]=[b retain];transaction->_revisions[i]=b->_revision;b->_pendingOutput=true;}
 return transaction;
}
id RTXNewOwnedBufferSubmission187(id<MTLComputePipelineState> pipeline,id<MTLLibrary> library,const id<MTLBuffer>*buffers,const NSUInteger*indices,const NSUInteger*offsets,NSUInteger count,MTLSize groups,MTLSize threads,uint64_t generation,uint64_t serial,NSError**error){
 try{@try{return newOwnedSubmission187(pipeline,library,buffers,indices,offsets,count,groups,threads,generation,serial,error);}@catch(NSException*exception){(void)exception;}}catch(...){ }
 return bufferFailure(error,@"Unable to allocate an owned command snapshot");
}
BOOL RTXIsOwnedBufferSubmission187(id object){return object_getClass(object)==[RTXOwnedBufferSubmission187 class];}
NSData*RTXCopyOwnedBufferRequest187(id object){return RTXIsOwnedBufferSubmission187(object)?[((RTXOwnedBufferSubmission187*)object)->_request copy]:nil;}
BOOL RTXOwnedBufferTerminal187(id object){if(!RTXIsOwnedBufferSubmission187(object))return YES;auto*t=(RTXOwnedBufferSubmission187*)object;std::lock_guard<std::mutex>lock(t->_arena->_state->mutex);return t->_terminal;}
void RTXCancelOwnedBufferSubmission187(id object){if(RTXIsOwnedBufferSubmission187(object))[(RTXOwnedBufferSubmission187*)object cancel];}
BOOL RTXPublishOwnedBufferSubmission187(id object,NSData*request,NSData*result,uint64_t completion){
 if(!RTXIsOwnedBufferSubmission187(object))return NO;
 try{@try{return [(RTXOwnedBufferSubmission187*)object publish:result request:request completion:completion];}@catch(NSException*exception){(void)exception;}}catch(...){ }
 RTXCancelOwnedBufferSubmission187(object);return NO;
}
