#pragma once
#include <cstdint>
// PTX 9.7.3 leaves FP32 NaN payloads unspecified; MSL 8.1 excludes sNaN.
// Apply only to arithmetic output. Input buffers and guards stay byte exact.
namespace RTXFloatResult110 {
inline bool quietNaN(uint32_t bits) {
    return (bits & 0x7fc00000u) == 0x7fc00000u;
}
inline bool matches(uint32_t actual, uint32_t expected, bool arithmeticOutput) {
    if (!arithmeticOutput) return actual == expected;
    const bool expectedNaN = (expected & 0x7f800000u) == 0x7f800000u && (expected & 0x007fffffu);
    if (expectedNaN) return quietNaN(expected) && quietNaN(actual);
    return actual == expected;
}
}
