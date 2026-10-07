#pragma once

// Host-only 570.144 preparation for the pinned GA106 6 GiB board profile.
// Mapping and publishing CPU bytes is not evidence of a GPU DMA transfer.
// This module has no MMIO, PCI-command, firmware-start or allocation API.
namespace GSPDmaProtocol {
using U32 = unsigned;
using U64 = unsigned long long;
constexpr U32 ResourceCount = 9, Page = 4096, TotalPages = 16212;
constexpr U64 TotalBytes = 66404352ULL, AddressLimit = 1ULL << 40;
enum Resource : U32 { Radix3, Bootloader, Signature, Metadata, Queues, Rmargs, LibosArgs, Logs, BooterLoad };
constexpr U32 Pages[ResourceCount] = {15546, 6, 1, 1, 129, 1, 1, 512, 15};
constexpr U64 Sizes[ResourceCount] = {63676416ULL, 24576, 4096, 4096, 528384, 4096, 4096, 2097152, 61440};
constexpr const char *Names[ResourceCount] = {
  "radix3", "bootloader", "signature", "metadata", "queues", "rmargs", "libos_args", "logs", "booter_load"
};

enum class State : U32 { Idle = 0, Writing = 1, Published = 2, Finished = 3, Aborted = 4, Failed = 5 };
enum class Error : U32 {
  Ok = 0, None = Ok, InvalidState, InvalidResource, InvalidOffset, InvalidLength,
  IncompleteWrites, IncompleteReads, InvalidPageCount, InvalidAddress,
  DuplicatePage, Noncontiguous, InvalidScratch, TransportFailed, CleanupFailed
};
struct Result {
  Error error;
  bool cleanupRequired;
  bool ok() const { return error == Error::Ok; }
};

inline const char *errorName(Error error) {
  switch (error) {
    case Error::Ok: return "ok";
    case Error::InvalidState: return "invalid-state";
    case Error::InvalidResource: return "invalid-resource";
    case Error::InvalidOffset: return "invalid-offset";
    case Error::InvalidLength: return "invalid-length";
    case Error::IncompleteWrites: return "incomplete-writes";
    case Error::IncompleteReads: return "incomplete-reads";
    case Error::InvalidPageCount: return "invalid-page-count";
    case Error::InvalidAddress: return "invalid-address";
    case Error::DuplicatePage: return "duplicate-page";
    case Error::Noncontiguous: return "noncontiguous-direct-resource";
    case Error::InvalidScratch: return "invalid-scratch";
    case Error::TransportFailed: return "transport-failed";
    case Error::CleanupFailed: return "cleanup-failed-resources-held";
  }
  return "unknown-error";
}

namespace Detail {
inline void sift(U64 *values, U32 root, U32 count) {
  while (root * 2 + 1 < count) {
    U32 child = root * 2 + 1;
    if (child + 1 < count && values[child] < values[child + 1]) ++child;
    if (values[root] >= values[child]) break;
    const U64 temporary = values[root]; values[root] = values[child]; values[child] = temporary;
    root = child;
  }
}
inline void sort(U64 *values, U32 count) {
  for (U32 parent = count / 2; parent; --parent) sift(values, parent - 1, count);
  for (U32 remaining = count; remaining > 1; --remaining) {
    const U64 temporary = values[0]; values[0] = values[remaining - 1]; values[remaining - 1] = temporary;
    sift(values, 0, remaining - 1);
  }
}
}

// Caller owns two separate arrays. The source remains in resource/page order;
// scratch is disposable. O(n log n), bounded to exactly 16,212 page addresses.
inline Error validatePages(const U64 *orderedPages, U32 count, U64 *sortScratch, U32 scratchCount) {
  if (!orderedPages || count != TotalPages) return Error::InvalidPageCount;
  if (!sortScratch || scratchCount < TotalPages) return Error::InvalidScratch;
  const U64 source = reinterpret_cast<U64>(orderedPages), target = reinterpret_cast<U64>(sortScratch);
  const U64 scratchBytes = U64(TotalPages) * sizeof(U64);
  if ((source <= target ? target - source : source - target) < scratchBytes) return Error::InvalidScratch;
  for (U32 i = 0; i < TotalPages; ++i) {
    const U64 address = orderedPages[i];
    if (address < Page || address > AddressLimit - Page || address % Page) return Error::InvalidAddress;
  }
  U32 base = 0;
  for (U32 resource = 0; resource < ResourceCount; ++resource) {
    // Native allocation intentionally requires all 2 MiB of logs contiguous,
    // stricter than the five directly addressed 64 KiB LibOS log windows.
    if (resource == Bootloader || resource == Logs) {
      for (U32 i = 1; i < Pages[resource]; ++i)
        if (orderedPages[base + i] != orderedPages[base + i - 1] + Page) return Error::Noncontiguous;
    }
    base += Pages[resource];
  }
  for (U32 i = 0; i < TotalPages; ++i) sortScratch[i] = orderedPages[i];
  Detail::sort(sortScratch, TotalPages);
  for (U32 i = 1; i < TotalPages; ++i)
    if (sortScratch[i] == sortScratch[i - 1]) return Error::DuplicatePage;
  return Error::Ok;
}

class Session {
  bool held = false;
  Error last = Error::Ok;
  Result result(Error error) const { return {error, held}; }
  Error checkChunk(State required, U32 resource, U64 offset, U64 length, const U64 *progress) const {
    if (state != required) return Error::InvalidState;
    if (resource >= ResourceCount) return Error::InvalidResource;
    if (!length || length > Page) return Error::InvalidLength;
    // Subtraction checks the entire span without offset+length overflow.
    if (offset > Sizes[resource] || length > Sizes[resource] - offset) return Error::InvalidLength;
    if (offset != progress[resource]) return Error::InvalidOffset;
    return Error::Ok;
  }
public:
  State state = State::Idle;
  U64 written[ResourceCount] = {}, readBytes[ResourceCount] = {};
  bool cleanupVerified = false;
  bool resourcesHeld() const { return held; }
  Error lastError() const { return last; }

