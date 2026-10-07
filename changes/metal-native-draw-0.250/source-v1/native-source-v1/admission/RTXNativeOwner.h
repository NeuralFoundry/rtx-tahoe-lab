#pragma once
#include <stdint.h>
#include <stddef.h>
#define RTX_NATIVE_MAGIC UINT64_C(0x5254584d434f3336)
#define RTX_NATIVE_ARGUMENT UINT32_C(0xe3600001)
#define RTX_NATIVE_NOT_OPEN UINT32_C(0xe3600002)
#define RTX_NATIVE_PROCESS UINT32_C(0xe3600003)
#define RTX_NATIVE_STATE UINT32_C(0xe3600004)
#define RTX_NATIVE_EVIDENCE UINT32_C(0xe3600005)
#define RTX_NATIVE_IO_SHAPE UINT32_C(0xe3600006)
#define RTX_NATIVE_EXCEPTION UINT32_C(0xe3600007)
enum RTXNativeState {RTXNativeCold=0,RTXNativeOpen=1,RTXNativeArmed=2,RTXNativeClosed=3,RTXNativeFailed=4};
typedef struct RTXNativeOwnerInfo {
 uint64_t magic,abi,bytes,process_id,state,open_attempts,io_opens,io_closes;
 uint64_t service_releases,states_destroyed,registry_id,calls,last_selector,arm_attempted,armed,completed;
 uint64_t session_failure,session_calls,session_elapsed,open_result,close_result,reserved[3];
} RTXNativeOwnerInfo;
#ifdef __cplusplus
extern "C" {
#endif
uint32_t rtx_native_info(RTXNativeOwnerInfo *out,size_t bytes);
// One process-bound attempt. The caller has already reviewed compiler provenance
// and this exact backend container. No public system Metal registration occurs.
uint32_t rtx_native_open(const void *container,size_t bytes);
// Bootstrap and diagnostic selectors only. Direct program submit 69 is denied;
// actual compute requests originate exclusively from the Metal command worker.
// Upload 73..75 must match this device's immutable Metal library payload.
uint32_t rtx_native_call(uint32_t selector,const uint64_t *scalars,uint32_t scalar_count,
 const void *input,size_t input_bytes,void *output,size_t capacity,size_t *output_bytes);
// After same-connection bootstrap: read and verify the native captures, then
// configure the standard command device. directory must be new and absolute.
uint32_t rtx_native_arm(const char *directory);
uint32_t rtx_native_close(void);
#ifdef __cplusplus
}
static_assert(sizeof(RTXNativeOwnerInfo)==192,"Native command owner ABI");
#endif
