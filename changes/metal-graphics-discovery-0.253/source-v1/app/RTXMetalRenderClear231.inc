// Real render-pass color clears are queued as GPU work on the existing native
// compute channel. Drawing and presentation remain unsupported.
#include "RTXRenderClear231.hpp"
#include "RTXClearKernel231.hpp"
#include <cstdlib>

// The SDK protocol supplies exact signatures for unsupported entry points.
// Forwarding records a command error instead of silently accepting a draw.
static const char *renderMethodTypes231(Protocol *protocol,SEL selector) {
 for(BOOL required:{YES,NO}) {
  const auto method=protocol_getMethodDescription(protocol,selector,required,YES);
  if(method.name&&method.types)return method.types;
 }
 unsigned count=0;Protocol *__unsafe_unretained *parents=protocol_copyProtocolList(protocol,&count);
 const char *found=nullptr;
 for(unsigned i=0;i<count&&!found;++i)found=renderMethodTypes231(parents[i],selector);
 std::free(parents);return found;
}
@interface RTXApplication209_RTXRenderClearEncoder231:NSObject {
 @public RTXApplication209_RTXCommandBuffer036 *_buffer;NSString *_label;
 BOOL _ended;NSUInteger _debugDepth;
}
- (id)initWithBuffer:(RTXApplication209_RTXCommandBuffer036*)buffer;
- (void)guardEncoding;
@end
@implementation RTXApplication209_RTXRenderClearEncoder231
- (id)init{[self release];return nil;}
- (id)initWithBuffer:(RTXApplication209_RTXCommandBuffer036*)buffer {
 if((self=[super init]))_buffer=[buffer retain];return self;
}
- (void)dealloc {
 if(_buffer){std::lock_guard<std::mutex>lock(_buffer->_queue->_context->mutex);
  if(_buffer->_active==self){_buffer->_active=nil;rememberError(_buffer,@"Render encoder was released without endEncoding");}}
 [_label release];[_buffer release];[super dealloc];
}
- (id<MTLDevice>)device{return _buffer.device;}
- (NSString*)label{std::lock_guard<std::mutex>lock(_buffer->_queue->_context->mutex);return [[_label retain]autorelease];}
- (void)setLabel:(NSString*)label{std::lock_guard<std::mutex>lock(_buffer->_queue->_context->mutex);NSString*copy=[label copy];[_label release];_label=copy;}
- (void)guardEncoding{[_buffer guardEncoding];if(_ended||_buffer->_active!=self)misuse(@"The render encoder has ended");}
- (void)endEncoding {
 std::lock_guard<std::mutex>lock(_buffer->_queue->_context->mutex);[self guardEncoding];
 if(_debugDepth)rememberError(_buffer,@"Unbalanced render encoder debug groups");
 _ended=YES;_buffer->_active=nil;
}
- (void)insertDebugSignpost:(NSString*)string{std::lock_guard<std::mutex>lock(_buffer->_queue->_context->mutex);[self guardEncoding];if(!string)rememberError(_buffer,@"Invalid render signpost");}
- (void)pushDebugGroup:(NSString*)string{std::lock_guard<std::mutex>lock(_buffer->_queue->_context->mutex);[self guardEncoding];if(!string||_debugDepth==1024)rememberError(_buffer,@"Invalid render debug group");else ++_debugDepth;}
- (void)popDebugGroup{std::lock_guard<std::mutex>lock(_buffer->_queue->_context->mutex);[self guardEncoding];if(!_debugDepth)rememberError(_buffer,@"Unbalanced render debug group");else --_debugDepth;}
- (NSMethodSignature*)methodSignatureForSelector:(SEL)selector {
 NSMethodSignature*known=[super methodSignatureForSelector:selector];if(known)return known;
 const char*types=renderMethodTypes231(@protocol(MTLRenderCommandEncoder),selector);
 return types?[NSMethodSignature signatureWithObjCTypes:types]:nil;
}
- (BOOL)respondsToSelector:(SEL)selector {
 return [super respondsToSelector:selector]||renderMethodTypes231(@protocol(MTLRenderCommandEncoder),selector)!=nullptr;
}
- (void)forwardInvocation:(NSInvocation*)invocation {
 if(!renderMethodTypes231(@protocol(MTLRenderCommandEncoder),invocation.selector)){[super forwardInvocation:invocation];return;}
 std::lock_guard<std::mutex>lock(_buffer->_queue->_context->mutex);[self guardEncoding];
 rememberError(_buffer,[NSString stringWithFormat:@"Render feature %@ is not implemented",NSStringFromSelector(invocation.selector)]);
 const auto length=invocation.methodSignature.methodReturnLength;
 if(length){std::vector<unsigned char>zero(length,0);[invocation setReturnValue:zero.data()];}
}
@end

