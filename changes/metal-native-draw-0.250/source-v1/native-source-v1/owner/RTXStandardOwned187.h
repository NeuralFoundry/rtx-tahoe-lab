#pragma once
#include "RTXNativeOwner.h"
#ifdef __cplusplus
extern "C" {
#endif
// After the existing native_open/bootstrap with its reviewed legacy payload,
// arm this same connection with the new ABI2 library. Standard Metal queues
// then use owned allocations through the ordinary commit interface.
uint32_t rtx_native_arm_owned187(const char*directory,const void*container,size_t bytes);
#ifdef __cplusplus
}
#endif
