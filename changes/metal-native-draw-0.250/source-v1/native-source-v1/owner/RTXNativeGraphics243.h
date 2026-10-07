#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Private bring-up API. Call after same-connection bootstrap and rtx_native_arm.
// Exclusively claims an idle command device, then validates root242/graphics241.
uint32_t rtx_native_graphics_begin243(void);
uint32_t rtx_native_graphics_draw250(const void*request,size_t requestBytes,void*result,size_t resultBytes,uint64_t*completion);
// Fixed 64x64 RGBA8 triangle, 384-byte pitch, 256-byte prefix, 17-byte tail.
// Exactly 24849 bytes. Image is unchanged and completion zero on failure.
// Success proves native completion and guards; independently check the pixels.
uint32_t rtx_native_graphics_draw243(void *image,size_t bytes,uint64_t *completion);
#ifdef __cplusplus
}
#endif