  Result fail(Error error) {
    state = State::Failed;
    last = error == Error::Ok ? Error::TransportFailed : error;
    return result(last);
  }
  // Called after the native allocation attempt, including a partial failure.
  // The native caller must pass the actual validatePages/allocator outcome.
  Result begin(Error mappingValidation) {
    if (state != State::Idle) return fail(Error::InvalidState);
    held = true; cleanupVerified = false;
    if (mappingValidation != Error::Ok) return fail(mappingValidation);
    state = State::Writing;
    return result(Error::Ok);
  }
  Error checkWrite(U32 resource, U64 offset, U64 length) const {
    return checkChunk(State::Writing, resource, offset, length, written);
  }
  // Check before invoking transport; commit only after the actual CPU copy.
  Result write(U32 resource, U64 offset, U64 length, bool copySucceeded = true) {
    const Error checked = checkWrite(resource, offset, length);
    if (checked != Error::Ok) return fail(checked);
    if (!copySucceeded) return fail(Error::TransportFailed);
    written[resource] += length;
    return result(Error::Ok);
  }
  Error checkPublish() const {
    if (state != State::Writing) return Error::InvalidState;
    for (U32 i = 0; i < ResourceCount; ++i)
      if (written[i] != Sizes[i]) return Error::IncompleteWrites;
    return Error::Ok;
  }
  Result publish(bool syncSucceeded = true) {
    const Error checked = checkPublish();
    if (checked != Error::Ok) return fail(checked);
    if (!syncSucceeded) return fail(Error::TransportFailed);
    state = State::Published;
    return result(Error::Ok);
  }
  Error checkRead(U32 resource, U64 offset, U64 length) const {
    return checkChunk(State::Published, resource, offset, length, readBytes);
  }
  Result read(U32 resource, U64 offset, U64 length, bool copySucceeded = true) {
    const Error checked = checkRead(resource, offset, length);
    if (checked != Error::Ok) return fail(checked);
    if (!copySucceeded) return fail(Error::TransportFailed);
    readBytes[resource] += length;
    return result(Error::Ok);
  }
  Error checkFinish() const {
    if (state != State::Published) return Error::InvalidState;
    for (U32 i = 0; i < ResourceCount; ++i)
      if (readBytes[i] != Sizes[i]) return Error::IncompleteReads;
    return Error::Ok;
  }
  Result finish(bool cleanupSucceeded = true) {
    const Error checked = checkFinish();
    if (checked != Error::Ok) return fail(checked);
    if (!cleanupSucceeded) return fail(Error::CleanupFailed);
    held = false; cleanupVerified = true; state = State::Finished;
    return result(Error::Ok);
  }
  // There is no firmware start in this protocol. Abort is therefore always
  // permitted, including retrying a previously unsuccessful native cleanup.
  Result abort(bool cleanupSucceeded = true) {
    if (state == State::Finished || state == State::Aborted) return result(Error::Ok);
    if (held && !cleanupSucceeded) return fail(Error::CleanupFailed);
    held = false; cleanupVerified = true; state = State::Aborted;
    return result(Error::Ok);
  }
  Result cleanupResult(bool succeeded) {
    if (!held) return result(Error::Ok);
    if (!succeeded) return fail(Error::CleanupFailed);
    held = false; cleanupVerified = true;
    // A cleanup acknowledgment cannot leave a writable/published session
    // behind after its native resources have been released.
    if (state == State::Writing || state == State::Published) return fail(Error::InvalidState);
    return result(Error::Ok);
  }
};
} // namespace GSPDmaProtocol
