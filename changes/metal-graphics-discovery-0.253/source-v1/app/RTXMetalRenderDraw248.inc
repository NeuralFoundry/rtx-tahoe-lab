#include "RTXDrawTransfer248.hpp"
@interface RTXApplication209_RTXRenderDrawEncoder248:RTXApplication209_RTXRenderClearEncoder231 {
 @public id<MTLRenderPipelineState>_pipeline;id<MTLBuffer>_vertex;
 id<MTLBuffer>_color;NSUInteger _vertexOffset,_colorOffset,_width,_height,_pitch;
}
@end
@implementation RTXApplication209_RTXRenderDrawEncoder248
- (void)dealloc{[_pipeline release];[_vertex release];[_color release];[super dealloc];}
- (void)setRenderPipelineState:(id<MTLRenderPipelineState>)pipeline{
 std::lock_guard<std::mutex>lock(_buffer->_queue->_context->mutex);[self guardEncoding];NSData*container=RTXCopyRenderPipelineContainer247(pipeline);
 if(!container||pipeline.device!=self.device){[container release];rememberError(_buffer,@"Invalid graphics pipeline for this device");return;}[container release];[pipeline retain];[_pipeline release];_pipeline=pipeline;
}
- (void)setVertexBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index{
 std::lock_guard<std::mutex>lock(_buffer->_queue->_context->mutex);[self guardEncoding];
 if(index|| (buffer&&(buffer.device!=self.device||offset>buffer.length||(offset&3)))){rememberError(_buffer,@"Unsupported vertex binding");return;}[buffer retain];[_vertex release];_vertex=buffer;_vertexOffset=offset;
}
- (void)setViewport:(MTLViewport)v{
 std::lock_guard<std::mutex>lock(_buffer->_queue->_context->mutex);[self guardEncoding];if(v.originX!=0||v.originY!=0||v.width!=_width||v.height!=_height||v.znear!=0||v.zfar!=1)rememberError(_buffer,@"This graphics path requires a full-target viewport");
}
- (void)setScissorRect:(MTLScissorRect)r{std::lock_guard<std::mutex>lock(_buffer->_queue->_context->mutex);[self guardEncoding];if(r.x||r.y||r.width!=_width||r.height!=_height)rememberError(_buffer,@"This graphics path requires a full-target scissor");}
- (void)setCullMode:(MTLCullMode)mode{std::lock_guard<std::mutex>lock(_buffer->_queue->_context->mutex);[self guardEncoding];if(mode!=MTLCullModeNone)rememberError(_buffer,@"Culling is not implemented");}
- (void)setFrontFacingWinding:(MTLWinding)winding{std::lock_guard<std::mutex>lock(_buffer->_queue->_context->mutex);[self guardEncoding];if(winding!=MTLWindingClockwise&&winding!=MTLWindingCounterClockwise)rememberError(_buffer,@"Invalid winding");}
- (void)setTriangleFillMode:(MTLTriangleFillMode)mode{std::lock_guard<std::mutex>lock(_buffer->_queue->_context->mutex);[self guardEncoding];if(mode!=MTLTriangleFillModeFill)rememberError(_buffer,@"Wireframe is not implemented");}
- (void)drawPrimitives:(MTLPrimitiveType)type vertexStart:(NSUInteger)first vertexCount:(NSUInteger)count{
 std::lock_guard<std::mutex>lock(_buffer->_queue->_context->mutex);[self guardEncoding];if(!count)return;
 if(type!=MTLPrimitiveTypeTriangle||!_pipeline||!_vertex||first>UINT32_MAX||count>RTXDrawTransfer248::MaxVertices||count%3||_vertex==_color||_buffer->_operations.size()>=1024){rememberError(_buffer,@"Invalid or unsupported triangle draw");return;}
 RTXDrawTransfer248::Input input;input.generation=1;input.serial=1;input.vertexBytes=_vertex.length;input.colorBytes=_color.length;input.vertexOffset=_vertexOffset;input.colorOffset=_colorOffset;input.firstVertex=uint32_t(first);input.vertexCount=uint32_t(count);input.width=uint32_t(_width);input.height=uint32_t(_height);input.pitch=uint32_t(_pitch);input.programDigest[0]=1;RTXDrawTransfer248::Plan plan;
 if(!RTXDrawTransfer248::plan(input,plan)){rememberError(_buffer,@"Draw ranges exceed their resources");return;}
 auto operation=std::make_unique<Operation036>();operation->draw=std::make_unique<DrawOperation248>();auto&d=*operation->draw;d.pipeline=[_pipeline retain];d.vertex=[_vertex retain];d.color=[_color retain];d.vertexOffset=_vertexOffset;d.colorOffset=_colorOffset;d.first=first;d.count=count;d.width=_width;d.height=_height;d.pitch=_pitch;_buffer->_operations.push_back(std::move(operation));
}
@end
static id<MTLRenderCommandEncoder>newRenderDrawEncoder248(RTXApplication209_RTXCommandBuffer036*buffer,MTLRenderPassDescriptor*descriptor){
 auto fail=[&](NSString*m)->id<MTLRenderCommandEncoder>{rememberError(buffer,m);return nil;};MTLRenderPassDescriptor*d=[[descriptor copy]autorelease];
 if(!buffer->_queue->_context->graphics)return fail(@"No graphics submission transport is configured");
 if(d.depthAttachment.texture||d.depthAttachment.resolveTexture||d.stencilAttachment.texture||d.stencilAttachment.resolveTexture||d.visibilityResultBuffer||d.rasterizationRateMap||d.renderTargetArrayLength>1||d.defaultRasterSampleCount>1||d.tileWidth||d.tileHeight||d.threadgroupMemoryLength||d.imageblockSampleLength||[d getSamplePositions:nullptr count:0])return fail(@"Unsupported graphics render pass");
 for(NSUInteger n=0;n<4;++n)if(d.sampleBufferAttachments[n].sampleBuffer)return fail(@"Render counters are not implemented");
 for(NSUInteger n=0;n<8;++n){auto*a=d.colorAttachments[n];if(a.resolveTexture||a.level||a.slice||a.depthPlane||a.storeActionOptions||(n&&a.texture))return fail(@"Unsupported color attachment");}
 auto*a=d.colorAttachments[0];if(!a.texture||a.loadAction!=MTLLoadActionLoad||a.storeAction!=MTLStoreActionStore||a.texture.pixelFormat!=MTLPixelFormatRGBA8Unorm)return fail(@"Triangle rendering requires one RGBA8Unorm load/store target");
 id<MTLBuffer>color=nil;NSUInteger offset=0;uint8_t raw[32];if(!RTXCopyRenderTargetBinding231(a.texture,buffer.device,&color,&offset,raw))return fail(@"Invalid render target storage");
 const NSUInteger width=RTXDrawTransfer248::u32(raw+8),height=RTXDrawTransfer248::u32(raw+12),pitch=RTXDrawTransfer248::u32(raw+16);
 if(!width||width>512||!height||height>512||color.length>RTXDrawTransfer248::MaxColorBytes||(d.renderTargetWidth&&d.renderTargetWidth!=width)||(d.renderTargetHeight&&d.renderTargetHeight!=height)){[color release];return fail(@"Render target exceeds the draw transfer bounds");}
 auto*e=[[RTXApplication209_RTXRenderDrawEncoder248 alloc]initWithBuffer:buffer];if(!e){[color release];return fail(@"Unable to allocate draw encoder");}e->_color=color;e->_colorOffset=offset;e->_width=width;e->_height=height;e->_pitch=pitch;buffer->_active=e;return (id<MTLRenderCommandEncoder>)[e autorelease];
}
