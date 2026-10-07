#pragma once
#include "ResidentABI.hpp"
namespace RtxResidentABI058 {
// IOKit supplies inline message buffers even when the corresponding count is
// zero. Canonicalize only those empty spans at the OS boundary; the strict
// resident ABI still checks every nonempty pointer and exact byte count.
template<class InlineArguments>inline Call fromInlineArguments(const InlineArguments &a){
 return {a.scalarInputCount?reinterpret_cast<const uint64_t*>(a.scalarInput):nullptr,a.scalarInputCount,
   a.structureInputSize?static_cast<const uint8_t*>(a.structureInput):nullptr,a.structureInputSize,
   a.structureOutputSize?static_cast<uint8_t*>(a.structureOutput):nullptr,a.structureOutputSize};
}
}
