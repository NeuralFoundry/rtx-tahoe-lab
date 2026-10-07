#import "RTXMetalCommand.h"
#ifdef __cplusplus
extern "C" {
#endif
BOOL RTXClaimNativeOwnedBroker188(id<RTXCommandTransport>*transport,NSData**container,uint64_t*generation,uint64_t*completed);
void RTXRetireNativeOwnedBroker188(void);
int rtx_owned_broker_serve188(const char*service,uint32_t allowedUID,void*claim,void*retire,const char*evidenceDirectory,uint32_t stopAfter,uint32_t timeoutSeconds);
// Requests serialized retirement and drain; serve returns only after final
// connection callbacks and send barriers. Never resets or frees GPU DMA.
int rtx_owned_broker_stop198(void);
#ifdef __cplusplus
}
#endif
