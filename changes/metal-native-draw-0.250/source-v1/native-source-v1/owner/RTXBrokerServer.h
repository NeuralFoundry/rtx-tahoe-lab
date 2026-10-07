#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Embed in the same root process that has completed native bootstrap. claim
// and nativeInfo are resolved from that process's reviewed owner bundle.
int rtx_broker_serve(const char *service,uint32_t allowedUID,void *claim,void *nativeInfo,
                     const char *evidenceDirectory,uint32_t stopAfter,uint32_t timeoutSeconds);
#ifdef __cplusplus
}
#endif
