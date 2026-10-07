#import "RTXMetalCommand.h"
#import <objc/runtime.h>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

@class RTXCommandContext036, RTXCommandQueue036, RTXCommandBuffer036, RTXComputeEncoder036;
static char contextKey;
static thread_local RTXCommandBuffer036 *insideHandler;
static thread_local RTXCommandContext036 *insideTransport;
static NSError *commandError(NSString *message,NSUInteger code=MTLCommandBufferErrorInternal){
 return [NSError errorWithDomain:MTLCommandBufferErrorDomain code:code userInfo:@{NSLocalizedDescriptionKey:message}];
}
static void misuse(NSString *message){[NSException raise:NSInternalInconsistencyException format:@"%@",message];}
struct Operation036 {
 id<MTLComputePipelineState> pipeline=nil;id<MTLLibrary> library=nil;
 id<MTLBuffer> buffers[8]={};NSUInteger indices[8]={},offsets[8]={},count=0;
 MTLSize groups={},threads={};
 ~Operation036(){for(auto b:buffers)[b release];[pipeline release];[library release];}
};
@interface RTXCommandContext036 : NSObject {
 @public std::mutex mutex;std::condition_variable changed;
 dispatch_queue_t worker;NSData *payload;id<RTXCommandTransport> transport;
 uint64_t generation,nextSerial,calls,publications;BOOL closed,uncertain;
 std::vector<RTXCommandQueue036 *> queues; // Borrowed, protected by mutex.
}
@end
@interface RTXCommandQueue036 : NSObject<MTLCommandQueue> {
 @public id<MTLDevice> _device;RTXCommandContext036 *_context;NSString *_label;
 NSUInteger _limit,_outstanding;BOOL _running;
 std::deque<RTXCommandBuffer036 *> _pending; // Enqueue owns until terminal.
}
- (id)initWithDevice:(id<MTLDevice>)device context:(RTXCommandContext036 *)context limit:(NSUInteger)limit;
- (void)unsupported:(SEL)selector;
@end
@interface RTXCommandBuffer036 : NSObject<MTLCommandBuffer> {
 @public RTXCommandQueue036 *_queue;MTLCommandBufferStatus _status;NSError *_error;
 NSString *_label;NSMutableArray *_scheduledHandlers,*_completedHandlers;
 BOOL _scheduledDone,_callbacksDone,_counted;NSUInteger _debugDepth;
 RTXComputeEncoder036 *_active; // Encoder retains buffer, buffer borrows encoder.
 std::vector<std::unique_ptr<Operation036>> _operations;
}
- (id)initWithQueue:(RTXCommandQueue036 *)queue;
- (void)unsupported:(SEL)selector;
- (void)guardEncoding;
@end
@interface RTXComputeEncoder036 : NSObject<MTLComputeCommandEncoder> {
 @public RTXCommandBuffer036 *_buffer;NSString *_label;BOOL _ended;
 id<MTLComputePipelineState> _pipeline;id<MTLBuffer> _bindings[32];
 NSUInteger _offsets[32],_debugDepth;
}
- (id)initWithBuffer:(RTXCommandBuffer036 *)buffer;
- (void)unsupported:(SEL)selector;
- (void)guardEncoding;
@end

