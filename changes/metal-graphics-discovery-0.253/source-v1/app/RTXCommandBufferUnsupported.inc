// Unsupported SDK operations explicitly record an error (queue methods raise).
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
NS_ASSUME_NONNULL_BEGIN
- (void)presentDrawable:(id <MTLDrawable>)drawable { [self unsupported:_cmd]; }
- (void)presentDrawable:(id <MTLDrawable>)drawable atTime:(CFTimeInterval)presentationTime { [self unsupported:_cmd]; }
- (void)presentDrawable:(id <MTLDrawable>)drawable afterMinimumDuration:(CFTimeInterval)duration { [self unsupported:_cmd]; }
- (nullable id <MTLBlitCommandEncoder>)blitCommandEncoder { [self unsupported:_cmd]; return nil; }
- (nullable id <MTLBlitCommandEncoder>)blitCommandEncoderWithDescriptor:(MTLBlitPassDescriptor *)blitPassDescriptor { [self unsupported:_cmd]; return nil; }
- (void)encodeWaitForEvent:(id <MTLEvent>)event value:(uint64_t)value { [self unsupported:_cmd]; }
- (void)encodeSignalEvent:(id <MTLEvent>)event value:(uint64_t)value { [self unsupported:_cmd]; }
- (nullable id <MTLParallelRenderCommandEncoder>)parallelRenderCommandEncoderWithDescriptor:(MTLRenderPassDescriptor *)renderPassDescriptor { [self unsupported:_cmd]; return nil; }
- (nullable id<MTLResourceStateCommandEncoder>) resourceStateCommandEncoder { [self unsupported:_cmd]; return nil; }
- (nullable id<MTLResourceStateCommandEncoder>) resourceStateCommandEncoderWithDescriptor:(MTLResourceStatePassDescriptor *) resourceStatePassDescriptor { [self unsupported:_cmd]; return nil; }
- (nullable id <MTLAccelerationStructureCommandEncoder>)accelerationStructureCommandEncoder { [self unsupported:_cmd]; return nil; }
- (id <MTLAccelerationStructureCommandEncoder>)accelerationStructureCommandEncoderWithDescriptor:(MTLAccelerationStructurePassDescriptor *)descriptor { [self unsupported:_cmd]; return nil; }
- (void)useResidencySet:(id <MTLResidencySet>)residencySet { [self unsupported:_cmd]; }
- (void)useResidencySets:(const id <MTLResidencySet> _Nonnull[_Nonnull])residencySets count:(NSUInteger)count { [self unsupported:_cmd]; }
NS_ASSUME_NONNULL_END
#pragma clang diagnostic pop