// Called only while the command-context mutex is held. Build every operation
// locally before publishing any of them to the command buffer.
static id<MTLRenderCommandEncoder> newRenderClearEncoder231(
    RTXApplication209_RTXCommandBuffer036 *buffer,MTLRenderPassDescriptor *descriptor) {
 auto fail=[&](NSString*message)->id<MTLRenderCommandEncoder>{rememberError(buffer,message);return nil;};
 if(![descriptor isKindOfClass:[MTLRenderPassDescriptor class]])return fail(@"Invalid render pass descriptor");
 MTLRenderPassDescriptor*d=[[descriptor copy]autorelease];
 if(d.depthAttachment.texture||d.depthAttachment.resolveTexture||d.stencilAttachment.texture||d.stencilAttachment.resolveTexture||
    d.visibilityResultBuffer||d.rasterizationRateMap||d.renderTargetArrayLength>1||d.defaultRasterSampleCount>1||
    d.tileWidth||d.tileHeight||d.threadgroupMemoryLength||d.imageblockSampleLength||[d getSamplePositions:nullptr count:0])
  return fail(@"Render pass configuration requires unimplemented depth, sampling or tile support");
 for(NSUInteger i=0;i<4;++i)if(d.sampleBufferAttachments[i].sampleBuffer)return fail(@"Render counters are not implemented");
 NSError*error=nil;
 NSData*image=[NSData dataWithBytes:RTXClear231::Image length:RTXClear231::ImageBytes];
 id<MTLLibrary>library=[RTXNewCompiledLibrary(buffer.device,image,&error)autorelease];
 id<MTLFunction>function=[[library newFunctionWithName:@"rtx_clear_color231"]autorelease];
 id<MTLComputePipelineState>pipeline=[RTXNewComputePipeline(buffer.device,function,&error)autorelease];
 if(!library||!pipeline||error)return fail(@"Internal color-clear program is unavailable");
 std::vector<std::unique_ptr<Operation036>> operations;NSUInteger width=0,height=0;
 for(NSUInteger i=0;i<8;++i) {
  auto*a=d.colorAttachments[i];
  if(a.resolveTexture||a.level||a.slice||a.depthPlane||a.storeActionOptions)return fail(@"Render resolve, subresource or store options are not implemented");
  if(!a.texture)continue;
  if(a.loadAction!=MTLLoadActionClear||a.storeAction!=MTLStoreActionStore)return fail(@"This render path currently supports color clear followed by store");
  auto op=std::make_unique<Operation036>();op->pipeline=[pipeline retain];op->library=[library retain];op->count=3;
  uint8_t descriptorBytes[32];
  if(!RTXCopyRenderTargetBinding231(a.texture,buffer.device,&op->buffers[1],&op->offsets[1],descriptorBytes))return fail(@"Invalid, busy or unsupported color render target");
  for(const auto&other:operations)if(other->buffers[1]==op->buffers[1])return fail(@"Color attachments must use distinct backing resources");
  const auto color=a.clearColor;RTXRenderClear231::Plan plan;
  if(!RTXRenderClear231::make(descriptorBytes,op->buffers[1].length,op->offsets[1],{color.red,color.green,color.blue,color.alpha},plan))return fail(@"Color clear exceeds the supported format, range or backing limits");
  if((width&&width!=plan.width)||(height&&height!=plan.height))return fail(@"Color attachments must have matching dimensions");
  width=plan.width;height=plan.height;
  op->buffers[0]=[buffer.device newBufferWithBytes:plan.color.data()length:plan.color.size()options:MTLResourceStorageModeShared];
  op->buffers[2]=[buffer.device newBufferWithBytes:plan.descriptors.data()length:plan.descriptors.size()options:MTLResourceStorageModeShared];
  if(!op->buffers[0]||!op->buffers[2])return fail(@"Unable to allocate immutable color-clear arguments");
  op->indices[0]=0;op->indices[1]=1;op->indices[2]=2;
  op->groups=MTLSizeMake(plan.groupsX,plan.groupsY,1);op->threads=MTLSizeMake(64,1,1);
  if(!RTXValidatePipelineDispatch(pipeline,op->groups,op->threads))return fail(@"Invalid color-clear dispatch");
  operations.push_back(std::move(op));
 }
 if(operations.empty()||buffer->_operations.size()+operations.size()>1024||
    (d.renderTargetWidth&&d.renderTargetWidth!=width)||(d.renderTargetHeight&&d.renderTargetHeight!=height))
  return fail(@"Color clear needs matching nonempty attachments within command limits");
 auto*encoder=[[RTXApplication209_RTXRenderClearEncoder231 alloc]initWithBuffer:buffer];
 if(!encoder)return fail(@"Unable to allocate render encoder");
 for(auto&operation:operations)buffer->_operations.push_back(std::move(operation));
 buffer->_active=encoder;return (id<MTLRenderCommandEncoder>)[encoder autorelease];
}
