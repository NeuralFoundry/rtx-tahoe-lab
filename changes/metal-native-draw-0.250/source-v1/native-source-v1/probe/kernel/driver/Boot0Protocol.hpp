#pragma once
// The same bounded transaction is used by the kernel adapter and fault tests.
// The transport deliberately exposes no DMA, BAR resizing, reset, or MMIO write.
namespace Boot0 {
using U32 = unsigned int;
using U64 = unsigned long long;
static_assert(sizeof(U32) == 4 && sizeof(U64) == 8, "Unsupported integer widths");

struct Facts {
  U32 identity = 0, subsystem = 0, command = 0xffff, pmcsr = 0xffff, link = 0;
  U64 bar0 = 0, descriptor0 = 0, length0 = 0;
  U64 bar1 = 0, descriptor1 = 0, length1 = 0;
  bool targetBDF = false, barTypesValid = false;
};

inline const char *preflight(const Facts &f) {
  if (f.identity != 0x252010de || f.subsystem != 0x104c1043 || !f.targetBDF) return "target-mismatch";
  if (f.command != 0) return "device-command-not-idle";
  if ((f.pmcsr & 3) != 0 || f.pmcsr == 0xffff) return "device-not-D0";
  if (!(f.link & 15) || !((f.link >> 4) & 63) || (f.link & 0x800)) return "link-not-ready";
  if (!f.barTypesValid) return "unexpected-BAR-types";
  if (!f.bar0 || (f.bar0 & 0xfff) || f.bar0 != f.descriptor0 || f.length0 != (16ULL << 20)) return "BAR0-mismatch";
  if (f.bar0 > 0xffffffffULL - 0xfff) return "BAR0-address-out-of-range";
  if (!f.bar1 || (f.bar1 & ((64ULL << 20) - 1)) || f.bar1 != f.descriptor1 || f.length1 != (64ULL << 20))
    return "BAR1-fix-not-present";
  if (f.bar1 >= (1ULL << 40)) return "BAR1-address-out-of-range";
  return nullptr;
}

struct Result {
  const char *status = "not-run";
  U32 commandBefore = 0xffff, commandDuring = 0xffff, commandAfter = 0xffff;
  U32 first = 0, second = 0, reads = 0;
  bool enableAttempted = false, restoreVerified = false, passed = false;
};

struct IdentityOnly {
  template <class Transport> const char *operator()(Transport &) { return nullptr; }
};

template <class Transport, class Extra> Result run(Transport &io, Extra &extra) {
  Result result;
  if (!io.open()) { result.status = "device-busy"; return result; }
  const Facts facts = io.facts();
  result.commandBefore = facts.command;
  bool mapped = false, validRead = false;
  do {
    if (const char *error = preflight(facts)) { result.status = error; break; }
    // Mapping does not read the MMIO window; map before enabling memory decode.
    if (!(mapped = io.mapPage())) { result.status = "map-failed"; break; }
    // Catch a concurrent command change before the only permitted PCI change.
    if (io.command() != facts.command) { result.status = "command-changed-before-enable"; break; }
    result.enableAttempted = true;
    io.setMemory(true);
    result.commandDuring = io.command();
    if (result.commandDuring != 2) { result.status = "memory-enable-not-confirmed"; break; }
    result.first = io.readBoot0();
    result.reads = 1;
    if (!result.first || result.first == 0xffffffff) { result.status = "invalid-BOOT0"; break; }
    result.second = io.readBoot0();
    result.reads = 2;
    if (result.first != result.second) { result.status = "unstable-BOOT0"; break; }
    if (((result.first >> 20) & 0x1ff) != 0x176) { result.status = "unexpected-chipset"; break; }
    if (const char *error = extra(io)) { result.status = error; break; }
    validRead = true;
    result.status = "GA106-BOOT0-read";
  } while (false);

  // Every path after a successful open reaches this cleanup, even failed reads.
  if (mapped) io.unmapPage();
  if (result.enableAttempted) io.setMemory(false);
  result.commandAfter = io.command();
  result.restoreVerified = result.commandAfter == result.commandBefore;
  if (result.enableAttempted && !result.restoreVerified) result.status = "command-restore-failed";
  io.close();
  result.passed = validRead && result.restoreVerified;
  return result;
}

template <class Transport> Result run(Transport &io) {
  IdentityOnly extra;
  return run(io, extra);
}
}
