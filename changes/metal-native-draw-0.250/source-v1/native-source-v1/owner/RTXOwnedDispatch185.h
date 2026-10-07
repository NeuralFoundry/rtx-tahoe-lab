#pragma once
#include "RTXNativeOwner.h"
typedef struct RTXOwnedBinding185 {uint32_t slot,index;uint64_t offset,bytes;} RTXOwnedBinding185;
typedef struct RTXOwnedSize185 {uint64_t x,y,z;} RTXOwnedSize185;
#ifdef __cplusplus
extern "C" {
#endif
// Private owner API after same-connection pristine bootstrap and root/data
// verification. One compiled ABI2 metadata(512)+code(4096) payload per lifetime.
// Claiming requires no existing Metal queues, calls or broker clients.
uint32_t rtx_native_owned_begin185(const void *payload,size_t bytes);
// Slot bindings refer only to that owner's four verified mappings. No GPU VA,
// external completion claim or raw kernel selector can be supplied here.
uint32_t rtx_native_owned_submit185(uint32_t program,const RTXOwnedBinding185 *bindings,size_t count,RTXOwnedSize185 groups,RTXOwnedSize185 threads,uint64_t *completion);
#ifdef __cplusplus
}
static_assert(sizeof(RTXOwnedBinding185)==24,"Owned binding ABI");
#endif
