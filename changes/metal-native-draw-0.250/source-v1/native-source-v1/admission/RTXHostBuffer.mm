#import "RTXHostBuffer.h"
#import "RTXMetalBuffer.h"
#import <objc/runtime.h>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <array>
#include <limits>
#include "RTXSoftwareLimits.hpp"

namespace {
constexpr NSUInteger Page=RTXSoftware039::HostPage, MaxBuffer=RTXSoftware039::MaxBuffer, MaxArena=RTXSoftware039::MaxArena;
bool geometry(NSUInteger length,NSUInteger &allocated) {
  if(!length||length>MaxBuffer)return false;
  allocated=(length+Page-1)&~(Page-1);return true;
}
bool rangeOK(NSRange range,NSUInteger length) {
  return range.location<=length && range.length<=length-range.location;
}
struct ArenaState {
  std::mutex mutex;
  NSUInteger budget,charged=0,count=0;
  bool closed=false;
  explicit ArenaState(NSUInteger value):budget(value){}
};
}
@interface RTXBufferArena () {
@public
  ArenaState *_state;
  id _device;
  bool _borrowsDevice,_metalBuffers;
}
@end
@interface RTXHostBuffer () {
@public
  RTXBufferArena *_arena;
  void *_bytes;
  NSUInteger _length,_allocated;
  NSString *_label;
  uint64_t _revision;
  bool _pendingOutput;
  id _retainedDevice;
  MTLPurgeableState _purge;
}
- (nullable instancetype)initWithArena:(RTXBufferArena *)arena length:(NSUInteger)length
    allocation:(NSUInteger)allocated memory:(void *)memory;
@end

@interface RTXHostSubmission () {
  RTXHostBuffer *_output;
  NSData *_request;
  NSUInteger _offset;
  uint64_t _revision;
  uint64_t _completion;
  bool _terminal;
}
- (nullable instancetype)initWithOutput:(RTXHostBuffer *)output offset:(NSUInteger)offset
    request:(NSData *)request completion:(uint64_t)completion;
@end

@interface RTXMetalBuffer036 : RTXHostBuffer <MTLBuffer>
@end

@implementation RTXBufferArena
- (instancetype)initWithDevice:(id)device budget:(NSUInteger)budget {
  self=[super init];if(!self)return nil;
  if(!device||!budget||budget>MaxArena||budget%Page){[self release];return nil;}
  _state=new(std::nothrow) ArenaState(budget);
  if(!_state){[self release];return nil;}
  _device=[device retain];return self;
}
- (void)dealloc {
  // Every buffer retains the arena, so no live backing can remain here.
  if(_state){NSCAssert(!_state->charged&&!_state->count,@"Live backing lost its arena");delete _state;}
  if(!_borrowsDevice)[_device release];[super dealloc];
}
- (id)device {return _device;}
- (BOOL)closed {std::lock_guard<std::mutex> lock(_state->mutex);return _state->closed;}
- (NSUInteger)allocatedBytes {std::lock_guard<std::mutex> lock(_state->mutex);return _state->charged;}
- (NSUInteger)liveBuffers {std::lock_guard<std::mutex> lock(_state->mutex);return _state->count;}
- (void)close {std::lock_guard<std::mutex> lock(_state->mutex);_state->closed=true;}
- (RTXHostBuffer *)newBufferWithLength:(NSUInteger)length {
  NSUInteger allocated=0;if(!geometry(length,allocated))return nil;
  std::lock_guard<std::mutex> lock(_state->mutex);
  if(_state->closed||allocated>_state->budget-_state->charged)return nil;
  void *bytes=nullptr;
  if(posix_memalign(&bytes,Page,allocated))return nil;
  std::memset(bytes,0,allocated);
  Class bufferClass=_metalBuffers?[RTXMetalBuffer036 class]:[RTXHostBuffer class];
  RTXHostBuffer *buffer=[[bufferClass alloc]initWithArena:self length:length allocation:allocated memory:bytes];
  if(!buffer){std::free(bytes);return nil;}
  _state->charged+=allocated;++_state->count;return buffer;
}
- (RTXHostBuffer *)newBufferWithBytes:(const void *)bytes length:(NSUInteger)length {
  if(!bytes)return nil;
  RTXHostBuffer *buffer=[self newBufferWithLength:length];if(!buffer)return nil;
  if(![buffer writeBytes:bytes range:NSMakeRange(0,length)]){[buffer release];return nil;}
  return buffer;
}
- (RTXHostSubmission *)newSubmissionWithInputA:(RTXHostBuffer *)a offsetA:(NSUInteger)oa
    inputB:(RTXHostBuffer *)b offsetB:(NSUInteger)ob output:(RTXHostBuffer *)output offset:(NSUInteger)oo
    generation:(uint64_t)generation requestID:(uint64_t)requestID program:(uint32_t)program {
  if(!a||!b||!output||a==b||a==output||b==output||a->_arena!=self||b->_arena!=self||output->_arena!=self||
     !generation||!requestID||program>2||((oa|ob|oo)&3)||
     !rangeOK(NSMakeRange(oa,256),a->_length)||!rangeOK(NSMakeRange(ob,256),b->_length)||
     !rangeOK(NSMakeRange(oo,256),output->_length))return nil;
  std::lock_guard<std::mutex> lock(_state->mutex);
  if(_state->closed||output->_pendingOutput||a->_pendingOutput||b->_pendingOutput||
     output->_revision==std::numeric_limits<uint64_t>::max())return nil;
  std::array<unsigned char,2112> wire{};
  auto word=[&](NSUInteger at,uint64_t value,unsigned count){for(unsigned i=0;i<count;++i)wire[at+i]=static_cast<unsigned char>(value>>(8*i));};
  word(0,UINT64_C(0x5254585245513335),8);word(8,1,4);word(12,2112,4);
  word(16,generation,8);word(24,requestID,8);word(32,program,4);word(36,1,4);
  std::memcpy(wire.data()+64,static_cast<unsigned char *>(a->_bytes)+oa,256);
  std::memcpy(wire.data()+320,static_cast<unsigned char *>(b->_bytes)+ob,256);
  std::memcpy(wire.data()+576,static_cast<unsigned char *>(output->_bytes)+oo,256);
  NSData *request=[[NSData alloc]initWithBytes:wire.data() length:wire.size()];
  if(!request)return nil;
  RTXHostSubmission *submission=[[RTXHostSubmission alloc]initWithOutput:output offset:oo
    request:request completion:requestID];
  [request release];
  if(submission)output->_pendingOutput=true;
  return submission;
}
@end

