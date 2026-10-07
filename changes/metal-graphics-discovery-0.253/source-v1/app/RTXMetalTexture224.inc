// Included by RTXHostBuffer.mm after the arena/buffer definitions so views use
// the same lock, revision, pending-output and heap invalidation as MTLBuffer.
#include "RTXTextureStorage224.hpp"
#include "RTXTextureBindings225.hpp"
#import "RTXMetalTexture224.h"
static_assert(RTXTexture224::MaxStorage==MaxBuffer,"Texture backing must fit the buffer arena contract");

static RTXTexture224::Format textureFormat224(MTLPixelFormat f){
 using F=RTXTexture224::Format;
 switch(f){
 case MTLPixelFormatR32Uint:return F::R32Uint;
 case MTLPixelFormatR32Sint:return F::R32Sint;
 case MTLPixelFormatR32Float:return F::R32Float;
 case MTLPixelFormatRGBA8Unorm:return F::RGBA8Unorm;
 case MTLPixelFormatRGBA8Uint:return F::RGBA8Uint;
 case MTLPixelFormatRGBA8Sint:return F::RGBA8Sint;
 case MTLPixelFormatBGRA8Unorm:return F::BGRA8Unorm;
 case MTLPixelFormatRGBA32Uint:return F::RGBA32Uint;
 case MTLPixelFormatRGBA32Sint:return F::RGBA32Sint;
 case MTLPixelFormatRGBA32Float:return F::RGBA32Float;
 default:return F::Invalid;
 }
}
NSUInteger RTXLinearTextureAlignment224(MTLPixelFormat format){
 return RTXTexture224::pixelBytes(textureFormat224(format))?RTXTexture224::Alignment:NSUIntegerMax;
}
static bool textureDescriptor224(MTLTextureDescriptor*d){
 if(!d||d.textureType!=MTLTextureType2D||d.depth!=1||d.arrayLength!=1||d.mipmapLevelCount!=1||d.sampleCount!=1||
    d.storageMode!=MTLStorageModeShared||d.cpuCacheMode!=MTLCPUCacheModeDefaultCache||
    (d.hazardTrackingMode!=MTLHazardTrackingModeDefault&&d.hazardTrackingMode!=MTLHazardTrackingModeTracked)||
    (d.usage&~(MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite|MTLTextureUsageRenderTarget)))return false;
 const auto swizzle=d.swizzle;
 return swizzle.red==MTLTextureSwizzleRed&&swizzle.green==MTLTextureSwizzleGreen&&
   swizzle.blue==MTLTextureSwizzleBlue&&swizzle.alpha==MTLTextureSwizzleAlpha;
}
@interface RTXApplication209_RTXLinearTexture224:NSObject<MTLTexture>{
@public
 RTXApplication209_RTXMetalBuffer036*_backing;
 MTLTextureDescriptor*_descriptor;
 RTXTexture224::Layout _layout;
 NSString*_textureLabel;
 bool _fromBuffer;
}
- (instancetype)initWithBacking:(RTXApplication209_RTXMetalBuffer036*)backing descriptor:(MTLTextureDescriptor*)descriptor layout:(const RTXTexture224::Layout&)layout fromBuffer:(bool)fromBuffer;
- (void)transferRegion:(MTLRegion)region level:(NSUInteger)level slice:(NSUInteger)slice bytes:(void*)bytes row:(NSUInteger)row image:(NSUInteger)image upload:(bool)upload;
@end
static id<MTLTexture> textureFromBuffer224(RTXApplication209_RTXMetalBuffer036*b,MTLTextureDescriptor*d,NSUInteger offset,NSUInteger row,bool fromBuffer){
 if([b class]!=[RTXApplication209_RTXMetalBuffer036 class]||!textureDescriptor224(d))return nil;
 RTXTexture224::Layout layout;
 if(!RTXTexture224::make(textureFormat224(d.pixelFormat),d.width,d.height,offset,row,b->_length,layout))return nil;
 std::lock_guard<std::mutex> lock(b->_arena->_state->mutex);
 if(b->_arena->_state->closed||b->_pendingOutput||!bufferAvailable(b))return nil;
 return [[RTXApplication209_RTXLinearTexture224 alloc]initWithBacking:b descriptor:d layout:layout fromBuffer:fromBuffer];
}
id<MTLTexture> RTXNewSharedTexture224(id<MTLDevice>device,MTLTextureDescriptor*descriptor){
 if(!device||!textureDescriptor224(descriptor))return nil;
 RTXTexture224::Layout layout;
 if(!RTXTexture224::allocate(textureFormat224(descriptor.pixelFormat),descriptor.width,descriptor.height,layout))return nil;
 id<MTLBuffer>backing=[device newBufferWithLength:layout.storageBytes options:MTLResourceStorageModeShared];
 if(!backing)return nil;
 id<MTLTexture>texture=textureFromBuffer224((RTXApplication209_RTXMetalBuffer036*)backing,descriptor,0,layout.rowPitch,false);
 [backing release];return texture;
}
static BOOL textureBinding231(id<MTLTexture>texture,id<MTLDevice>device,NSUInteger access,uint32_t formatContract,id<MTLBuffer>*backing,NSUInteger*offset,uint8_t descriptor[32],bool renderTarget){
 if(!backing||!offset||!descriptor||access>3||[texture class]!=[RTXApplication209_RTXLinearTexture224 class]||texture.device!=device)return NO;
 auto*t=(RTXApplication209_RTXLinearTexture224*)texture;auto*b=t->_backing;
 std::lock_guard<std::mutex>lock(b->_arena->_state->mutex);
 if(b->_arena->_state->closed||b->_pendingOutput||!bufferAvailable(b))return NO;
 const auto usage=t->_descriptor.usage;
 if(renderTarget){if(access!=2||(usage!=MTLTextureUsageUnknown&&!(usage&MTLTextureUsageRenderTarget)))return NO;}
 else if(usage!=MTLTextureUsageUnknown&&(((access&1)&&!(usage&MTLTextureUsageShaderRead))||((access&2)&&!(usage&MTLTextureUsageShaderWrite))))return NO;
 RTXTexture225::Descriptor row;if(!RTXTexture225::descriptor(t->_layout,b->_length,row,formatContract))return NO;
 std::memcpy(descriptor,row.data(),row.size());*offset=t->_layout.offset;*backing=[b retain];return YES;
}
BOOL RTXCopyTextureBinding225(id<MTLTexture>texture,id<MTLDevice>device,NSUInteger access,uint32_t formatContract,id<MTLBuffer>*backing,NSUInteger*offset,uint8_t descriptor[32]){return textureBinding231(texture,device,access,formatContract,backing,offset,descriptor,false);}
BOOL RTXCopyRenderTargetBinding231(id<MTLTexture>texture,id<MTLDevice>device,id<MTLBuffer>*backing,NSUInteger*offset,uint8_t descriptor[32]){return textureBinding231(texture,device,2,RTXTexture225::FloatFormats226,backing,offset,descriptor,true);}
@implementation RTXApplication209_RTXLinearTexture224
- (instancetype)init{[self release];return nil;}
- (instancetype)initWithBacking:(RTXApplication209_RTXMetalBuffer036*)backing descriptor:(MTLTextureDescriptor*)descriptor layout:(const RTXTexture224::Layout&)layout fromBuffer:(bool)fromBuffer{
 if((self=[super init])){_descriptor=[descriptor copy];if(!_descriptor){[self release];return nil;}_backing=[backing retain];_layout=layout;_fromBuffer=fromBuffer;}return self;
}
- (void)dealloc{[_backing release];[_descriptor release];[_textureLabel release];[super dealloc];}
- (id<MTLDevice>)device{return _backing.device;}
- (NSString*)label{std::lock_guard<std::mutex>lock(_backing->_arena->_state->mutex);return[[_textureLabel retain]autorelease];}
- (void)setLabel:(NSString*)label{NSString*copy=[label copy],*old=nil;{std::lock_guard<std::mutex>lock(_backing->_arena->_state->mutex);old=_textureLabel;_textureLabel=copy;}[old release];}
- (MTLCPUCacheMode)cpuCacheMode{return _backing.cpuCacheMode;}
- (MTLStorageMode)storageMode{return _backing.storageMode;}
- (MTLHazardTrackingMode)hazardTrackingMode{return _backing.hazardTrackingMode;}
- (MTLResourceOptions)resourceOptions{return _backing.resourceOptions;}
- (NSUInteger)allocatedSize{return _backing.allocatedSize;}
- (MTLPurgeableState)setPurgeableState:(MTLPurgeableState)state{return[_backing setPurgeableState:state];}
- (id<MTLHeap>)heap{return _backing.heap;}
- (NSUInteger)heapOffset{return _backing.heapOffset+_layout.offset;}
- (void)makeAliasable{[_backing makeAliasable];}
- (BOOL)isAliasable{return _backing.isAliasable;}
- (kern_return_t)setOwnerWithIdentity:(task_id_token_t)token{return[_backing setOwnerWithIdentity:token];}
- (id<MTLResource>)rootResource{return _fromBuffer?(id<MTLResource>)_backing:nil;}
- (id<MTLBuffer>)buffer{return _fromBuffer?_backing:nil;}
- (NSUInteger)bufferOffset{return _fromBuffer?_layout.offset:0;}
- (NSUInteger)bufferBytesPerRow{return _fromBuffer?_layout.rowPitch:0;}
- (id<MTLTexture>)parentTexture{return nil;}
- (NSUInteger)parentRelativeLevel{return 0;}
- (NSUInteger)parentRelativeSlice{return 0;}
- (IOSurfaceRef)iosurface{return nullptr;}
- (NSUInteger)iosurfacePlane{return 0;}
- (MTLTextureType)textureType{return MTLTextureType2D;}
- (MTLPixelFormat)pixelFormat{return _descriptor.pixelFormat;}
- (NSUInteger)width{return _layout.width;}
- (NSUInteger)height{return _layout.height;}
- (NSUInteger)depth{return 1;}
- (NSUInteger)mipmapLevelCount{return 1;}
- (NSUInteger)arrayLength{return 1;}
- (NSUInteger)sampleCount{return 1;}
- (BOOL)isFramebufferOnly{return NO;}
- (MTLTextureUsage)usage{return _descriptor.usage;}
- (BOOL)allowGPUOptimizedContents{return _descriptor.allowGPUOptimizedContents;}
- (BOOL)isShareable{return NO;}
- (MTLTextureSwizzleChannels)swizzle{return _descriptor.swizzle;}
- (BOOL)isSparse{return NO;}
- (MTLTextureSparseTier)sparseTextureTier{return MTLTextureSparseTierNone;}
- (NSUInteger)firstMipmapInTail{return 0;}
- (NSUInteger)tailSizeInBytes{return 0;}
- (MTLResourceID)gpuResourceID{return {0};}
- (MTLTextureCompressionType)compressionType{return MTLTextureCompressionTypeLossless;}
- (float)minLOD{return 0.0f;}
- (MTLSharedTextureHandle*)newSharedTextureHandle{return nil;}
- (id<MTLTexture>)remoteStorageTexture{return nil;}
- (id<MTLTexture>)newRemoteTextureViewForDevice:(id<MTLDevice>)device{(void)device;return nil;}
- (id<MTLTexture>)newTextureViewWithPixelFormat:(MTLPixelFormat)format{(void)format;return nil;}
- (id<MTLTexture>)newTextureViewWithDescriptor:(MTLTextureViewDescriptor*)descriptor{(void)descriptor;return nil;}
- (id<MTLTexture>)newTextureViewWithPixelFormat:(MTLPixelFormat)format textureType:(MTLTextureType)type levels:(NSRange)levels slices:(NSRange)slices{(void)format;(void)type;(void)levels;(void)slices;return nil;}
- (id<MTLTexture>)newTextureViewWithPixelFormat:(MTLPixelFormat)format textureType:(MTLTextureType)type levels:(NSRange)levels slices:(NSRange)slices swizzle:(MTLTextureSwizzleChannels)swizzle{(void)format;(void)type;(void)levels;(void)slices;(void)swizzle;return nil;}
- (void)transferRegion:(MTLRegion)region level:(NSUInteger)level slice:(NSUInteger)slice bytes:(void*)bytes row:(NSUInteger)row image:(NSUInteger)image upload:(bool)upload{
 RTXTexture224::Transfer transfer;
 if(level||slice||region.origin.z||region.size.depth!=1||
    !RTXTexture224::plan(_layout,{region.origin.x,region.origin.y,region.size.width,region.size.height},row,transfer)||
    (image&&image<transfer.hostExtent)||!RTXTexture224::pointerExtent(bytes,transfer.hostExtent))
   [NSException raise:NSInvalidArgumentException format:@"Invalid linear texture transfer"];
 if(!transfer.rows)return;
 std::lock_guard<std::mutex>lock(_backing->_arena->_state->mutex);
 const auto hostAddress=reinterpret_cast<std::uintptr_t>(bytes);
 const auto imageAddress=reinterpret_cast<std::uintptr_t>(_backing->_bytes);
 const bool aliases=hostAddress>=imageAddress?hostAddress-imageAddress<_backing->_length:imageAddress-hostAddress<transfer.hostExtent;
 const bool mutates=upload||aliases;
 if(!bufferAvailable(_backing)||_backing->_pendingOutput||
    (mutates&&(_backing->_arena->_state->closed||_backing->_revision==std::numeric_limits<uint64_t>::max())))
   [NSException raise:NSInvalidArgumentException format:@"Texture storage is unavailable or GPU output is pending"];
 try{
   if(!RTXTexture224::copy(transfer,upload,_backing->_bytes,_backing->_length,bytes,transfer.hostExtent))
     [NSException raise:NSInvalidArgumentException format:@"Texture transfer exceeds backing storage"];
 }catch(const std::bad_alloc&){[NSException raise:NSMallocException format:@"Unable to snapshot texture transfer"];
 }
 if(mutates)++_backing->_revision;
}
- (void)replaceRegion:(MTLRegion)region mipmapLevel:(NSUInteger)level withBytes:(const void*)bytes bytesPerRow:(NSUInteger)row{
 [self transferRegion:region level:level slice:0 bytes:const_cast<void*>(bytes) row:row image:0 upload:true];
}
- (void)replaceRegion:(MTLRegion)region mipmapLevel:(NSUInteger)level slice:(NSUInteger)slice withBytes:(const void*)bytes bytesPerRow:(NSUInteger)row bytesPerImage:(NSUInteger)image{
 [self transferRegion:region level:level slice:slice bytes:const_cast<void*>(bytes) row:row image:image upload:true];
}
- (void)getBytes:(void*)bytes bytesPerRow:(NSUInteger)row fromRegion:(MTLRegion)region mipmapLevel:(NSUInteger)level{
 [self transferRegion:region level:level slice:0 bytes:bytes row:row image:0 upload:false];
}
- (void)getBytes:(void*)bytes bytesPerRow:(NSUInteger)row bytesPerImage:(NSUInteger)image fromRegion:(MTLRegion)region mipmapLevel:(NSUInteger)level slice:(NSUInteger)slice{
 [self transferRegion:region level:level slice:slice bytes:bytes row:row image:image upload:false];
}
@end
