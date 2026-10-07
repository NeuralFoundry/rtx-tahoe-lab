#import "RTXMetalCommand.h"
#ifdef __cplusplus
extern "C" {
#endif
// Root-only, same-process, single ownership transfer after verified bootstrap.
// Returns retained objects. It does not open IOKit or start firmware. The old
// in-process context must have no queues or admitted calls and is atomically
// revoked before broker ownership begins; an asynchronous close is insufficient.
BOOL RTXClaimNativeBroker(id<RTXCommandTransport> *transport,NSData **container,
                          uint64_t *generation,uint64_t *completed);
#ifdef __cplusplus
}
#endif