static void rememberError(RTXCommandBuffer036 *b,NSString *message){
 if(!b->_error)b->_error=[commandError(message) retain];
}
static void kickLocked(RTXCommandQueue036 *q);
static void invokeHandlers(NSArray *handlers,RTXCommandBuffer036 *b){
 insideHandler=b;
 for(MTLCommandBufferHandler handler in handlers){
  // A client exception must not strand the remaining handlers or resources.
  @try{handler(b);}@catch(NSException *exception){(void)exception;}
 }
 insideHandler=nullptr;
}
static void callbacks(RTXCommandBuffer036 *b){
 auto *c=b->_queue->_context;
 NSArray *scheduled=nil,*completed=nil;
 {std::lock_guard<std::mutex> lock(c->mutex);
  scheduled=[b->_scheduledHandlers copy];[b->_scheduledHandlers removeAllObjects];}
 invokeHandlers(scheduled,b);[scheduled release];
 {std::unique_lock<std::mutex> lock(c->mutex);
  b->_scheduledDone=YES;c->changed.notify_all();
  c->changed.wait(lock,[&]{return b->_status>=MTLCommandBufferStatusCompleted;});
  completed=[b->_completedHandlers copy];[b->_completedHandlers removeAllObjects];}
 invokeHandlers(completed,b);[completed release];
 {std::lock_guard<std::mutex> lock(c->mutex);b->_callbacksDone=YES;c->changed.notify_all();}
}
static void execute(RTXCommandBuffer036 *b){
 auto *q=b->_queue;auto *c=q->_context;NSError *failure=nil;
 {std::lock_guard<std::mutex> lock(c->mutex);
  if(c->closed||c->uncertain)failure=[commandError(@"Command device is closed or its last transport outcome is uncertain") retain];
  else if(b->_error)failure=[b->_error retain];
  b->_status=MTLCommandBufferStatusScheduled;c->changed.notify_all();}
 // Client handlers never run on the serialized transport worker or under its
 // mutex. A scheduled handler may wait for this command's GPU work, but not
 // for its own callback sequence (which would be a self-deadlock).
 dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{@autoreleasepool{callbacks(b);}});
 if(!failure)for(const auto &operation:b->_operations){@autoreleasepool {
  auto &op=*operation;uint64_t serial=0;
  {std::lock_guard<std::mutex> lock(c->mutex);
   if(c->closed||c->uncertain||c->nextSerial==0)failure=[commandError(@"Command device is unavailable or serial space is exhausted") retain];
   else serial=c->nextSerial;}
  if(failure)break;
  NSError *error=nil;
  NSData *operationPayload=RTXCopyLibraryPayload(op.library);
  BOOL matchingPayload=operationPayload&&[operationPayload isEqualToData:c->payload];[operationPayload release];
  if(!matchingPayload){failure=[commandError(@"Pipeline library does not match this command device's configured payload",MTLCommandBufferErrorInvalidResource)retain];break;}
  id transaction=RTXNewBufferSubmission(op.pipeline,op.library,op.buffers,op.indices,op.offsets,op.count,op.groups,op.threads,c->generation,serial,&error);
  if(!transaction){failure=[commandError(error.localizedDescription?:@"Unable to stage command buffers",MTLCommandBufferErrorInvalidResource) retain];break;}
  NSData *request=RTXCopyBufferSubmissionRequest(transaction),*result=nil;uint64_t completion=0;BOOL success=NO,called=NO;
  {std::lock_guard<std::mutex> lock(c->mutex);
   if(!c->closed&&!c->uncertain){called=YES;++c->calls;c->nextSerial=serial==UINT64_MAX?0:serial+1;}}
  if(called){
   insideTransport=c;
   @try {success=[c->transport executeRequest:request libraryPayload:c->payload result:&result completion:&completion error:&error];}
   @catch(NSException *exception){error=commandError(exception.reason?:@"Transport raised an exception");success=NO;}
   insideTransport=nullptr;
   if([result isKindOfClass:[NSData class]])result=[result copy];else result=nil;
  }
  BOOL published=NO;
  {std::lock_guard<std::mutex> lock(c->mutex);
   if(called&&success&&!error&&!c->closed&&[result isKindOfClass:[NSData class]])
    published=RTXPublishBufferSubmission(transaction,request,result,completion);
   if(published)++c->publications;
   else if(called){c->uncertain=YES;for(auto *other:c->queues)kickLocked(other);} // No replay after any uncertain owner call.
  }
  if(!published){RTXCancelBufferSubmission(transaction);failure=[commandError(error.localizedDescription?:@"Transport completion could not be published; further device work is disabled") retain];}
  [result release];[request release];[transaction release];
  if(failure)break;
 }}
 {std::lock_guard<std::mutex> lock(c->mutex);
  if(failure){[b->_error release];b->_error=[failure retain];}
  b->_status=failure?MTLCommandBufferStatusError:MTLCommandBufferStatusCompleted;
  b->_operations.clear();
  if(b->_counted){--q->_outstanding;b->_counted=NO;}
  q->_running=NO; // Current buffer is already removed from pending.
  kickLocked(q);c->changed.notify_all();}
 [failure release];
}
static void kickLocked(RTXCommandQueue036 *q){
 if(q->_running||q->_pending.empty())return;
 auto *b=q->_pending.front();auto *c=q->_context;
 if(b->_status<MTLCommandBufferStatusCommitted&&!c->closed&&!c->uncertain)return;
 q->_pending.pop_front();q->_running=YES;
 // The copied block retains b. Transfer the queue's enqueue retain to it.
 dispatch_async(c->worker,^{@autoreleasepool{execute(b);}});[b release];
}
@implementation RTXCommandContext036
- (id)init {if((self=[super init])){worker=dispatch_queue_create("RTX036.command.transport",DISPATCH_QUEUE_SERIAL);nextSerial=1;}return self;}
- (void)dealloc {[payload release];[transport release];dispatch_release(worker);[super dealloc];}
@end

