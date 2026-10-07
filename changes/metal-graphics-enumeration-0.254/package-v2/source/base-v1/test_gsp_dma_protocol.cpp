#include "driver/GSPDmaProtocol.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <limits>
#include <algorithm>

using namespace GSPDmaProtocol;
static unsigned checks = 0;
static void check(bool good, const char *message) {
  ++checks;
  if (!good) { std::fprintf(stderr, "FAIL: %s\n", message); std::abort(); }
}
static U32 base(U32 resource) {
  U32 value = 0;
  for (U32 i = 0; i < resource; ++i) value += Pages[i];
  return value;
}
static std::vector<U64> pageFixture() {
  std::vector<U64> result(TotalPages);
  for (U32 i = 0; i < TotalPages; ++i) result[i] = (1ULL << 32) + U64(i + 1) * Page;
  return result;
}
static Error validate(const std::vector<U64> &pages) {
  std::vector<U64> scratch(TotalPages);
  return validatePages(pages.data(), U32(pages.size()), scratch.data(), U32(scratch.size()));
}
static bool transferAll(Session &session, bool writing, bool irregular = false) {
  // Reverse resource order proves each resource has its own sequential cursor.
  // 1+4095 byte chunks exercise legal spans that do not match page boundaries.
  for (U32 r = ResourceCount; r; --r) {
    const U32 resource = r - 1;
    U64 offset = 0;
    while (offset < Sizes[resource]) {
      const U64 length = irregular ? (offset % Page ? Page - offset % Page : 1) : Page;
      const Error before = writing ? session.checkWrite(resource, offset, length) : session.checkRead(resource, offset, length);
      if (before != Error::Ok) return false;
      const Result result = writing ? session.write(resource, offset, length, true) : session.read(resource, offset, length, true);
      if (!result.ok()) return false;
      offset += length;
    }
  }
  return true;
}
static Session published() {
  Session session;
  if (!session.begin(Error::Ok).ok() || !transferAll(session, true) || !session.publish(true).ok()) std::abort();
  return session;
}

// Models a transport which must preserve every allocated object while any
// prepare/clear/complete step is unresolved. The Session sees only its result;
// the native adapter owns and verifies the actual IOKit cleanup sequence.
struct Transport {
  bool owned[ResourceCount] = {};
  unsigned releases[ResourceCount] = {}, cleanupCalls = 0;
  Error allocate(U32 failAt) {
    for (U32 i = 0; i < ResourceCount; ++i) {
      owned[i] = true;
      if (i == failAt) return Error::TransportFailed;
    }
    return Error::Ok;
  }
  bool cleanup(bool succeeds) {
    ++cleanupCalls;
    if (!succeeds) return false;
    for (U32 i = 0; i < ResourceCount; ++i) {
      if (owned[i]) { owned[i] = false; ++releases[i]; }
    }
    return true;
  }
};

