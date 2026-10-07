#pragma once
#include "SEC2StageProtocol.hpp"

// Selector9: exactly160 little-endian u64s. Selector10: [kind,first,count],
// kind0=DMEM words actually read, kind1=all235 DMA completion slots; u32 output.
namespace SEC2StageWire {
using U64 = unsigned long long;
constexpr U64 Magic = 0x5345433253544731ULL;
constexpr unsigned Words = 160, Bytes = Words * 8, InitialBase = 48, FinalBase = 62;
enum Field : unsigned {
  MagicWord, Abi, Attempted, Passed, ReleaseSafe, Completed, HasRiscv, ImageMatched,
  EnvironmentMatched, CpuIntact, Staged, Quiescent, TargetsCleared, FinalVerified,
  AnyDMA, ResetAttempted, MemoryAttempted, MasterAttempted, TargetsAttempted,
  CommandBefore, CommandEnabled, CommandAfter, InitialMask, FinalMask,
  InitialReads, FinalReads, Hwcfg, ResetCount, ResetPolls, DrainPolls, DmaPolls,
  SynchronizeCount, CanaryMatched, ImemSubmitted, ImemCompleted,
  DmemSubmitted, DmemCompleted, DmemReads, DmemMatched, MismatchWord, MismatchValue,
  DeviceStatusBefore, DeviceStatusAfter, ProviderOpen, Unsafe, SessionState,
  CurrentCommand, SnapshotCount, Generation = 76
};
inline void encode(const SEC2Stage::Result *r, bool providerOpen, bool unsafe,
                   bool completed, unsigned state, unsigned command, U64 generation, U64 *out) {
  for (unsigned i = 0; i < Words; ++i) out[i] = 0;
  out[MagicWord] = Magic; out[Abi] = 1; out[Attempted] = r != nullptr;
  out[Completed] = completed; out[Generation] = generation;
  out[ProviderOpen] = providerOpen; out[Unsafe] = unsafe;
  out[SessionState] = state; out[CurrentCommand] = command;
  out[SnapshotCount] = SEC2Stage::SnapshotCount;
  if (!r) return;
  out[Passed]=r->passed; out[ReleaseSafe]=r->releaseSafe; out[HasRiscv]=r->hasRiscv;
  out[ImageMatched]=r->imageMatched; out[EnvironmentMatched]=r->environmentMatched;
  out[CpuIntact]=r->cpuIntact; out[Staged]=r->staged; out[Quiescent]=r->quiescent;
  out[TargetsCleared]=r->targetsCleared; out[FinalVerified]=r->finalVerified;
  out[AnyDMA]=r->anyDMA; out[ResetAttempted]=r->resetAttempted;
  out[MemoryAttempted]=r->memoryAttempted; out[MasterAttempted]=r->masterAttempted;
  out[TargetsAttempted]=r->targetsAttempted;
  out[CommandBefore]=r->commandBefore; out[CommandEnabled]=r->commandEnabled;
  out[CommandAfter]=r->commandAfter; out[InitialMask]=r->initialMask; out[FinalMask]=r->finalMask;
  out[InitialReads]=r->initialReads; out[FinalReads]=r->finalReads; out[Hwcfg]=r->hwcfg;
  out[ResetCount]=r->resetCount; out[ResetPolls]=r->resetPolls; out[DrainPolls]=r->drainPolls;
  out[DmaPolls]=r->dmaPolls; out[SynchronizeCount]=r->synchronizeCount;
  out[CanaryMatched]=r->canaryMatched; out[ImemSubmitted]=r->imemSubmitted;
  out[ImemCompleted]=r->imemCompleted; out[DmemSubmitted]=r->dmemSubmitted;
  out[DmemCompleted]=r->dmemCompleted; out[DmemReads]=r->dmemReads;
  out[DmemMatched]=r->dmemMatched; out[MismatchWord]=r->mismatchWord; out[MismatchValue]=r->mismatchValue;
  out[DeviceStatusBefore]=r->deviceStatusBefore; out[DeviceStatusAfter]=r->deviceStatusAfter;
  for (unsigned i=0;i<SEC2Stage::SnapshotCount;++i) {
    out[InitialBase+i]=r->initial[i]; out[FinalBase+i]=r->final[i];
  }
}
}
