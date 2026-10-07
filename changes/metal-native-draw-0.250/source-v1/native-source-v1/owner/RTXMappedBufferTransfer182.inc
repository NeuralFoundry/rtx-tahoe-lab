#import "RTXMappedBufferTransfer182.h"
#include <vector>
@interface RTXMappedTransfer182 : NSObject {
@public
 RTXMetalBuffer036 *_buffer;NSData *_snapshot;NSRange _range;uint64_t _revision;
 BOOL _upload,_terminal;
}
- (instancetype)initTransaction;
- (void)cancel;
- (BOOL)finish:(NSData *)readback;
@end
@implementation RTXMappedTransfer182
- (instancetype)init{[self release];return nil;}
- (instancetype)initTransaction{return [super init];}
- (void)dealloc{[self cancel];[_snapshot release];[_buffer release];[super dealloc];}
- (void)cancel{
 if(!_buffer)return;std::lock_guard<std::mutex> lock(_buffer->_arena->_state->mutex);
 if(!_terminal){_terminal=YES;_buffer->_pendingOutput=false;}
}
- (BOOL)finish:(NSData *)readback{
 if(_upload&&readback)return NO;
 if(!_upload&&(![readback isKindOfClass:[NSData class]]||readback.length!=_range.length))return NO;
 std::vector<uint8_t> bytes(_upload?0:_range.length);
 if(!_upload)[readback getBytes:bytes.data() length:bytes.size()];
 std::lock_guard<std::mutex> lock(_buffer->_arena->_state->mutex);
 if(_terminal)return NO;_terminal=YES;_buffer->_pendingOutput=false;
 if(_buffer->_arena->_state->closed||_buffer->_purge==MTLPurgeableStateEmpty)return NO;
 if(_upload)return YES; // Later CPU input edits do not alter the upload snapshot.
 if(_buffer->_revision!=_revision||_revision==UINT64_MAX||
    std::memcmp(static_cast<const uint8_t*>(_buffer->_bytes)+_range.location,_snapshot.bytes,_range.length))return NO;
 std::memcpy(static_cast<uint8_t*>(_buffer->_bytes)+_range.location,bytes.data(),bytes.size());++_buffer->_revision;return YES;
}
@end
id RTXNewMappedBufferTransfer182(id<MTLDevice> device,id<MTLBuffer> object,NSRange range,BOOL upload){
 if(!device||object_getClass(object)!=[RTXMetalBuffer036 class]||!range.length)return nil;
 auto *b=(RTXMetalBuffer036*)object;
 if(b.device!=device||!rangeOK(range,b->_length))return nil;
 std::lock_guard<std::mutex> lock(b->_arena->_state->mutex);
 if(b->_arena->_state->closed||b->_pendingOutput||b->_purge==MTLPurgeableStateEmpty||(!upload&&b->_revision==UINT64_MAX))return nil;
 NSData *snapshot=[[NSData alloc]initWithBytes:static_cast<const uint8_t*>(b->_bytes)+range.location length:range.length];
 if(!snapshot)return nil;
 RTXMappedTransfer182 *t=[[RTXMappedTransfer182 alloc]initTransaction];if(!t){[snapshot release];return nil;}
 t->_buffer=[b retain];t->_snapshot=snapshot;t->_range=range;t->_revision=b->_revision;t->_upload=upload;b->_pendingOutput=true;return t;
}
NSData *RTXCopyMappedUpload182(id object){
 if(object_getClass(object)!=[RTXMappedTransfer182 class])return nil;auto*t=(RTXMappedTransfer182*)object;
 std::lock_guard<std::mutex> lock(t->_buffer->_arena->_state->mutex);
 return !t->_terminal&&t->_upload?[t->_snapshot retain]:nil;
}
BOOL RTXFinishMappedBufferTransfer182(id object,NSData *readback){return object_getClass(object)==[RTXMappedTransfer182 class]&&[(RTXMappedTransfer182*)object finish:readback];}
void RTXCancelMappedBufferTransfer182(id object){if(object_getClass(object)==[RTXMappedTransfer182 class])[(RTXMappedTransfer182*)object cancel];}
