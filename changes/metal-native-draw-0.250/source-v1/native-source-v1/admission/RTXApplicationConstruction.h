#pragma once
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
// Use only from the static bundle class's synchronized +initialize before any
// factory allocates a device. This never opens IOKit or starts a broker.
BOOL RTXUseApplicationDeviceClass(Class cls);
Class RTXApplicationDeviceClass(void);
extern "C" Class RTXApplicationBundleClass(void);
extern "C" uint64_t RTXApplicationBundleEntryCount(void);
// Stable entry into the existing production factory, with no second transport.
extern "C" id<MTLDevice> RTXApplicationBundleCreate(NSString *service,NSData *container,
 uint64_t generation,uint32_t timeout,id<MTLLibrary> *library,NSError **error);