static void pageTests() {
  U32 totalPages = 0; U64 totalBytes = 0;
  for (U32 i = 0; i < ResourceCount; ++i) {
    totalPages += Pages[i]; totalBytes += Sizes[i];
    check(Sizes[i] == U64(Pages[i]) * Page, "pinned resource sizes are page-exact");
  }
  check(totalPages == 16212 && totalBytes == 66404352 && totalPages == TotalPages && totalBytes == TotalBytes,
        "pinned firmware host allocation totals");
  static_assert(U32(State::Idle) == 0 && U32(State::Writing) == 1 && U32(State::Published) == 2 &&
                U32(State::Finished) == 3 && U32(State::Aborted) == 4 && U32(State::Failed) == 5, "client state ABI");
  auto pages = pageFixture();
  const auto original = pages;
  std::vector<U64> scratch(TotalPages);
  check(validatePages(pages.data(), TotalPages, scratch.data(), TotalPages) == Error::Ok, "valid actual-size mapping profile");
  check(pages == original, "page validator preserves exported logical ordering");
  check(std::is_sorted(scratch.begin(), scratch.end()), "bounded scratch sort establishes duplicate adjacency");
  std::reverse(pages.begin(), pages.begin() + Pages[Radix3]);
  check(validate(pages) == Error::Ok, "radix image permits arbitrary discontiguous logical order");
  std::swap(pages[base(Queues)], pages[base(Queues) + 100]);
  std::swap(pages[base(BooterLoad)], pages[base(BooterLoad) + 14]);
  check(validate(pages) == Error::Ok, "page-tabled queues and staged booter need not be contiguous");
  pages.back() = AddressLimit - Page;
  check(validate(pages) == Error::Ok, "last complete 40-bit page accepted");
  pages = original;
  pages[0] = Page;
  check(validate(pages) == Error::Ok, "lowest nonzero page accepted");
  for (U64 bad : {0ULL, 1ULL, U64(Page - 1), (1ULL << 32) + 1, AddressLimit - Page + 1, AddressLimit,
                  std::numeric_limits<U64>::max()}) {
    pages = original; pages[0] = bad;
    check(validate(pages) == Error::InvalidAddress, "invalid or overflowing page rejected");
  }
  for (U32 resource : {U32(Radix3), U32(Signature), U32(Metadata), U32(Queues), U32(Rmargs), U32(LibosArgs), U32(BooterLoad)}) {
    pages = original; pages[base(resource)] = original[base(Logs) + 10];
    check(validate(pages) == Error::DuplicatePage, "global alias between different allocations rejected");
  }
  pages = original; pages[10] = pages[11];
  check(validate(pages) == Error::DuplicatePage, "alias inside large image allocation rejected");
  for (U32 resource : {U32(Bootloader), U32(Logs)}) {
    for (U32 index : {1U, Pages[resource] - 1}) {
      pages = original; pages[base(resource) + index] = AddressLimit - U64(index + 1) * Page;
      check(validate(pages) == Error::Noncontiguous, "direct resource contiguity required through last page");
    }
  }
  check(validatePages(nullptr, TotalPages, scratch.data(), TotalPages) == Error::InvalidPageCount, "null page source");
  for (U32 count : {0U, TotalPages - 1, TotalPages + 1, std::numeric_limits<U32>::max()})
    check(validatePages(original.data(), count, scratch.data(), TotalPages) == Error::InvalidPageCount, "exact page count before dereference");
  check(validatePages(original.data(), TotalPages, nullptr, TotalPages) == Error::InvalidScratch, "null sort scratch");
  check(validatePages(original.data(), TotalPages, scratch.data(), TotalPages - 1) == Error::InvalidScratch, "short sort scratch");
  pages = original;
  check(validatePages(pages.data(), TotalPages, pages.data(), TotalPages) == Error::InvalidScratch, "in-place scratch cannot destroy exported ordering");
  std::vector<U64> overlap(TotalPages + 1);
  check(validatePages(overlap.data(), TotalPages, overlap.data() + 1, TotalPages) == Error::InvalidScratch, "forward overlapping scratch rejected");
  check(validatePages(overlap.data() + 1, TotalPages, overlap.data(), TotalPages) == Error::InvalidScratch, "backward overlapping scratch rejected");
}

