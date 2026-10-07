// Unsupported SDK operations explicitly record an error (queue methods raise).
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
NS_ASSUME_NONNULL_BEGIN
- (void)setBytes:(const void *)bytes length:(NSUInteger)length atIndex:(NSUInteger)index { [self unsupported:_cmd]; }
- (void) setBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset attributeStride:(NSUInteger)stride atIndex:(NSUInteger)index { [self unsupported:_cmd]; }
- (void) setBuffers:(const id<MTLBuffer> __nullable [__nonnull])buffers offsets:(const NSUInteger [__nonnull])offsets attributeStrides:(const NSUInteger [__nonnull])strides withRange:(NSRange)range { [self unsupported:_cmd]; }
- (void) setBufferOffset:(NSUInteger)offset attributeStride:(NSUInteger)stride atIndex:(NSUInteger)index { [self unsupported:_cmd]; }
- (void)setBytes:(void const *)bytes length:(NSUInteger)length attributeStride:(NSUInteger)stride atIndex:(NSUInteger)index { [self unsupported:_cmd]; }
- (void)setVisibleFunctionTable:(nullable id <MTLVisibleFunctionTable>)visibleFunctionTable atBufferIndex:(NSUInteger)bufferIndex { [self unsupported:_cmd]; }
- (void)setVisibleFunctionTables:(const id <MTLVisibleFunctionTable> __nullable [__nonnull])visibleFunctionTables withBufferRange:(NSRange)range { [self unsupported:_cmd]; }
- (void)setIntersectionFunctionTable:(nullable id <MTLIntersectionFunctionTable>)intersectionFunctionTable atBufferIndex:(NSUInteger)bufferIndex { [self unsupported:_cmd]; }
- (void)setIntersectionFunctionTables:(const id <MTLIntersectionFunctionTable> __nullable [__nonnull])intersectionFunctionTables withBufferRange:(NSRange)range { [self unsupported:_cmd]; }
- (void)setAccelerationStructure:(nullable id <MTLAccelerationStructure>)accelerationStructure atBufferIndex:(NSUInteger)bufferIndex { [self unsupported:_cmd]; }
- (void)setTexture:(nullable id <MTLTexture>)texture atIndex:(NSUInteger)index { [self unsupported:_cmd]; }
- (void)setTextures:(const id <MTLTexture> __nullable [__nonnull])textures withRange:(NSRange)range { [self unsupported:_cmd]; }
- (void)setSamplerState:(nullable id <MTLSamplerState>)sampler atIndex:(NSUInteger)index { [self unsupported:_cmd]; }
- (void)setSamplerStates:(const id <MTLSamplerState> __nullable [__nonnull])samplers withRange:(NSRange)range { [self unsupported:_cmd]; }
- (void)setSamplerState:(nullable id <MTLSamplerState>)sampler lodMinClamp:(float)lodMinClamp lodMaxClamp:(float)lodMaxClamp atIndex:(NSUInteger)index { [self unsupported:_cmd]; }
- (void)setSamplerStates:(const id <MTLSamplerState> __nullable [__nonnull])samplers lodMinClamps:(const float [__nonnull])lodMinClamps lodMaxClamps:(const float [__nonnull])lodMaxClamps withRange:(NSRange)range { [self unsupported:_cmd]; }
- (void)setThreadgroupMemoryLength:(NSUInteger)length atIndex:(NSUInteger)index { [self unsupported:_cmd]; }
- (void)setImageblockWidth:(NSUInteger)width height:(NSUInteger)height { [self unsupported:_cmd]; }
- (void)setStageInRegion:(MTLRegion)region { [self unsupported:_cmd]; }
- (void)setStageInRegionWithIndirectBuffer:(id <MTLBuffer>)indirectBuffer indirectBufferOffset:(NSUInteger)indirectBufferOffset { [self unsupported:_cmd]; }
- (void)dispatchThreadgroupsWithIndirectBuffer:(id <MTLBuffer>)indirectBuffer indirectBufferOffset:(NSUInteger)indirectBufferOffset threadsPerThreadgroup:(MTLSize)threadsPerThreadgroup { [self unsupported:_cmd]; }
- (void)updateFence:(id <MTLFence>)fence { [self unsupported:_cmd]; }
- (void)waitForFence:(id <MTLFence>)fence { [self unsupported:_cmd]; }
- (void)useResource:(id <MTLResource>)resource usage:(MTLResourceUsage)usage { [self unsupported:_cmd]; }
- (void)useResources:(const id <MTLResource> __nonnull[__nonnull])resources count:(NSUInteger)count usage:(MTLResourceUsage)usage { [self unsupported:_cmd]; }
- (void)useHeap:(id <MTLHeap>)heap { [self unsupported:_cmd]; }
- (void)useHeaps:(const id <MTLHeap> __nonnull[__nonnull])heaps count:(NSUInteger)count { [self unsupported:_cmd]; }
- (void)executeCommandsInBuffer:(id<MTLIndirectCommandBuffer>)indirectCommandBuffer withRange:(NSRange)executionRange { [self unsupported:_cmd]; }
- (void)executeCommandsInBuffer:(id<MTLIndirectCommandBuffer>)indirectCommandbuffer indirectBuffer:(id<MTLBuffer>)indirectRangeBuffer indirectBufferOffset:(NSUInteger)indirectBufferOffset { [self unsupported:_cmd]; }
-(void)sampleCountersInBuffer:(id<MTLCounterSampleBuffer>)sampleBuffer atSampleIndex:(NSUInteger)sampleIndex withBarrier:(BOOL)barrier { [self unsupported:_cmd]; }
NS_ASSUME_NONNULL_END
#pragma clang diagnostic pop