@implementation RTXCommandQueue036
- (id)init {[self release];return nil;}
- (id)initWithDevice:(id<MTLDevice>)device context:(RTXCommandContext036 *)context limit:(NSUInteger)limit {
 if((self=[super init])){_device=[device retain];_context=[context retain];_limit=limit;BOOL accepted=NO;
  {std::lock_guard<std::mutex> lock(context->mutex);
   if(!context->closed&&!context->uncertain){context->queues.push_back(self);accepted=YES;}}
  // The preliminary device check can race closure/ownership transfer. Admit
  // under the same mutex as the transfer gate and destroy outside that lock.
  if(!accepted){[self release];return nil;}}
 return self;
}
- (void)dealloc {
 if(_context){std::lock_guard<std::mutex> lock(_context->mutex);auto &v=_context->queues;v.erase(std::remove(v.begin(),v.end(),self),v.end());}
 [_label release];[_context release];[_device release];[super dealloc];
}
- (id<MTLDevice>)device{return _device;}
- (NSString *)label{std::lock_guard<std::mutex> lock(_context->mutex);return [[_label retain]autorelease];}
- (void)setLabel:(NSString *)label{std::lock_guard<std::mutex> lock(_context->mutex);NSString *copy=[label copy];[_label release];_label=copy;}
- (id<MTLCommandBuffer>)commandBuffer {
 std::lock_guard<std::mutex> lock(_context->mutex);
 if(_context->closed||_context->uncertain||_outstanding>=_limit)return nil;
 auto *b=[[RTXCommandBuffer036 alloc]initWithQueue:self];if(b){++_outstanding;b->_counted=YES;}return [b autorelease];
}
- (id<MTLCommandBuffer>)commandBufferWithDescriptor:(MTLCommandBufferDescriptor *)descriptor {
 if(!descriptor||!descriptor.retainedReferences||descriptor.errorOptions!=MTLCommandBufferErrorOptionNone||descriptor.logState)return nil;
 return [self commandBuffer];
}
- (id<MTLCommandBuffer>)commandBufferWithUnretainedReferences{return nil;}
- (void)insertDebugCaptureBoundary{}
- (void)unsupported:(SEL)selector{misuse([NSString stringWithFormat:@"Command queue feature %@ is unsupported",NSStringFromSelector(selector)]);}
#include "RTXCommandQueueUnsupported.inc"
@end

