#include "driver/GSPLaunchOwnership.hpp"
#include <cassert>
#include <cstdio>
#include <vector>

namespace L = GSPLaunchOwnership;
namespace D = GSPDmaProtocol;
static unsigned checks = 0;
#define CHECK(expression) do { ++checks; assert(expression); } while (0)

static Boot0::Facts identity() {
  Boot0::Facts f;
  f.identity = 0x252010de; f.subsystem = 0x104c1043;
  f.targetBDF = f.barTypesValid = true; f.command = 0; f.pmcsr = 8; f.link = 0x1083;
  f.bar0 = f.descriptor0 = 0xfb000000; f.length0 = 16ULL << 20;
  f.bar1 = f.descriptor1 = 0x824000000; f.length1 = 64ULL << 20;
  return f;
}
static FWSECPreflight::Snapshot board() {
  FWSECPreflight::Snapshot s;
  const unsigned raw[] = {0, 1, 0x1ffffe00, 0, 0x10, 0x10, 0, 0x80420100, 1, 0};
  for (unsigned i = 0; i < FWSECPreflight::Count; ++i) {
    s.first[i] = s.second[i] = raw[i]; s.reads[i] = 2;
  }
  s.complete = s.layoutValid = s.engineIdle = s.wprClear = s.displaySupported = true;
  s.vramBytes = L::VramBytes; s.workspaceBoundary = s.frtsEnd = L::BiosStart;
  s.frtsOffset = L::ExpectedLayout.regions[L::Frts].offset; s.wprLo = 0x1ffffe0000ULL;
  return s;
}
static FWSECDisplay::Snapshot display() {
  FWSECDisplay::Snapshot d;
  d.first[0] = d.second[0] = d.mask = 15; d.first[1] = d.second[1] = d.count = 4;
  for (unsigned i = 0; i < FWSECDisplay::Count; ++i) d.reads[i] = 2;
  d.complete = d.idle = true;
  return d;
}
static L::Ledger claimed() {
  L::Ledger ledger;
  CHECK(ledger.claim(identity(), board(), display(), 17, true));
  return ledger;
}
static L::Quiescence proof(const L::Ledger &ledger) {
  L::Quiescence p;
  p.generation = ledger.generation(); p.exposure = ledger.exposure(); p.command = 0;
  p.engineReset = p.dmaIdle = p.pciTransactionsDrained = p.targetsCleared = true;
  p.cpuStopped = p.coreSelectionVerified = p.hostBuffersIntact = p.commandStable = p.providerHeld = true;
  return p;
}
static L::Ledger exposed() {
  auto ledger = claimed();
  CHECK(ledger.freezeUploads(17)); CHECK(ledger.noteDeviceExposureAttempted(17));
  return ledger;
}
static void store(std::vector<unsigned char> &data, unsigned offset, L::U64 value) {
  for (unsigned i = 0; i < 8; ++i) data.at(offset + i) = (value >> (8 * i)) & 255;
}