static void lifecycleTests() {
  for (bool irregular : {false, true}) {
    Session session;
    check(session.state == State::Idle && !session.resourcesHeld() && !session.cleanupVerified, "new session has no claimed resources");
    check(session.begin(Error::Ok).ok() && session.state == State::Writing && session.resourcesHeld(), "successful preparation begins upload");
    check(transferAll(session, true, irregular), "all nine uploads allow independent exact sequential offsets");
    check(session.publish(true).ok() && session.state == State::Published, "publish follows all writes and synchronization");
    check(transferAll(session, false, irregular), "all nine readbacks are independently sequential");
    check(session.finish(true).ok() && session.state == State::Finished && !session.resourcesHeld() && session.cleanupVerified,
          "finish needs every readback and verified native cleanup");
    check(session.abort(false).ok() && session.state == State::Finished, "abort after finish is idempotent and preserves evidence");
    check(!session.begin(Error::Ok).ok() && !session.resourcesHeld(), "finished session cannot allocate again");
  }
  {
    Session session; session.begin(Error::Ok);
    check(session.checkWrite(Radix3, 0, Page) == Error::Ok && !session.written[Radix3], "precopy validation does not commit bytes");
    check(session.checkWrite(ResourceCount, 0, Page) == Error::InvalidResource && session.state == State::Writing,
          "rejected precheck leaves caller responsible for failure handling");
    check(session.write(Radix3, 0, Page, false).error == Error::TransportFailed && !session.written[Radix3], "failed CPU copy never advances upload");
    check(session.state == State::Failed && session.resourcesHeld(), "transport failure requests cleanup");
    check(!session.cleanupResult(false).ok() && session.resourcesHeld() && !session.cleanupVerified, "cleanup failure retains ownership");
    check(session.cleanupResult(true).ok() && !session.resourcesHeld() && session.state == State::Failed && session.cleanupVerified,
          "retry acknowledges cleanup without replaying success");
  }
  for (U32 resource = 0; resource < ResourceCount; ++resource) {
    Session session; session.begin(Error::Ok);
    check(session.write(resource, 0, 1).ok(), "first short upload advances selected resource only");
    check(session.write(resource, 0, 1).error == Error::InvalidOffset && session.written[resource] == 1 && session.resourcesHeld(),
          "duplicate write fails without counting bytes twice");
    check(session.abort(true).ok() && session.state == State::Aborted && session.cleanupVerified, "upload fault can abort safely");
    session = published();
    check(session.read(resource, 0, 1).ok(), "first short readback advances selected resource only");
    check(session.read(resource, 2, 1).error == Error::InvalidOffset && session.readBytes[resource] == 1, "readback cannot skip unread bytes");
    check(session.abort(true).ok() && !session.resourcesHeld(), "readback fault can abort safely");
  }
  for (bool reading : {false, true}) {
    for (U64 length : {0ULL, U64(Page + 1), std::numeric_limits<U64>::max()}) {
      Session session = reading ? published() : Session{};
      if (!reading) session.begin(Error::Ok);
      const Result result = reading ? session.read(0, 0, length) : session.write(0, 0, length);
      check(result.error == Error::InvalidLength && session.state == State::Failed && result.cleanupRequired, "empty/oversized/overflowing chunks fail closed");
    }
    for (U64 offset : {Sizes[0], Sizes[0] + 1, std::numeric_limits<U64>::max()}) {
      Session session = reading ? published() : Session{};
      if (!reading) session.begin(Error::Ok);
      const Result result = reading ? session.read(0, offset, Page) : session.write(0, offset, Page);
      check(result.error == Error::InvalidLength && session.written[0] == (reading ? Sizes[0] : 0), "past-end offsets rejected without addition overflow");
    }
    for (U32 resource : {ResourceCount, std::numeric_limits<U32>::max()}) {
      Session session = reading ? published() : Session{};
      if (!reading) session.begin(Error::Ok);
      const Result result = reading ? session.read(resource, 0, 1) : session.write(resource, 0, 1);
      check(result.error == Error::InvalidResource && session.state == State::Failed, "invalid resource cannot index progress arrays");
    }
  }
  {
    Session session; session.begin(Error::Ok);
    check(session.read(0, 0, 1).error == Error::InvalidState && !session.readBytes[0], "read before publication fails");
    session = published();
    check(session.write(0, 0, 1).error == Error::InvalidState && session.written[0] == Sizes[0], "write after publication cannot mutate approved image");
    session = published();
    check(session.publish().error == Error::InvalidState && session.resourcesHeld(), "duplicate publication fails");
    session = Session{}; session.begin(Error::Ok);
    check(session.begin(Error::Ok).error == Error::InvalidState && session.resourcesHeld(), "double begin retains current ownership for cleanup");
  }
  for (U32 resource = 0; resource < ResourceCount; ++resource) {
    Session session; session.begin(Error::Ok); transferAll(session, true);
    // Simulate exactly one unfinished final chunk for each allocation.
    session.written[resource] -= Page;
    check(session.publish().error == Error::IncompleteWrites && session.resourcesHeld(), "publish cannot omit any allocation tail");
    session = published(); transferAll(session, false); session.readBytes[resource] -= Page;
    check(session.finish(true).error == Error::IncompleteReads && session.resourcesHeld() && !session.cleanupVerified,
          "finish cannot certify unread resource tail");
  }
  {
    Session session; session.begin(Error::Ok); transferAll(session, true);
    check(session.publish(false).error == Error::TransportFailed && session.state == State::Failed, "synchronize failure cannot publish");
    session = published();
    check(session.read(Logs, 0, Page, false).error == Error::TransportFailed && !session.readBytes[Logs], "failed copyout does not count readback");
    session = published(); transferAll(session, false);
    check(session.finish(false).error == Error::CleanupFailed && session.resourcesHeld() && !session.cleanupVerified, "failed final cleanup cannot produce Finished");
    check(session.abort(false).error == Error::CleanupFailed && session.resourcesHeld(), "repeated cleanup failure remains retained");
    check(session.abort(true).ok() && session.state == State::Aborted && !session.resourcesHeld(), "later successful cleanup permits abort");
    check(session.abort(false).ok() && session.state == State::Aborted, "abort is idempotent after cleanup");
    check(!session.begin(Error::Ok).ok() && !session.resourcesHeld(), "aborted session cannot restart");
  }
  {
    Session session;
    check(session.abort(false).ok() && session.state == State::Aborted && !session.resourcesHeld(), "idle abort needs no native objects");
  }
  for (bool alreadyPublished : {false, true}) {
    Session session = alreadyPublished ? published() : Session{};
    if (!alreadyPublished) session.begin(Error::Ok);
    check(session.cleanupResult(true).error == Error::InvalidState && session.state == State::Failed && !session.resourcesHeld(),
          "unexpected cleanup cannot leave a live cursor on freed resources");
    check(session.write(0, 0, 1).error == Error::InvalidState && session.read(0, 0, 1).error == Error::InvalidState,
          "copy operations remain disabled after resources are released");
  }
}