@implementation RTXCommandBuffer036
- (id)init{[self release];return nil;}
- (id)initWithQueue:(RTXCommandQueue036 *)queue {
 if((self=[super init])){_queue=[queue retain];_scheduledHandlers=[[NSMutableArray alloc]init];_completedHandlers=[[NSMutableArray alloc]init];}
 return self;
}
- (void)dealloc {
 if(_queue){std::lock_guard<std::mutex> lock(_queue->_context->mutex);if(_counted)--_queue->_outstanding;}
 _operations.clear();[_scheduledHandlers release];[_completedHandlers release];[_error release];[_label release];[_queue release];[super dealloc];
}
- (id<MTLDevice>)device{return _queue.device;}
- (id<MTLCommandQueue>)commandQueue{return _queue;}
- (BOOL)retainedReferences{return YES;}
- (MTLCommandBufferErrorOption)errorOptions{return MTLCommandBufferErrorOptionNone;}
- (CFTimeInterval)kernelStartTime{return 0;}
- (CFTimeInterval)kernelEndTime{return 0;}
- (CFTimeInterval)GPUStartTime{return 0;}
- (CFTimeInterval)GPUEndTime{return 0;}
- (id<MTLLogContainer>)logs{return nil;}
- (NSString *)label{std::lock_guard<std::mutex> lock(_queue->_context->mutex);return [[_label retain]autorelease];}
- (void)setLabel:(NSString *)label{std::lock_guard<std::mutex> lock(_queue->_context->mutex);NSString *copy=[label copy];[_label release];_label=copy;}
- (MTLCommandBufferStatus)status{std::lock_guard<std::mutex> lock(_queue->_context->mutex);return _status;}
- (NSError *)error{std::lock_guard<std::mutex> lock(_queue->_context->mutex);return [[_error retain]autorelease];}
- (void)guardEncoding {if(_status>=MTLCommandBufferStatusCommitted)misuse(@"A committed command buffer cannot be encoded or committed again");}
- (void)enqueue {
 auto *c=_queue->_context;std::lock_guard<std::mutex> lock(c->mutex);
 if(_status!=MTLCommandBufferStatusNotEnqueued)misuse(@"A command buffer can only be enqueued once");
 _status=MTLCommandBufferStatusEnqueued;_queue->_pending.push_back([self retain]);kickLocked(_queue);
}
- (void)commit {
 auto *c=_queue->_context;std::lock_guard<std::mutex> lock(c->mutex);[self guardEncoding];
 if(_active)rememberError(self,@"The compute encoder must end before commit");
 if(_debugDepth)rememberError(self,@"Unbalanced command buffer debug groups");
 if(_status==MTLCommandBufferStatusNotEnqueued)_queue->_pending.push_back([self retain]);
 _status=MTLCommandBufferStatusCommitted;kickLocked(_queue);
}
- (void)addScheduledHandler:(MTLCommandBufferHandler)block {
 if(!block)misuse(@"Scheduled handler cannot be nil");
 std::lock_guard<std::mutex> lock(_queue->_context->mutex);[self guardEncoding];id copy=[block copy];[_scheduledHandlers addObject:copy];[copy release];
}
- (void)addCompletedHandler:(MTLCommandBufferHandler)block {
 if(!block)misuse(@"Completion handler cannot be nil");
 std::lock_guard<std::mutex> lock(_queue->_context->mutex);[self guardEncoding];id copy=[block copy];[_completedHandlers addObject:copy];[copy release];
}
- (void)waitUntilScheduled {
 auto *c=_queue->_context;if(insideHandler==self||insideTransport==c)misuse(@"Waiting inside this command's handler or transport would deadlock");
 std::unique_lock<std::mutex> lock(c->mutex);c->changed.wait(lock,[&]{return _scheduledDone;});
}
- (void)waitUntilCompleted {
 auto *c=_queue->_context;if(insideHandler==self||insideTransport==c)misuse(@"Waiting inside this command's handler or transport would deadlock");
 std::unique_lock<std::mutex> lock(c->mutex);c->changed.wait(lock,[&]{return _callbacksDone;});
}
- (id<MTLComputeCommandEncoder>)computeCommandEncoder {
 std::lock_guard<std::mutex> lock(_queue->_context->mutex);[self guardEncoding];
 if(_active){rememberError(self,@"Only one encoder may be active on a command buffer");return nil;}
 auto *e=[[RTXComputeEncoder036 alloc]initWithBuffer:self];_active=e;return [e autorelease];
}
- (id<MTLComputeCommandEncoder>)computeCommandEncoderWithDispatchType:(MTLDispatchType)type {
 if(type!=MTLDispatchTypeSerial&&type!=MTLDispatchTypeConcurrent){[self unsupported:_cmd];return nil;}
 // Metal permits concurrent requests to fall back to serial on this device.
 return [self computeCommandEncoder];
}
- (id<MTLComputeCommandEncoder>)computeCommandEncoderWithDescriptor:(MTLComputePassDescriptor *)descriptor {
 // Counter attachment descriptors are not implemented in this prototype.
 // Refuse the descriptor entry rather than silently discarding its settings.
 (void)descriptor;[self unsupported:_cmd];return nil;
}
- (void)pushDebugGroup:(NSString *)string{std::lock_guard<std::mutex> lock(_queue->_context->mutex);[self guardEncoding];if(!string||_debugDepth==1024)rememberError(self,@"Invalid debug group");else ++_debugDepth;}
- (void)popDebugGroup{std::lock_guard<std::mutex> lock(_queue->_context->mutex);[self guardEncoding];if(!_debugDepth)rememberError(self,@"Unbalanced debug group");else --_debugDepth;}
- (void)unsupported:(SEL)selector{std::lock_guard<std::mutex> lock(_queue->_context->mutex);[self guardEncoding];rememberError(self,[NSString stringWithFormat:@"Command buffer feature %@ is unsupported",NSStringFromSelector(selector)]);}
#include "RTXCommandBufferUnsupported.inc"
@end

