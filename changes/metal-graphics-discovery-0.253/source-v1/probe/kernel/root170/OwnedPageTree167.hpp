#pragma once
#include <stddef.h>
#include <stdint.h>
#include "MetalDmaBacking166.hpp"

// CPU encoder, not an allocator or an RM mapping request. The native owner must
// hold every supplied page, serialize inputs, publish all bytes through its DMA
// descriptor, and retain the tree/data until verified GPU quiescence. No client
// supplied physical addresses may reach this interface.
namespace RTXPageTree167 {
constexpr uint64_t Page = 4096, VaLimit = 1ULL << 49;
constexpr uint64_t SysLimit = RTXBacking166::AddressLimit, VidLimit = 1ULL << 37;
constexpr uint32_t MaxMappings = 65536, MaxTables = 16384;
enum class Aperture : uint32_t { Video = 0, System = 2 };
enum class Access : uint32_t { Read = 1, ReadWrite = 2, ReadWriteAtomic = 3 };
enum class Error : uint32_t { None, Shape, Mapping, Order, Capacity, Aliasing, TablePages, TableDataOverlap };
struct Mapping {
    uint64_t va, physical, owner;
    Aperture aperture;
    Access access;
    uint32_t cached, reserved;
};
static_assert(sizeof(Mapping) == 40, "mapping layout");
struct Shape { Error error = Error::None; uint32_t tables = 0; };
struct Result { Error error = Error::None; uint32_t tables = 0; uint64_t root = 0; };

inline bool validRange(const void *p, size_t bytes) {
    return p && bytes && reinterpret_cast<uintptr_t>(p) <= UINTPTR_MAX - bytes;
}
inline bool overlap(const void *a, size_t an, const void *b, size_t bn) {
    const uintptr_t x = reinterpret_cast<uintptr_t>(a), y = reinterpret_cast<uintptr_t>(b);
    return x < y + bn && y < x + an;
}
inline Shape measure(const Mapping *m, uint32_t count) {
    if (!count || count > MaxMappings || !validRange(m, size_t(count) * sizeof(*m)) ||
        reinterpret_cast<uintptr_t>(m) % alignof(Mapping))
        return {Error::Shape, 0};
    uint32_t tables = 1;
    constexpr unsigned shifts[4] = {47, 38, 29, 21};
    for (uint32_t i = 0; i < count; ++i) {
        const Mapping &p = m[i];
        const bool sys = p.aperture == Aperture::System;
        if ((!sys && p.aperture != Aperture::Video) || !p.owner || p.reserved || p.cached > 1 ||
            uint32_t(p.access) < 1 || uint32_t(p.access) > 3 ||
            p.va % Page || p.va > VaLimit - Page || p.physical < Page ||
            p.physical % Page || p.physical > (sys ? SysLimit : VidLimit) - Page)
            return {Error::Mapping, 0};
        if (i && p.va <= m[i-1].va) return {Error::Order, 0};
        for (unsigned d = 0; d < 4; ++d)
            if (!i || (p.va >> shifts[d]) != (m[i-1].va >> shifts[d])) ++tables;
    }
    if (tables > MaxTables) return {Error::Capacity, 0};
    return {Error::None, tables};
}
inline void put64(uint8_t *p, uint64_t v) { for (unsigned i = 0; i < 8; ++i) p[i] = uint8_t(v >> (8*i)); }
inline uint64_t pte(const Mapping &m) {
    // NVIDIA 570.144 uvm_turing_mmu.c; Ampere inherits this 4K PTE format.
    return (6ULL << 56) | (m.physical >> 4) | 1ULL |
        (m.aperture == Aperture::System ? 4ULL : 0ULL) | (m.cached ? 0ULL : 8ULL) |
        (m.access == Access::Read ? 64ULL : 0ULL) |
        (m.access == Access::ReadWriteAtomic ? 0ULL : 128ULL);
}
inline bool contains(const uint64_t *sorted, uint32_t n, uint64_t value) {
    uint32_t low = 0, high = n;
    while (low < high) { uint32_t mid = low + (high-low)/2; if (sorted[mid] < value) low = mid+1; else high = mid; }
    return low < n && sorted[low] == value;
}

// Rejection leaves image untouched; scratch is temporary and may change.
// DMA page order defines deterministic preorder node storage, not contiguity.
// This creates an unpublished replacement tree; it never edits an active tree.
inline Result build(const Mapping *m, uint32_t count, const uint64_t *dmaPages,
                    uint32_t pageCount, uint64_t *scratch, uint8_t *image,
                    size_t imageBytes, bool disableAts) {
    const Shape shape = measure(m, count);
    if (shape.error != Error::None) return {shape.error, 0, 0};
    if (pageCount != shape.tables || imageBytes != size_t(shape.tables)*Page)
        return {Error::Capacity, 0, 0};
    const void *ranges[4] = {m, dmaPages, scratch, image};
    const size_t lengths[4] = {size_t(count)*sizeof(*m), size_t(pageCount)*8, size_t(pageCount)*8, imageBytes};
    for (unsigned i = 0; i < 4; ++i) {
        if (!validRange(ranges[i], lengths[i])) return {Error::Shape, 0, 0};
        if ((i == 1 || i == 2) && reinterpret_cast<uintptr_t>(ranges[i]) % alignof(uint64_t))
            return {Error::Shape, 0, 0};
        for (unsigned j = 0; j < i; ++j)
            if (overlap(ranges[i], lengths[i], ranges[j], lengths[j])) return {Error::Aliasing, 0, 0};
    }
    if (!RTXBacking166::validPages(dmaPages, scratch, pageCount)) return {Error::TablePages, 0, 0};
    for (uint32_t i = 0; i < count; ++i)
        if (m[i].aperture == Aperture::System && contains(scratch, pageCount, m[i].physical))
            return {Error::TableDataOverlap, 0, 0};
    for (size_t i = 0; i < imageBytes; ++i) image[i] = 0;
    constexpr unsigned shifts[4] = {47, 38, 29, 21};
    uint32_t nodes[5] = {}, next = 1;
    for (uint32_t i = 0; i < count; ++i) {
        const uint64_t va = m[i].va;
        for (unsigned d = 0; d < 4; ++d) {
            if (i && (va >> shifts[d]) == (m[i-1].va >> shifts[d])) continue;
            const uint32_t child = next++;
            const uint32_t index = uint32_t((va >> shifts[d]) & (d == 0 ? 3ULL : d == 3 ? 255ULL : 511ULL));
            const size_t offset = size_t(nodes[d])*Page + size_t(index)*(d == 3 ? 16 : 8) + (d == 3 ? 8 : 0);
            // Sysmem coherent + volatile; NO_ATS only belongs to PD1. A dual
            // PD0's unused big half stays zero (IS_PDE_TRUE is encoded as 0).
            const uint64_t entry = (dmaPages[child] >> 4) | 12ULL | (disableAts && d == 2 ? 32ULL : 0ULL);
            put64(image + offset, entry);
            nodes[d+1] = child;
        }
        put64(image + size_t(nodes[4])*Page + size_t((va >> 12)&511)*8, pte(m[i]));
    }
    return {Error::None, next, dmaPages[0]};
}
} // namespace RTXPageTree167
