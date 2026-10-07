#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// Embed in the same root process that has completed native bootstrap. claim
// and nativeInfo are resolved from that process's reviewed owner bundle.
// Returns after listener/peers are cancelled and backend retains are released.
// Caller then gathers final native evidence and closes its owner. This does
// not establish GPU quiescence or authorize freeing retained kernel DMA.
// The root callback must independently verify compiler artifact provenance for
// this exact copied payload. Zero means admitted; it performs no GPU work.
typedef uint32_t (*RTXResidentAdmission059)(void *context,const void *payload,size_t bytes);
int rtx_resident_broker_serve059(const char *service,uint32_t allowedUID,void *claim,void *nativeInfo,
    RTXResidentAdmission059 admission,void *admissionContext,const char *evidenceDirectory,uint32_t stopAfter,uint32_t timeoutSeconds);
#ifdef __cplusplus
}
#endif
