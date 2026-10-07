#pragma once
#include <cstddef>
namespace RTXSoftware039 {
// Bounds of the current software path, not the physical GPU's full capability.
constexpr std::size_t HostPage=4096;
constexpr std::size_t MaxBuffer=16*1024*1024;
constexpr std::size_t MaxArena=64*1024*1024;
constexpr std::size_t MaxDispatchThreads=64;
}
