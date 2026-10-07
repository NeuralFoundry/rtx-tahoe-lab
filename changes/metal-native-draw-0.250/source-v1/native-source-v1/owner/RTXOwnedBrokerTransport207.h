#import "RTXMetalCommand.h"
NS_ASSUME_NONNULL_BEGIN
@protocol RTXOwnedChannel207<NSObject>
- (BOOL)exchange:(NSData*)request reply:(NSData* _Nullable * _Nonnull)reply serverUID:(uint32_t*)uid serverPID:(int64_t*)pid;
- (void)cancel;
@end
id<RTXOwnedChannel207> _Nullable RTXNewOwnedXPC207(NSString*service,uint32_t timeoutMilliseconds) NS_RETURNS_RETAINED;
// Called before configuring the ordinary application command device. Hello
// pins its root server identity, catalog, generation and native serial floor.
id<RTXCommandTransport> _Nullable RTXNewOwnedTransport207(id<RTXOwnedChannel207>channel,NSData*container,uint64_t generation,NSError* _Nullable * _Nullable error) NS_RETURNS_RETAINED;
void RTXCloseOwnedTransport207(id<RTXCommandTransport>transport);
NSDictionary* _Nullable RTXCopyOwnedTransportInfo207(id<RTXCommandTransport>transport) NS_RETURNS_RETAINED;
NS_ASSUME_NONNULL_END
