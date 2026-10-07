#pragma once
#import "RTXOwnedBrokerTransport208.h"
NS_ASSUME_NONNULL_BEGIN
id<RTXOwnedChannel208> _Nullable RTXNewGraphicsXPC251(NSString*service,uint32_t timeoutMilliseconds) NS_RETURNS_RETAINED;
id<RTXGraphicsCommandTransport248> _Nullable RTXNewOwnedGraphicsTransport251(id<RTXOwnedChannel208>channel,NSData*container,uint64_t generation,NSError* _Nullable * _Nullable error) NS_RETURNS_RETAINED;
void RTXCloseOwnedGraphicsTransport251(id<RTXGraphicsCommandTransport248>transport);
NSDictionary* _Nullable RTXCopyOwnedGraphicsTransportInfo251(id<RTXGraphicsCommandTransport248>transport) NS_RETURNS_RETAINED;
extern "C" id<MTLDevice> _Nullable RTXCreateGraphicsApplication251(id<RTXOwnedChannel208>channel,NSData*catalog,uint64_t generation,id<MTLLibrary> _Nullable * _Nonnull library,NSError* _Nullable * _Nullable error) NS_RETURNS_RETAINED;
extern "C" id<MTLDevice> _Nullable RTXCreateGraphicsXPCApplication251(NSString*service,NSData*catalog,uint64_t generation,uint32_t timeout,id<MTLLibrary> _Nullable * _Nonnull library,NSError* _Nullable * _Nullable error) NS_RETURNS_RETAINED;
extern "C" id<MTLDevice> _Nullable RTXConsumeInitializedGraphicsDevice253(id<MTLDevice>device,NSString*service,NSData*catalog,uint64_t generation,id<MTLLibrary> _Nullable * _Nonnull library,NSError* _Nullable * _Nullable error) NS_RETURNS_RETAINED;
NS_ASSUME_NONNULL_END