static void cleanupTests() {
  for (U32 failedAllocation = 0; failedAllocation < ResourceCount; ++failedAllocation) {
    for (bool firstCleanupSucceeds : {false, true}) {
      Transport transport; Session session;
      check(session.begin(transport.allocate(failedAllocation)).error == Error::TransportFailed && session.resourcesHeld(),
            "every partial begin failure requires native cleanup");
      const Result first = session.abort(transport.cleanup(firstCleanupSucceeds));
      check(first.ok() == firstCleanupSucceeds && session.resourcesHeld() == !firstCleanupSucceeds,
            "partial allocation cleanup result controls retention");
      bool before = true;
      for (U32 i = 0; i < ResourceCount; ++i)
        before = before && (transport.releases[i] == (firstCleanupSucceeds && i <= failedAllocation ? 1U : 0U));
      check(before, "failed cleanup releases no allocated object");
      if (!firstCleanupSucceeds) check(session.abort(transport.cleanup(true)).ok(), "cleanup failure can be retried before firmware");
      bool after = true;
      for (U32 i = 0; i < ResourceCount; ++i)
        after = after && !transport.owned[i] && transport.releases[i] == (i <= failedAllocation ? 1U : 0U);
      check(after && session.state == State::Aborted && session.cleanupVerified, "all and only partially allocated objects released once");
    }
  }
  for (Error error : {Error::InvalidPageCount, Error::InvalidAddress, Error::DuplicatePage, Error::Noncontiguous, Error::InvalidScratch}) {
    Transport transport; transport.allocate(ResourceCount); Session session;
    check(session.begin(error).error == error && session.state == State::Failed && session.resourcesHeld(), "page validation failure is terminal for this allocation session");
    check(session.abort(transport.cleanup(true)).ok() && session.cleanupVerified, "invalid page mappings still clean up every allocation");
    check(std::all_of(transport.releases, transport.releases + ResourceCount, [](unsigned value) { return value == 1; }),
          "mapping validation failure releases all nine native allocations");
  }
}

int main() {
  pageTests(); lifecycleTests(); cleanupTests();
  std::printf("GSP DMA protocol: %u checks passed\n", checks);
  return 0;
}
