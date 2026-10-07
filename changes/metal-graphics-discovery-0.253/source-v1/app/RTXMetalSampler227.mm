#import "RTXMetalSampler227.h"
#include "RTXSamplerState227.hpp"
#include <cmath>
#include <cstring>
@interface RTXApplication209_RTXSampler227:NSObject<MTLSamplerState>{
@public id<MTLDevice> _device;NSString*_label;RTXSampler227::Descriptor _record;
}
- (instancetype)initWithDevice:(id<MTLDevice>)device label:(NSString*)label record:(const RTXSampler227::Descriptor&)record;
@end
@implementation RTXApplication209_RTXSampler227
- (instancetype)init{[self release];return nil;}
- (instancetype)initWithDevice:(id<MTLDevice>)device label:(NSString*)label record:(const RTXSampler227::Descriptor&)record{
 if((self=[super init])){_device=[device retain];_label=[label copy];_record=record;}return self;
}
- (void)dealloc{[_device release];[_label release];[super dealloc];}
- (id<MTLDevice>)device{return _device;}
- (NSString*)label{return _label;}
// Argument-buffer samplers are not supported by this descriptor-based path.
- (MTLResourceID)gpuResourceID{return {0};}
@end
static uint32_t address227(MTLSamplerAddressMode value){
 switch(value){case MTLSamplerAddressModeClampToEdge:return 0;case MTLSamplerAddressModeRepeat:return 1;
 case MTLSamplerAddressModeMirrorRepeat:return 2;case MTLSamplerAddressModeClampToZero:return 3;default:return UINT32_MAX;}
}
id<MTLSamplerState> RTXNewSamplerState227(id<MTLDevice>device,MTLSamplerDescriptor*d){
 if(!device||![d isKindOfClass:[MTLSamplerDescriptor class]]||d.minFilter!=d.magFilter||
    (d.minFilter!=MTLSamplerMinMagFilterNearest&&d.minFilter!=MTLSamplerMinMagFilterLinear)||
    d.mipFilter!=MTLSamplerMipFilterNotMipmapped||d.maxAnisotropy!=1||d.compareFunction!=MTLCompareFunctionNever||
    d.supportArgumentBuffers||d.lodMinClamp!=0||!std::isfinite(d.lodMaxClamp)||d.lodMaxClamp<0)return nil;
 RTXSampler227::State state{d.normalizedCoordinates?1u:0u,address227(d.sAddressMode),address227(d.tAddressMode),d.minFilter==MTLSamplerMinMagFilterLinear?1u:0u};
 RTXSampler227::Descriptor record;if(!RTXSampler227::encode(state,record))return nil;
 return [[RTXApplication209_RTXSampler227 alloc]initWithDevice:device label:d.label record:record];
}
BOOL RTXCopySamplerBinding227(id<MTLSamplerState>sampler,id<MTLDevice>device,uint8_t descriptor[32]){
 if(!descriptor||[sampler class]!=[RTXApplication209_RTXSampler227 class]||sampler.device!=device)return NO;
 auto*s=(RTXApplication209_RTXSampler227*)sampler;RTXSampler227::State state;
 if(!RTXSampler227::decode(s->_record.data(),state))return NO;std::memcpy(descriptor,s->_record.data(),32);return YES;
}
