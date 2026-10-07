#pragma once
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "RTXAcceleratorIdentity103.hpp"
// Read the bounded root readiness record before opening the application channel.
extern "C" NSDictionary *RTXPrepareApplicationRuntime115(id device,const RTXAccelerator103::Binding &binding,NSError **error);
extern "C" BOOL RTXActivateApplicationRuntime115(id<MTLDevice> device,NSDictionary *plan,uint32_t port,NSError **error);
extern "C" NSDictionary *RTXCopyApplicationRuntimeInfo115(id<MTLDevice> device);
// Pure decoder and bounded file reader also exercised by the native CPU tests.
extern "C" NSDictionary *RTXDecodeApplicationRuntime115(NSData *raw,NSDictionary *spec,const RTXAccelerator103::Binding &binding,uint64_t pid,NSError **error);
extern "C" NSData *RTXReadApplicationRuntimeFile115(NSString *path,NSUInteger maximum,NSError **error);
