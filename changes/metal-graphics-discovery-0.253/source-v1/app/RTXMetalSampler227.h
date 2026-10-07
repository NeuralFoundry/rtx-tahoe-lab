#import <Metal/Metal.h>
#include <stdint.h>
id<MTLSamplerState> RTXNewSamplerState227(id<MTLDevice> device,MTLSamplerDescriptor*descriptor);
BOOL RTXCopySamplerBinding227(id<MTLSamplerState> sampler,id<MTLDevice> device,uint8_t descriptor[32]);