@implementation RTXComputeEncoder036
- (id)init{[self release];return nil;}
- (id)initWithBuffer:(RTXCommandBuffer036 *)buffer{if((self=[super init]))_buffer=[buffer retain];return self;}
- (void)dealloc {
 if(_buffer){std::lock_guard<std::mutex> lock(_buffer->_queue->_context->mutex);if(_buffer->_active==self){_buffer->_active=nil;rememberError(_buffer,@"Encoder was released without endEncoding");}}
 for(auto b:_bindings)[b release];[_pipeline release];[_label release];[_buffer release];[super dealloc];
}
- (id<MTLDevice>)device{return _buffer.device;}
- (MTLDispatchType)dispatchType{return MTLDispatchTypeSerial;}
- (NSString *)label{std::lock_guard<std::mutex> lock(_buffer->_queue->_context->mutex);return [[_label retain]autorelease];}
- (void)setLabel:(NSString *)label{std::lock_guard<std::mutex> lock(_buffer->_queue->_context->mutex);NSString *copy=[label copy];[_label release];_label=copy;}
- (void)guardEncoding {[_buffer guardEncoding];if(_ended||_buffer->_active!=self)misuse(@"The compute encoder has ended");}
- (void)endEncoding {
 std::lock_guard<std::mutex> lock(_buffer->_queue->_context->mutex);[self guardEncoding];
 if(_debugDepth)rememberError(_buffer,@"Unbalanced encoder debug groups");_ended=YES;_buffer->_active=nil;
 // Recorded operations retain precisely their bindings and pipeline.
 for(auto &b:_bindings){[b release];b=nil;}[_pipeline release];_pipeline=nil;
}
- (void)setComputePipelineState:(id<MTLComputePipelineState>)pipeline {
 std::lock_guard<std::mutex> lock(_buffer->_queue->_context->mutex);[self guardEncoding];
 id<MTLLibrary> library=RTXCopyPipelineLibrary(pipeline);NSData *payload=RTXCopyLibraryPayload(library);
 BOOL valid=library&&pipeline.device==self.device&&[payload isEqualToData:_buffer->_queue->_context->payload];[payload release];[library release];
 if(!valid){rememberError(_buffer,@"Pipeline belongs to another device or compiled library");return;}
 [pipeline retain];[_pipeline release];_pipeline=pipeline;
}
- (void)setBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index {
 std::lock_guard<std::mutex> lock(_buffer->_queue->_context->mutex);[self guardEncoding];
 if(index>=32||(buffer&&([buffer class]!=objc_getClass("RTXMetalBuffer036")||buffer.device!=self.device||offset>buffer.length||offset%4))){rememberError(_buffer,@"Invalid compute buffer binding");return;}
 [buffer retain];[_bindings[index] release];_bindings[index]=buffer;_offsets[index]=offset;
}
- (void)setBufferOffset:(NSUInteger)offset atIndex:(NSUInteger)index {
 std::lock_guard<std::mutex> lock(_buffer->_queue->_context->mutex);[self guardEncoding];
 if(index>=32||!_bindings[index]||offset>_bindings[index].length||offset%4){rememberError(_buffer,@"Invalid buffer offset");return;}_offsets[index]=offset;
}
- (void)setBuffers:(const id<MTLBuffer>[])buffers offsets:(const NSUInteger[])offsets withRange:(NSRange)range {
 if(range.location>32||range.length>32-range.location||(range.length&&(!buffers||!offsets))){[self unsupported:_cmd];return;}
 for(NSUInteger i=0;i<range.length;++i)[self setBuffer:buffers[i]offset:offsets[i]atIndex:range.location+i];
}
- (void)dispatchThreadgroups:(MTLSize)groups threadsPerThreadgroup:(MTLSize)threads {
 std::lock_guard<std::mutex> lock(_buffer->_queue->_context->mutex);[self guardEncoding];
 if(!RTXValidatePipelineDispatch(_pipeline,groups,threads)||_buffer->_operations.size()>=1024){rememberError(_buffer,@"Invalid whole-group dispatch or command count");return;}
 auto op=std::make_unique<Operation036>();op->pipeline=[_pipeline retain];op->library=RTXCopyPipelineLibrary(_pipeline);op->groups=groups;op->threads=threads;
 NSDictionary *metadata=RTXCopyPipelineMetadata(_pipeline);NSArray *indices=metadata[@"bindings"];
 BOOL valid=indices.count>0&&indices.count<=8;op->count=indices.count;
 if(valid)for(NSUInteger n=0;n<op->count;++n){NSUInteger i=[indices[n]unsignedIntegerValue];
  if(i>=32||!_bindings[i]||_offsets[i]>_bindings[i].length||_bindings[i].length-_offsets[i]<256){valid=NO;break;}
  for(NSUInteger p=0;p<n;++p)if(op->buffers[p]==_bindings[i])valid=NO;
  op->buffers[n]=[_bindings[i] retain];op->indices[n]=i;op->offsets[n]=_offsets[i];
 }
 [metadata release];
 if(valid)_buffer->_operations.push_back(std::move(op));else rememberError(_buffer,@"Missing, aliased or undersized reflected binding");
}
- (void)dispatchThreads:(MTLSize)grid threadsPerThreadgroup:(MTLSize)threads {
 if(!threads.width||grid.width%threads.width||grid.height!=1||grid.depth!=1){[self unsupported:_cmd];return;}
 [self dispatchThreadgroups:MTLSizeMake(grid.width/threads.width,1,1)threadsPerThreadgroup:threads];
}
- (void)memoryBarrierWithScope:(MTLBarrierScope)scope{(void)scope;std::lock_guard<std::mutex> lock(_buffer->_queue->_context->mutex);[self guardEncoding];}
- (void)memoryBarrierWithResources:(const id<MTLResource>[])resources count:(NSUInteger)count{(void)resources;(void)count;std::lock_guard<std::mutex> lock(_buffer->_queue->_context->mutex);[self guardEncoding];}
- (void)barrierAfterQueueStages:(MTLStages)after beforeStages:(MTLStages)before{(void)after;(void)before;std::lock_guard<std::mutex> lock(_buffer->_queue->_context->mutex);[self guardEncoding];}
- (void)insertDebugSignpost:(NSString *)string{std::lock_guard<std::mutex> lock(_buffer->_queue->_context->mutex);[self guardEncoding];if(!string)rememberError(_buffer,@"Invalid signpost");}
- (void)pushDebugGroup:(NSString *)string{std::lock_guard<std::mutex> lock(_buffer->_queue->_context->mutex);[self guardEncoding];if(!string||_debugDepth==1024)rememberError(_buffer,@"Invalid debug group");else ++_debugDepth;}
- (void)popDebugGroup{std::lock_guard<std::mutex> lock(_buffer->_queue->_context->mutex);[self guardEncoding];if(!_debugDepth)rememberError(_buffer,@"Unbalanced debug group");else --_debugDepth;}
- (void)unsupported:(SEL)selector{std::lock_guard<std::mutex> lock(_buffer->_queue->_context->mutex);[self guardEncoding];rememberError(_buffer,[NSString stringWithFormat:@"Compute feature %@ is unsupported",NSStringFromSelector(selector)]);}
#include "RTXComputeEncoderUnsupported.inc"
@end

