#pragma once
#include "RTXSoftwareLimits.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>

namespace RTXHeap117 {
constexpr std::size_t Page = RTXSoftware039::HostPage;
struct SizeAndAlign { std::size_t size, align; };
inline bool bufferLayout(std::size_t length, SizeAndAlign &out) noexcept {
    if (!length || length > RTXSoftware039::MaxBuffer) return false;
    out = {(length + Page - 1) & ~(Page - 1), Page};
    return true;
}
inline bool heapLayout(std::size_t requested, std::size_t &out) noexcept {
    if (!requested || requested > RTXSoftware039::MaxArena) return false;
    out = (requested + Page - 1) & ~(Page - 1);
    return true;
}

// CPU backing allocation ledger, used under the owning device arena's mutex.
// It does not assign GPU addresses. A resource retains its heap until death.
class Layout final {
public:
    struct Allocation {
        const Layout *owner = nullptr;
        std::uint64_t identity = 0;
        std::size_t offset = 0, size = 0, length = 0;
    };
private:
    struct PageState { std::uint64_t identity = 0; std::size_t length = 0; };
    std::unique_ptr<PageState[]> pages_;
    std::size_t capacity_ = 0, used_ = 0;
    std::uint64_t next_ = 1;
public:
    explicit Layout(std::size_t capacity) noexcept {
        if (!capacity || capacity > RTXSoftware039::MaxArena || capacity % Page) return;
        pages_.reset(new(std::nothrow) PageState[capacity / Page]{});
        if (pages_) capacity_ = capacity;
    }
    Layout(const Layout &) = delete;
    Layout &operator=(const Layout &) = delete;
    bool valid() const noexcept { return capacity_ != 0; }
    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t used() const noexcept { return used_; }
    bool allocate(std::size_t length, Allocation &out) noexcept {
        SizeAndAlign shape{};
        if (!valid() || !next_ || !bufferLayout(length, shape) || shape.size > capacity_ - used_) return false;
        const std::size_t wanted = shape.size / Page, count = capacity_ / Page;
        std::size_t run = 0;
        for (std::size_t p = 0; p < count; ++p) {
            run = pages_[p].identity ? 0 : run + 1;
            if (run != wanted) continue;
            const auto start = p + 1 - wanted;
            const auto identity = next_;
            next_ = identity == std::numeric_limits<std::uint64_t>::max() ? 0 : identity + 1;
            for (std::size_t q = start; q <= p; ++q) pages_[q] = {identity, length};
            used_ += shape.size;
            out = {this, identity, start * Page, shape.size, length};
            return true;
        }
        return false;
    }
    bool contains(const Allocation &a) const noexcept {
        SizeAndAlign shape{};
        if (a.owner != this || !a.identity || !bufferLayout(a.length, shape) || a.size != shape.size ||
            a.offset % Page || a.offset > capacity_ || a.size > capacity_ - a.offset) return false;
        const auto first = a.offset / Page, end = first + a.size / Page;
        // Both boundaries matter: a shortened/shifted forged lease cannot free
        // only part of another allocation with the same identity.
        if ((first && pages_[first - 1].identity == a.identity) ||
            (end < capacity_ / Page && pages_[end].identity == a.identity)) return false;
        for (auto p = first; p < end; ++p)
            if (pages_[p].identity != a.identity || pages_[p].length != a.length) return false;
        return true;
    }
    bool release(const Allocation &a) noexcept {
        if (!contains(a)) return false;
        for (auto p = a.offset / Page; p < (a.offset + a.size) / Page; ++p) pages_[p] = {};
        used_ -= a.size;
        return true;
    }
    std::size_t maxAvailable(std::size_t alignment, std::size_t backingBase = 0) const noexcept {
        if (!valid() || !alignment || (alignment & (alignment - 1))) return 0;
        std::size_t best = 0, cursor = 0, count = capacity_ / Page;
        while (cursor < count) {
            if (pages_[cursor].identity) { ++cursor; continue; }
            const auto start = cursor * Page;
            while (cursor < count && !pages_[cursor].identity) ++cursor;
            const auto end = cursor * Page;
            // Bounded subtraction avoids overflowing even at SIZE_MAX input.
            const auto padding = (alignment - ((backingBase + start) & (alignment - 1))) & (alignment - 1);
            if (padding <= end - start) best = std::max(best, end - start - padding);
        }
        return best;
    }
};
}
