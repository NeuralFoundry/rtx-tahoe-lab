#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
NS_ASSUME_NONNULL_BEGIN
// Private commit-time transfer transaction over the existing Metal buffer.
// Retains its CPU backing and snapshots uploads. Readback publication rejects
// intervening CPU writes/close/purge. Success here never proves a GPU dispatch.
id _Nullable RTXNewMappedBufferTransfer182(id<MTLDevice> device,id<MTLBuffer> buffer,NSRange range,BOOL upload) NS_RETURNS_RETAINED;
NSData * _Nullable RTXCopyMappedUpload182(id transaction) NS_RETURNS_RETAINED;
BOOL RTXFinishMappedBufferTransfer182(id transaction,NSData * _Nullable readback);
void RTXCancelMappedBufferTransfer182(id transaction);
// Private native-owner entry: same root-owned connection, validated181 handle
// slot and bounded byte offsets. No GPU/physical address parameter or new open.
extern "C" uint32_t rtx_native_mapped_transfer182(id<MTLBuffer> buffer,uint32_t slot,
    uint64_t resourceOffset,uint64_t nativeOffset,uint64_t bytes,BOOL upload);
NS_ASSUME_NONNULL_END