@implementation RTXHostBuffer
- (instancetype)initWithArena:(RTXBufferArena *)arena length:(NSUInteger)length
    allocation:(NSUInteger)allocated memory:(void *)memory {
  self=[super init];if(!self)return nil;
  _arena=[arena retain];_retainedDevice=[arena.device retain];_bytes=memory;_length=length;_allocated=allocated;_purge=MTLPurgeableStateNonVolatile;return self;
}
- (void)dealloc {
  if(_arena){
    {std::lock_guard<std::mutex> lock(_arena->_state->mutex);
      NSCAssert(_arena->_state->charged>=_allocated&&_arena->_state->count,@"Backing accounting underflow");
      std::free(_bytes);_bytes=nullptr;_arena->_state->charged-=_allocated;--_arena->_state->count;
    }
    [_arena release];
  }
  [_label release];[_retainedDevice release];[super dealloc];
}
- (NSUInteger)length {return _length;}
- (NSUInteger)allocatedSize {return _allocated;}
- (id)device {return _arena.device;}
- (NSString *)label {
  std::lock_guard<std::mutex> lock(_arena->_state->mutex);return [[_label retain]autorelease];
}
- (void)setLabel:(NSString *)label {
  NSString *copy=[label copy],*old=nil;
  {std::lock_guard<std::mutex> lock(_arena->_state->mutex);old=_label;_label=copy;}
  [old release];
}
- (BOOL)writeBytes:(const void *)bytes range:(NSRange)range {
  if(!rangeOK(range,_length)||(!bytes&&range.length))return NO;
  std::lock_guard<std::mutex> lock(_arena->_state->mutex);
  if(_arena->_state->closed||(range.length&&_revision==std::numeric_limits<uint64_t>::max()))return NO;
  if(range.length){std::memcpy(static_cast<unsigned char *>(_bytes)+range.location,bytes,range.length);++_revision;}
  return YES;
}
- (NSData *)newUploadSnapshotForDevice:(id)device range:(NSRange)range {
  if(device!=_arena.device||!rangeOK(range,_length))return nil;
  std::lock_guard<std::mutex> lock(_arena->_state->mutex);
  if(_arena->_state->closed)return nil;
  return [[NSData alloc]initWithBytes:static_cast<const unsigned char *>(_bytes)+range.location length:range.length];
}
- (BOOL)publishReadback:(NSData *)data range:(NSRange)range {
  if(!data||data.length!=range.length)return NO;
  return [self writeBytes:data.bytes range:range];
}
- (NSData *)newCPUReadbackForRange:(NSRange)range {
  // Readback remains available after close so callers can inspect a completed
  // result. Closing prevents mutation/new snapshots; it does not erase results.
  if(!rangeOK(range,_length))return nil;
  std::lock_guard<std::mutex> lock(_arena->_state->mutex);
  return [[NSData alloc]initWithBytes:static_cast<const unsigned char *>(_bytes)+range.location length:range.length];
}
@end

@implementation RTXHostSubmission
- (instancetype)initWithOutput:(RTXHostBuffer *)output offset:(NSUInteger)offset
    request:(NSData *)request completion:(uint64_t)completion {
  self=[super init];if(!self)return nil;
  _output=[output retain];_request=[request retain];_offset=offset;
  _revision=output->_revision;_completion=completion;return self;
}
- (void)dealloc {
  [self cancel];[_request release];[_output release];[super dealloc];
}
- (id)device {return _output.device;}
- (NSData *)request {return [[_request retain]autorelease];}
- (BOOL)terminal {std::lock_guard<std::mutex> lock(_output->_arena->_state->mutex);return _terminal;}
- (void)cancel {
  if(!_output)return;
  std::lock_guard<std::mutex> lock(_output->_arena->_state->mutex);
  if(!_terminal){_terminal=true;_output->_pendingOutput=false;}
}
- (BOOL)publishTransportReadback:(NSData *)data request:(NSData *)request completion:(uint64_t)completion {
  // Copy external Objective-C data before taking the arena lock. No callback,
  // caller mutation or custom NSData implementation runs under that lock.
  if(!data||data.length!=256||!request||![request isEqualToData:_request]||completion!=_completion)return NO;
  std::array<unsigned char,256> bytes{};[data getBytes:bytes.data() length:bytes.size()];
  std::lock_guard<std::mutex> lock(_output->_arena->_state->mutex);
  if(_terminal)return NO;
  _terminal=true;_output->_pendingOutput=false;
  if(_output->_arena->_state->closed||_output->_revision!=_revision||_revision==std::numeric_limits<uint64_t>::max())return NO;
  std::memcpy(static_cast<unsigned char *>(_output->_bytes)+_offset,bytes.data(),bytes.size());++_output->_revision;
  return YES;
}
@end

#include "RTXMetalBuffer.inc"