static RTXCommandContext036 *contextFor(id device){@synchronized(device){return [[objc_getAssociatedObject(device,&contextKey)retain]autorelease];}}
BOOL RTXConfigureCommandDevice(id<MTLDevice> device,id<MTLLibrary> library,uint64_t generation,id<RTXCommandTransport> transport,NSError **error){
 if(error)*error=nil;NSData *payload=RTXCopyLibraryPayload(library);
 if(!device||!payload||library.device!=device||!generation||![transport conformsToProtocol:@protocol(RTXCommandTransport)]||![transport respondsToSelector:@selector(executeRequest:libraryPayload:result:completion:error:)]){
  [payload release];if(error)*error=commandError(@"Invalid command device configuration");return NO;}
 @synchronized(device){
  if(objc_getAssociatedObject(device,&contextKey)){[payload release];if(error)*error=commandError(@"The command device is already configured");return NO;}
  auto *c=[[RTXCommandContext036 alloc]init];c->payload=payload;c->generation=generation;c->transport=[transport retain];
  objc_setAssociatedObject(device,&contextKey,c,OBJC_ASSOCIATION_RETAIN_NONATOMIC);[c release];
 }
 return YES;
}
BOOL RTXClaimIdleCommandDeviceForBroker(id<MTLDevice> device){
 auto *c=contextFor(device);if(!c)return NO;std::lock_guard<std::mutex> lock(c->mutex);
 if(c->closed||c->uncertain||!c->queues.empty()||c->calls||c->publications||c->nextSerial!=1)return NO;
 c->closed=YES;c->changed.notify_all();return YES;
}
void RTXCloseCommandDevice(id<MTLDevice> device){
 auto *c=contextFor(device);if(!c)return;std::lock_guard<std::mutex> lock(c->mutex);c->closed=YES;
 for(auto *q:c->queues)kickLocked(q);c->changed.notify_all();
}
NSDictionary *RTXCopyCommandDeviceInfo(id<MTLDevice> device){
 auto *c=contextFor(device);if(!c)return [@{@"configured":@NO}copy];std::lock_guard<std::mutex> lock(c->mutex);
 return [@{@"configured":@YES,@"closed":@(c->closed),@"uncertain":@(c->uncertain),@"generation":@(c->generation),@"next_serial":@(c->nextSerial),@"transport_calls":@(c->calls),@"publications":@(c->publications),@"queues":@(c->queues.size())}copy];
}
static id<MTLCommandQueue> deviceQueueCount(id self,SEL,NSUInteger limit){
 auto *c=contextFor(self);if(!c||!limit||limit>1024)return nil;
 {std::lock_guard<std::mutex> lock(c->mutex);if(c->closed||c->uncertain)return nil;}
 return [[RTXCommandQueue036 alloc]initWithDevice:self context:c limit:limit];
}
static id<MTLCommandQueue> deviceQueue(id self,SEL){return deviceQueueCount(self,nullptr,64);}
static id<MTLCommandQueue> deviceQueueDescriptor(id self,SEL,MTLCommandQueueDescriptor *descriptor){if(!descriptor||descriptor.logState)return nil;return deviceQueueCount(self,nullptr,descriptor.maxCommandBufferCount);}
BOOL RTXInstallCommandMethods(Class cls){
 if(!cls||objc_getClass(class_getName(cls)))return NO;
 struct Entry{const char *name;IMP implementation;const char *types;};
 const Entry entries[]={{"newCommandQueue",reinterpret_cast<IMP>(deviceQueue),"@16@0:8"},{"newCommandQueueWithMaxCommandBufferCount:",reinterpret_cast<IMP>(deviceQueueCount),"@24@0:8Q16"},{"newCommandQueueWithDescriptor:",reinterpret_cast<IMP>(deviceQueueDescriptor),"@24@0:8@16"}};
 for(const auto &e:entries)if(!class_addMethod(cls,sel_registerName(e.name),e.implementation,e.types))return NO;return YES;
}
