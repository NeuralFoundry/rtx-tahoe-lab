// Included beside the standard buffer implementation. All heaps and buffers
// on one device use its existing arena mutex and allocation budget.
static bool sharedOptions(MTLResourceOptions options);
@implementation RTXApplication209_RTXMetalHeap117
- (instancetype)init{[self release];return nil;}
- (instancetype)initWithArena:(RTXApplication209_RTXBufferArena *)arena memory:(void *)memory layout:(RTXHeap117::Layout *)layout{
 self=[super init];if(!self)return nil;
 _arena=[arena retain];_retainedDevice=[arena.device retain];_bytes=memory;_layout=layout;
 _purge=MTLPurgeableStateNonVolatile;_epoch=1;return self;
}
- (void)dealloc{
 if(_arena){
  {std::lock_guard<std::mutex> lock(_arena->_state->mutex);
   NSCAssert(_layout&&_layout->used()==0&&_arena->_state->charged>=_layout->capacity(),@"Live heap allocation lost its owner");
   _arena->_state->charged-=_layout->capacity();std::free(_bytes);delete _layout;
  }
  [_arena release];
 }
 [_label release];[_retainedDevice release];[super dealloc];
}
- (id<MTLDevice>)device{return _retainedDevice;}
- (MTLHeapType)type{return MTLHeapTypeAutomatic;}
- (MTLStorageMode)storageMode{return MTLStorageModeShared;}
- (MTLCPUCacheMode)cpuCacheMode{return MTLCPUCacheModeDefaultCache;}
- (MTLHazardTrackingMode)hazardTrackingMode{return MTLHazardTrackingModeTracked;}
- (MTLResourceOptions)resourceOptions{return MTLResourceStorageModeShared|MTLResourceHazardTrackingModeTracked;}
- (NSUInteger)size{return _layout->capacity();}
- (NSUInteger)currentAllocatedSize{return _layout->capacity();}
- (NSUInteger)allocatedSize{return _layout->capacity();}
- (NSUInteger)usedSize{std::lock_guard<std::mutex> lock(_arena->_state->mutex);return _layout->used();}
- (NSString *)label{std::lock_guard<std::mutex> lock(_arena->_state->mutex);return [[_label retain]autorelease];}
- (void)setLabel:(NSString *)label{
 NSString *copy=[label copy],*old=nil;
 {std::lock_guard<std::mutex> lock(_arena->_state->mutex);old=_label;_label=copy;}[old release];
}
- (NSUInteger)maxAvailableSizeWithAlignment:(NSUInteger)alignment{
 if(!alignment||(alignment&(alignment-1)))[NSException raise:NSInvalidArgumentException format:@"Heap alignment must be a nonzero power of two"];
 std::lock_guard<std::mutex> lock(_arena->_state->mutex);
 if(_arena->_state->closed||_purge==MTLPurgeableStateEmpty)return 0;
 return std::min(std::size_t(MaxBuffer),_layout->maxAvailable(alignment,reinterpret_cast<std::uintptr_t>(_bytes)));
}
- (MTLPurgeableState)setPurgeableState:(MTLPurgeableState)value{
 if(value<MTLPurgeableStateKeepCurrent||value>MTLPurgeableStateEmpty)[NSException raise:NSInvalidArgumentException format:@"Invalid heap purgeable state"];
 std::lock_guard<std::mutex> lock(_arena->_state->mutex);const auto prior=_purge;
 if(value!=MTLPurgeableStateKeepCurrent){
  if(value==MTLPurgeableStateEmpty){
   if(_epoch==std::numeric_limits<uint64_t>::max())[NSException raise:NSInternalInconsistencyException format:@"Heap generation exhausted"];
   std::memset(_bytes,0,_layout->capacity());++_epoch;
  }
  _purge=value;
 }
 return value==MTLPurgeableStateKeepCurrent?_purge:prior;
}
- (id<MTLBuffer>)newBufferWithLength:(NSUInteger)length options:(MTLResourceOptions)options{
 if(!sharedOptions(options))return nil;
 std::lock_guard<std::mutex> lock(_arena->_state->mutex);
 if(_arena->_state->closed||_purge==MTLPurgeableStateEmpty)return nil;
 RTXHeap117::Layout::Allocation allocation;
 if(!_layout->allocate(length,allocation))return nil;
 void *memory=static_cast<unsigned char *>(_bytes)+allocation.offset;
 std::memset(memory,0,allocation.size);
 RTXApplication209_RTXMetalBuffer036 *buffer=[[RTXApplication209_RTXMetalBuffer036 alloc]initWithArena:_arena length:length allocation:allocation.size memory:memory];
 if(!buffer){bool released=_layout->release(allocation);NSCAssert(released,@"Heap allocation rollback failed");return nil;}
 buffer->_heap=[self retain];buffer->_heapAllocation=allocation;++_arena->_state->count;
 return buffer;
}
- (id<MTLBuffer>)newBufferWithLength:(NSUInteger)length options:(MTLResourceOptions)options offset:(NSUInteger)offset{
 (void)length;(void)options;(void)offset;return nil; // Placement heaps are not implemented.
}
- (id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor{(void)descriptor;return nil;}
- (id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor offset:(NSUInteger)offset{(void)descriptor;(void)offset;return nil;}
- (id<MTLAccelerationStructure>)newAccelerationStructureWithSize:(NSUInteger)size{(void)size;return nil;}
- (id<MTLAccelerationStructure>)newAccelerationStructureWithSize:(NSUInteger)size offset:(NSUInteger)offset{(void)size;(void)offset;return nil;}
- (id<MTLAccelerationStructure>)newAccelerationStructureWithDescriptor:(MTLAccelerationStructureDescriptor *)descriptor{(void)descriptor;return nil;}
- (id<MTLAccelerationStructure>)newAccelerationStructureWithDescriptor:(MTLAccelerationStructureDescriptor *)descriptor offset:(NSUInteger)offset{(void)descriptor;(void)offset;return nil;}
@end

static MTLSizeAndAlign metalHeapBufferLayout117(id,SEL,NSUInteger length,MTLResourceOptions options){
 RTXHeap117::SizeAndAlign shape{};
 if(!sharedOptions(options)||!RTXHeap117::bufferLayout(length,shape))
  [NSException raise:NSInvalidArgumentException format:@"Unsupported heap buffer length or options"];
 return {shape.size,shape.align};
}
static id<MTLHeap> metalNewHeap117(id device,SEL,MTLHeapDescriptor *descriptor){
 if(![descriptor isKindOfClass:MTLHeapDescriptor.class])return nil;
 // Snapshot the public descriptor before taking the arena lock. This first
 // implementation accepts explicit shared/tracked automatic heaps only.
 const auto length=descriptor.size;const auto type=descriptor.type;
 const auto storage=descriptor.storageMode;const auto cache=descriptor.cpuCacheMode;
 const auto hazard=descriptor.hazardTrackingMode;
 std::size_t capacity=0;
 if(type!=MTLHeapTypeAutomatic||storage!=MTLStorageModeShared||cache!=MTLCPUCacheModeDefaultCache||
    hazard!=MTLHazardTrackingModeTracked||!RTXHeap117::heapLayout(length,capacity))return nil;
 RTXApplication209_RTXBufferArena *arena=metalArena(device,true);if(!arena)return nil;
 std::lock_guard<std::mutex> lock(arena->_state->mutex);
 if(arena->_state->closed||capacity>arena->_state->budget-arena->_state->charged)return nil;
 auto *layout=new(std::nothrow) RTXHeap117::Layout(capacity);
 if(!layout||!layout->valid()){delete layout;return nil;}
 void *memory=nullptr;if(posix_memalign(&memory,Page,capacity)){delete layout;return nil;}
 std::memset(memory,0,capacity);
 RTXApplication209_RTXMetalHeap117 *heap=[[RTXApplication209_RTXMetalHeap117 alloc]initWithArena:arena memory:memory layout:layout];
 if(!heap){std::free(memory);delete layout;return nil;}
 arena->_state->charged+=capacity;return heap;
}