int main() {
  CHECK(L::validLayout(L::ExpectedLayout));
  CHECK(L::ReservedBytes == 193 * L::MiB);
  CHECK(L::containsOwned(L::ReservedStart, L::ReservedBytes));
  CHECK(!L::containsOwned(L::ReservedStart - 1, 1));
  CHECK(!L::containsOwned(L::BiosStart, 1));
  CHECK(!L::containsOwned(L::ReservedStart, ~L::U64(0)));
  CHECK(!L::containsOwned(L::ReservedStart, 0));
  CHECK(L::containsOwned(L::BiosStart - 1, 1));
  CHECK(!L::containsOwned(L::BiosStart - 1, 2));
  for (unsigned i = 0; i < L::RegionCount; ++i) {
    auto layout = L::ExpectedLayout; ++layout.regions[i].offset; CHECK(!L::validLayout(layout));
    layout = L::ExpectedLayout; ++layout.regions[i].size; CHECK(!L::validLayout(layout));
    layout = L::ExpectedLayout; layout.regions[i].size = ~L::U64(0); CHECK(!L::validLayout(layout));
    L::Ledger ledger; CHECK(!ledger.claim(identity(), board(), display(), 17, true, layout));
  }
  {
    L::Ledger ledger;
    CHECK(!ledger.canReleaseHost() && !ledger.canMutateUploads() && !ledger.requiresPin());
    CHECK(!ledger.noteStartAttempted(17) && !ledger.noteDeviceExposureAttempted(17));
    CHECK(!ledger.claim(identity(), board(), display(), 0, true));
    CHECK(!ledger.claim(identity(), board(), display(), 17, false));
    auto f = identity(); f.subsystem ^= 1; CHECK(!ledger.claim(f, board(), display(), 17, true));
    f = identity(); f.command = 6; CHECK(!ledger.claim(f, board(), display(), 17, true));
    auto b = board(); b.wprClear = false; CHECK(!ledger.claim(identity(), b, display(), 17, true));
    auto d = display(); d.first[2] = d.second[2] = 0x100; CHECK(!ledger.claim(identity(), board(), d, 17, true));
    CHECK(ledger.claim(identity(), board(), display(), 17, true));
    CHECK(!ledger.claim(identity(), board(), display(), 17, true));
    CHECK(ledger.matchesBeforeDeviceUse(identity(), board(), display(), 17, true));
    CHECK(!ledger.matchesBeforeDeviceUse(identity(), board(), display(), 18, true));
    CHECK(!ledger.matchesBeforeDeviceUse(identity(), board(), display(), 17, false));
    CHECK(ledger.canMutateUploads() && ledger.canReleaseHost() && ledger.canReleaseRegion());
    CHECK(!ledger.freezeUploads(18)); CHECK(ledger.canMutateUploads());
    CHECK(ledger.freezeUploads(17)); CHECK(!ledger.canMutateUploads());
    CHECK(!ledger.freezeUploads(17)); CHECK(!ledger.requiresPin());
    CHECK(!ledger.releaseBeforeStart(18, true)); CHECK(ledger.owned());
    CHECK(ledger.releaseBeforeStart(17, true)); CHECK(!ledger.owned());
    CHECK(!ledger.claim(identity(), board(), display(), 18, true));
    CHECK(!ledger.releaseBeforeStart(17, true));
  }
  // Every incomplete cleanup proof blocks release. Neither PCI command=0 nor
  // an idle/halted flag alone is sufficient after a staging DMA attempt.
  for (unsigned i = 0; i < 12; ++i) {
    auto ledger = exposed(); auto p = proof(ledger);
    switch (i) {
      case 0: p.generation++; break; case 1: p.exposure--; break; case 2: p.command = 6; break;
      case 3: p.engineReset = false; break; case 4: p.dmaIdle = false; break;
      case 5: p.pciTransactionsDrained = false; break; case 6: p.targetsCleared = false; break;
      case 7: p.cpuStopped = false; break; case 8: p.coreSelectionVerified = false; break;
      case 9: p.hostBuffersIntact = false; break; case 10: p.commandStable = false; break;
      case 11: p.providerHeld = false; break;
    }
    CHECK(!ledger.noteQuiescence(p)); CHECK(ledger.requiresPin());
    CHECK(!ledger.canReleaseHost() && !ledger.canReleaseRegion() && !ledger.canMutateUploads());
    CHECK(!ledger.releaseBeforeStart(17, true));
    CHECK(ledger.noteQuiescence(proof(ledger))); CHECK(ledger.canReleaseHost());
    CHECK(!ledger.canMutateUploads()); CHECK(ledger.releaseBeforeStart(17, true));
  }
  {
    auto ledger = exposed(); const auto old = proof(ledger);
    CHECK(ledger.noteQuiescence(old)); CHECK(!ledger.requiresPin());
    CHECK(ledger.noteDeviceExposureAttempted(17)); CHECK(ledger.requiresPin());
    CHECK(!ledger.noteQuiescence(old)); // Generation matches; old exposure does not.
    CHECK(ledger.noteQuiescence(proof(ledger)));
    auto bad = proof(ledger); bad.hostBuffersIntact = false;
    CHECK(!ledger.noteQuiescence(bad)); CHECK(!ledger.canReleaseHost());
    CHECK(ledger.noteQuiescence(proof(ledger)));
    CHECK(!ledger.matchesBeforeDeviceUse(identity(), board(), display(), 17, true));
  }
  for (unsigned variant = 0; variant < 3; ++variant) {
    auto ledger = exposed(); CHECK(ledger.noteQuiescence(proof(ledger)));
    // Success, failed write, timeout, and an invalid generation must all keep
    // attempted START sticky. There is intentionally no 'write succeeded' flag.
    CHECK(ledger.noteStartAttempted(variant ? 18 : 17) == !variant);
    CHECK(ledger.startAttempted() && ledger.requiresPin());
    CHECK(!ledger.noteQuiescence(proof(ledger)));
    CHECK(!ledger.releaseBeforeStart(17, true));
    CHECK(!ledger.noteStartAttempted(17)); CHECK(!ledger.noteDeviceExposureAttempted(17));
    CHECK(!ledger.canReleaseHost() && !ledger.canReleaseRegion() && !ledger.canMutateUploads());
  }
  {
    auto ledger = claimed(); CHECK(!ledger.noteStartAttempted(17));
    CHECK(ledger.requiresPin()); CHECK(!ledger.canReleaseHost());
    auto stage = claimed(); CHECK(!stage.noteDeviceExposureAttempted(17));
    CHECK(stage.requiresPin()); CHECK(stage.noteQuiescence(proof(stage)));
  }
  for (unsigned variant = 0; variant < 2; ++variant) {
    auto ledger = claimed();
    if (variant) ledger.noteCleanupFailure();
    else CHECK(!ledger.releaseBeforeStart(17, false));
    CHECK(ledger.requiresPin()); CHECK(!ledger.canReleaseHost());
    CHECK(!ledger.releaseBeforeStart(17, true)); // Native cleanup failure is sticky.
  }

  std::vector<L::U64> pages(D::TotalPages), scratch(D::TotalPages);
  for (unsigned i = 0; i < D::TotalPages; ++i) pages[i] = 0x1000000ULL + L::U64(i) * D::Page;
  CHECK(D::validatePages(pages.data(), D::TotalPages, scratch.data(), D::TotalPages) == D::Error::Ok);
  const auto direct = [&](unsigned resource, L::U64 offset, L::U64 address, L::U64 length) {
    return L::validateDirectPointer(pages.data(), D::TotalPages, resource, offset, address, length);
  };
  for (unsigned resource = 0; resource < D::ResourceCount; ++resource) {
    const auto address = pages[L::firstPage(resource)];
    CHECK(direct(resource, 0, address, D::Sizes[resource]));
    CHECK(direct(resource, D::Sizes[resource] - 1, address + D::Sizes[resource] - 1, 1));
    CHECK(!direct(resource, D::Sizes[resource], address + D::Sizes[resource], 1));
    CHECK(!direct(resource, 0, address, D::Sizes[resource] + 1));
    CHECK(!direct(resource, 0, address + D::Page, 1));
    CHECK(!direct(resource, ~L::U64(0), address, 1));
    CHECK(!direct(resource, 0, address, ~L::U64(0)));
  }
  CHECK(!L::validateDirectPointer(nullptr, D::TotalPages, 0, 0, pages[0], 1));
  CHECK(!L::validateDirectPointer(pages.data(), D::TotalPages - 1, 0, 0, pages[0], 1));
  CHECK(!direct(D::ResourceCount, 0, pages[0], 1)); CHECK(!direct(0, 0, pages[0], 0));
  CHECK(direct(0, D::Page - 1, pages[0] + D::Page - 1, 2));
  pages[1] += D::Page;
  CHECK(direct(0, 0, pages[0], D::Page));
  CHECK(!direct(0, D::Page - 1, pages[0] + D::Page - 1, 2));
  pages[1] -= D::Page;
  std::vector<unsigned char> table(4096, 0);
  for (unsigned i = 0; i < 129; ++i) store(table, i * 8, pages[i]);
  CHECK(L::validatePageTable(table.data(), table.size(), 0, pages.data(), 129));
  CHECK(!L::validatePageTable(table.data(), table.size(), ~L::U64(0), pages.data(), 129));
  CHECK(!L::validatePageTable(table.data(), 129 * 8 - 1, 0, pages.data(), 129));
  CHECK(!L::validatePageTable(table.data(), D::TotalBytes + 1, 0, pages.data(), 129));
  CHECK(!L::validatePageTable(nullptr, table.size(), 0, pages.data(), 129));
  CHECK(!L::validatePageTable(table.data(), table.size(), 0, nullptr, 129));
  CHECK(!L::validatePageTable(table.data(), table.size(), 0, pages.data(), 0));
  CHECK(!L::validatePageTable(table.data(), table.size(), 0, pages.data(), D::TotalPages + 1));
  table[128 * 8] ^= 1; CHECK(!L::validatePageTable(table.data(), table.size(), 0, pages.data(), 129));

  L::ContentSeal seal;
  CHECK(!seal.completeFor(17));
  seal.generation = 17; seal.structuralMask = L::ContentSeal::AllStructures;
  seal.firmwareMask = L::ContentSeal::FirmwareResources;
  seal.globalPagesValidated = seal.uploadsSynchronized = seal.readbacksVerified = true;
  CHECK(seal.completeFor(17)); CHECK(!seal.completeFor(18)); CHECK(!seal.completeFor(0));
  for (unsigned i = 0; i < D::ResourceCount; ++i) {
    auto missing = seal; missing.structuralMask &= ~(1U << i); CHECK(!missing.completeFor(17));
    if (seal.firmwareMask & (1U << i)) {
      missing = seal; missing.firmwareMask &= ~(1U << i); CHECK(!missing.completeFor(17));
    }
  }
  for (unsigned i = 0; i < 3; ++i) {
    auto missing = seal;
    if (i == 0) missing.globalPagesValidated = false;
    if (i == 1) missing.uploadsSynchronized = false;
    if (i == 2) missing.readbacksVerified = false;
    CHECK(!missing.completeFor(17));
  }
  std::printf("GSP launch ownership: %u checks passed\n", checks);
}
