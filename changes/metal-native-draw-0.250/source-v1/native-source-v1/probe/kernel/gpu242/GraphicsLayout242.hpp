#pragma once
#include <stdint.h>
namespace RTXGraphicsLayout242 {
constexpr uint64_t Begin=0x2000000000ULL,End=0x2100000000ULL;
constexpr unsigned LegacyBuffers=4,LegacyPages=1301;constexpr uint64_t LegacyBytes=5312516;
struct Buffer {uint64_t bytes;uint32_t access;};
constexpr Buffer Description[9]={{4097,3},{65537,3},{1048577,3},{4194305,3},{4096,1},{48,1},{24849,3},{16,3},{8192,1}};
#if defined(RTX_GRAPHICS242)
constexpr unsigned ABI=242,Buffers=9,Pages=1313;constexpr uint64_t Bytes=5349717;
#else
// Preserve the existing four-buffer CPU regression configuration. Native242
// driver and owner builds explicitly require RTX_GRAPHICS242.
constexpr unsigned ABI=195,Buffers=4,Pages=1301;constexpr uint64_t Bytes=5312516;
#endif
constexpr uint64_t mapped(unsigned i){return i<Buffers?(Description[i].bytes+4095)&~uint64_t(4095):0;}
constexpr uint64_t address(unsigned i){uint64_t va=Begin;for(unsigned n=0;n<i;++n)va+=mapped(n)+4096;return i<Buffers?va:0;}
constexpr unsigned graphicsSlot(unsigned role){return role<5?LegacyBuffers+role:Buffers;}
constexpr uint64_t sumBytes(){uint64_t value=0;for(unsigned n=0;n<Buffers;++n)value+=Description[n].bytes;return value;}
constexpr unsigned sumPages(){unsigned value=0;for(unsigned n=0;n<Buffers;++n)value+=unsigned(mapped(n)/4096);return value;}
static_assert(sumBytes()==Bytes&&sumPages()==Pages,"owned graphics layout totals");
}
