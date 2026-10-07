#include "RTXDrawTransfer248.hpp"
@interface RTXApplication209_RTXDrawSubmission248:NSObject {
 @public RTXApplication209_RTXBufferArena*_arena;
 RTXApplication209_RTXMetalBuffer036*_vertex,*_color;uint64_t _vertexRevision,_colorRevision,_vertexEpoch,_colorEpoch;
 RTXDrawTransfer248::Plan _plan;NSData*_request;BOOL _terminal;
}
- (instancetype)initTransaction;
- (void)cancel;
- (BOOL)publish:(NSData*)result request:(NSData*)request completion:(uint64_t)completion;
@end
@implementation RTXApplication209_RTXDrawSubmission248
- (instancetype)init{[self release];return nil;}
- (instancetype)initTransaction{return [super init];}
- (void)dealloc{[self cancel];[_vertex release];[_color release];[_request release];[_arena release];[super dealloc];}
- (void)cancel{if(!_arena)return;std::lock_guard<std::mutex>lock(_arena->_state->mutex);if(!_terminal){_terminal=YES;_vertex->_pendingOutput=false;_color->_pendingOutput=false;}}
- (BOOL)publish:(NSData*)data request:(NSData*)request completion:(uint64_t)completion{
 if(![data isKindOfClass:[NSData class]]||data.length!=_plan.payloadBytes||![request isKindOfClass:[NSData class]]||![request isEqualToData:_request])return NO;
 RTXDrawTransfer248::Bytes copy(data.length),wire(_request.length);[data getBytes:copy.data()length:copy.size()];[_request getBytes:wire.data()length:wire.size()];
 if(!RTXDrawTransfer248::readback(_plan,wire,copy.data(),copy.size(),completion))return NO;
 std::lock_guard<std::mutex>lock(_arena->_state->mutex);if(_terminal)return NO;_terminal=YES;_vertex->_pendingOutput=false;_color->_pendingOutput=false;
 if(_arena->_state->closed||!bufferAvailable(_vertex)||!bufferAvailable(_color)||heapEpoch(_vertex)!=_vertexEpoch||heapEpoch(_color)!=_colorEpoch||
    _vertex->_revision!=_vertexRevision||_color->_revision!=_colorRevision||_color->_revision==UINT64_MAX||
    std::memcmp(_vertex->_bytes,wire.data()+RTXDrawTransfer248::Header,size_t(_plan.vertexBytes))||
    std::memcmp(_color->_bytes,wire.data()+RTXDrawTransfer248::Header+_plan.vertexBytes,size_t(_plan.colorBytes)))return NO;
 std::memcpy(_color->_bytes,copy.data()+_plan.vertexBytes,size_t(_plan.colorBytes));++_color->_revision;return YES;
}
@end
static id newDrawSubmission248(id<MTLRenderPipelineState>pipeline,id<MTLBuffer>vertex,NSUInteger vertexOffset,NSUInteger firstVertex,NSUInteger vertexCount,id<MTLBuffer>color,NSUInteger colorOffset,NSUInteger width,NSUInteger height,NSUInteger pitch,uint64_t generation,uint64_t serial,NSError**error){
 if(error)*error=nil;
 if(object_getClass(vertex)!=[RTXApplication209_RTXMetalBuffer036 class]||object_getClass(color)!=[RTXApplication209_RTXMetalBuffer036 class]||vertex==color||vertex.device!=pipeline.device||color.device!=pipeline.device||
    firstVertex>UINT32_MAX||vertexCount>UINT32_MAX||width>UINT32_MAX||height>UINT32_MAX||pitch>UINT32_MAX)return bufferFailure(error,@"Invalid graphics resource identity or dimensions");
 NSData*catalog=RTXCopyRenderPipelineContainer247(pipeline);if(!catalog||catalog.length!=4160){[catalog release];return bufferFailure(error,@"Invalid graphics pipeline");}
 RTXDrawTransfer248::Input input;input.generation=generation;input.serial=serial;input.vertexBytes=vertex.length;input.colorBytes=color.length;input.vertexOffset=vertexOffset;input.colorOffset=colorOffset;input.firstVertex=uint32_t(firstVertex);input.vertexCount=uint32_t(vertexCount);input.width=uint32_t(width);input.height=uint32_t(height);input.pitch=uint32_t(pitch);std::memcpy(input.programDigest.data(),static_cast<const uint8_t*>(catalog.bytes)+16,32);[catalog release];
 RTXDrawTransfer248::Plan plan;if(!RTXDrawTransfer248::plan(input,plan))return bufferFailure(error,@"Graphics bindings exceed the bounded draw transfer contract");
 auto*v=(RTXApplication209_RTXMetalBuffer036*)vertex;auto*c=(RTXApplication209_RTXMetalBuffer036*)color;auto*arena=v->_arena;if(arena!=c->_arena)return bufferFailure(error,@"Graphics buffers need one device arena");
 std::lock_guard<std::mutex>lock(arena->_state->mutex);
 if(arena->_state->closed||v->_pendingOutput||c->_pendingOutput||!bufferAvailable(v)||!bufferAvailable(c)||c->_revision==UINT64_MAX)return bufferFailure(error,@"Graphics storage is unavailable or already in use");
 RTXDrawTransfer248::Bytes a(static_cast<uint8_t*>(v->_bytes),static_cast<uint8_t*>(v->_bytes)+v->_length),b(static_cast<uint8_t*>(c->_bytes),static_cast<uint8_t*>(c->_bytes)+c->_length),wire;
 if(!RTXDrawTransfer248::assemble(input,a,b,wire))return bufferFailure(error,@"Unable to snapshot finite vertex data and render target");
 NSData*request=[[NSData alloc]initWithBytes:wire.data()length:wire.size()];if(!request)return bufferFailure(error,@"Unable to allocate graphics request");
 auto*t=[[RTXApplication209_RTXDrawSubmission248 alloc]initTransaction];if(!t){[request release];return bufferFailure(error,@"Unable to allocate graphics transaction");}
 t->_plan=plan;t->_request=request;t->_arena=[arena retain];t->_vertex=[v retain];t->_color=[c retain];t->_vertexRevision=v->_revision;t->_colorRevision=c->_revision;t->_vertexEpoch=heapEpoch(v);t->_colorEpoch=heapEpoch(c);v->_pendingOutput=true;c->_pendingOutput=true;return t;
}
extern "C" id RTXNewDrawSubmission248(id<MTLRenderPipelineState>pipeline,id<MTLBuffer>vertex,NSUInteger vertexOffset,NSUInteger firstVertex,NSUInteger vertexCount,id<MTLBuffer>color,NSUInteger colorOffset,NSUInteger width,NSUInteger height,NSUInteger pitch,uint64_t generation,uint64_t serial,NSError**error){
 try{@try{return newDrawSubmission248(pipeline,vertex,vertexOffset,firstVertex,vertexCount,color,colorOffset,width,height,pitch,generation,serial,error);}@catch(NSException*exception){(void)exception;}}catch(...){ }
 return bufferFailure(error,@"Unable to prepare graphics buffers");
}
extern "C" NSData*RTXCopyDrawRequest248(id object){return object_getClass(object)==[RTXApplication209_RTXDrawSubmission248 class]?[((RTXApplication209_RTXDrawSubmission248*)object)->_request copy]:nil;}
extern "C" void RTXCancelDrawSubmission248(id object){if(object_getClass(object)==[RTXApplication209_RTXDrawSubmission248 class])[(RTXApplication209_RTXDrawSubmission248*)object cancel];}
extern "C" BOOL RTXPublishDrawSubmission248(id object,NSData*request,NSData*result,uint64_t completion){
 if(object_getClass(object)!=[RTXApplication209_RTXDrawSubmission248 class])return NO;
 try{@try{return [(RTXApplication209_RTXDrawSubmission248*)object publish:result request:request completion:completion];}@catch(NSException*exception){(void)exception;}}catch(...){ }
 RTXCancelDrawSubmission248(object);return NO;
}
