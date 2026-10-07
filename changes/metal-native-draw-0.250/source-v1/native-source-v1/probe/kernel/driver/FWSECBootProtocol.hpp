#pragma once
#include <stdint.h>

// RTXProbe 0.8 used this offline; 0.9 supplies the owned native transaction.
// Every caller must own the device and region, stage the exact signed board
// payload, keep all DMA resources pinned, and provide an independent quiescence
// and recovery path. This helper neither allocates/frees nor resets anything.
//
// Pinned primary references (paths relative to the named repository):
// torvalds/linux v6.15:
//   drivers/gpu/drm/nouveau/nvkm/falcon/{ga102,gm200}.c
//   drivers/gpu/drm/nouveau/nvkm/subdev/gsp/fwsec.c
// NVIDIA/open-gpu-kernel-modules 570.144:
//   src/nvidia/src/kernel/gpu/falcon/arch/turing/kernel_falcon_tu102.c
//   src/nvidia/src/kernel/gpu/gsp/arch/ampere/kernel_gsp_falcon_ga102.c
//   src/nvidia/src/kernel/gpu/gsp/arch/turing/kernel_gsp_frts_tu102.c
//   src/common/inc/swref/published/{ampere/ga102/dev_falcon_v4.h,
//       ampere/ga102/dev_falcon_second_pri.h,turing/tu102/dev_fb.h}
namespace rtxfwsecboot {
using U32 = uint32_t;
using U64 = uint64_t;
constexpr U64 VramBytes = 6ULL << 30;
constexpr U64 FrtsOffset = 0x17fe00000ULL;
constexpr U32 FrtsBytes = 1U << 20;
constexpr U32 BoardBoot0 = 0xb76000a1U;
constexpr U32 SignatureDmemOffset = 1444, EngineId = 0x400, UcodeId = 9;
constexpr U32 PollLimit = 20000, PollDelayUs = 100;

enum Reg : unsigned {
  PmcBoot0, Engine, Hwcfg2, CpuCtl, RiscvCpu, CoreSelect, DmaCmd,
  WprLo, WprHi, FrtsScratch, Mailbox0, Mailbox1,
  Rm, BromPara, BromEngine, BromUcode, BromAlgorithm, BootVector, CpuAlias,
  RegCount
};
// Global BAR0 offsets, not relative offsets. The write-only CPU alias must
// never be read for a readback check.
constexpr U32 Addresses[RegCount] = {
  0x000000, 0x1103c0, 0x1100f4, 0x110100, 0x111388, 0x111668, 0x110118,
  0x1fa824, 0x1fa828, 0x001438, 0x110040, 0x110044,
  0x110084, 0x111210, 0x11119c, 0x111198, 0x111180, 0x110104, 0x110130
};
constexpr unsigned InitialCount = 12;

struct Gate {
  // These are current ownership/staging assertions, never values inferred
  // from an old successful JSON report. regionOwned includes exclusion of
  // VGA workspace, MMU-locked VRAM and every other current allocation.
  bool stageVerified = false, ramDMAIdle = false, hostBufferIntact = false;
  bool regionOwned = false;
  U64 vramSize = 0, frtsOffset = 0;
  U32 frtsSize = 0;
};

struct Result {
  const char *status = "fwsec-boot-not-started";
  U32 initial[InitialCount] = {}, initialReads = 0;
  U32 cpuBeforeStart = 0, lastCpu = 0, haltPolls = 0, delayCalls = 0;
  U32 mailbox0 = 0, mailbox1 = 0, scratch = 0, wprLo = 0, wprHi = 0;
  U32 failedRegister = RegCount, writeAttempts = 0, verifiedWrites = 0;
  bool registersTouched = false, startAttempted = false, startWriteAccepted = false;
  bool aliasUsed = false, halted = false, runningObserved = false;
  bool sideEffectsMayRemain = false, requiresCallerQuiescence = false;
  bool outcomeRead = false, wprTransitionObserved = false, passed = false;
  // There is no reliable public BROM signature-error decoder here. Successful
  // FRTS execution is indirect authentication evidence, not a BROM status bit.
  bool authenticatedExecutionInferred = false;
};

inline bool readable(U32 value) {
  return value != 0xffffffffU && (value & 0xffff0000U) != 0xbadf0000U &&
         (value & 0xffff0000U) != 0xbad00000U;
}
inline U32 wprPage(U32 value) { return value >> 4; }
inline bool haltedCpu(U32 value) { return readable(value) && (value & 0x12U) == 0x10U; }

// Backend contract: read(Reg)->U32, write(Reg,U32)->bool, delayUs(U32).
// A failed write may already have reached hardware; bookkeeping precedes it.
template<class IO> Result run(IO &io, const Gate &gate) {
  Result r;
  if (!gate.stageVerified || !gate.ramDMAIdle || !gate.hostBufferIntact || !gate.regionOwned) {
    r.status = "fwsec-boot-gate-incomplete"; return r;
  }
  if (gate.vramSize != VramBytes || gate.frtsOffset != FrtsOffset || gate.frtsSize != FrtsBytes) {
    r.status = "fwsec-boot-region-mismatch"; return r;
  }
  for (unsigned i = 0; i < InitialCount; ++i) {
    r.initial[i] = io.read(static_cast<Reg>(i)); ++r.initialReads;
    if (!readable(r.initial[i])) {
      r.failedRegister = i; r.status = "fwsec-boot-preflight-unreadable"; return r;
    }
  }
  if (r.initial[PmcBoot0] != BoardBoot0) { r.status = "fwsec-boot-board-mismatch"; return r; }
  if ((r.initial[Engine] & 1) || (r.initial[Hwcfg2] & 0x1000) ||
      !haltedCpu(r.initial[CpuCtl]) || (r.initial[RiscvCpu] & 0x80) ||
      (r.initial[CoreSelect] & 0x11) != 1 || (r.initial[DmaCmd] & 3) != 2) {
    r.status = "fwsec-boot-engine-not-ready"; return r;
  }
  if (wprPage(r.initial[WprHi])) { r.status = "fwsec-boot-wpr-already-initialized"; return r; }

  const Reg configRegs[] = {Rm, Mailbox0, BromPara, BromEngine, BromUcode, BromAlgorithm, BootVector};
  const U32 configValues[] = {BoardBoot0, 0, SignatureDmemOffset, EngineId, UcodeId, 1, 0};
  for (unsigned i = 0; i < sizeof(configRegs) / sizeof(configRegs[0]); ++i) {
    const Reg reg = configRegs[i];
    r.registersTouched = true; r.requiresCallerQuiescence = true; ++r.writeAttempts;
    if (!io.write(reg, configValues[i])) {
      r.failedRegister = reg; r.status = "fwsec-boot-config-write-failed"; return r;
    }
    if (io.read(reg) != configValues[i]) {
      r.failedRegister = reg; r.status = "fwsec-boot-config-readback-failed"; return r;
    }
    ++r.verifiedWrites;
  }
  // Recheck every safety input that could change while programming BROM.
  r.cpuBeforeStart = io.read(CpuCtl);
  const U32 riscv = io.read(RiscvCpu), core = io.read(CoreSelect), dma = io.read(DmaCmd);
  const U32 engine = io.read(Engine), scrub = io.read(Hwcfg2), wpr = io.read(WprHi);
  if (!haltedCpu(r.cpuBeforeStart) || !readable(riscv) || (riscv & 0x80) ||
      !readable(core) || (core & 0x11) != 1 || !readable(dma) || (dma & 3) != 2 ||
      !readable(engine) || (engine & 1) || !readable(scrub) || (scrub & 0x1000) ||
      !readable(wpr) || wprPage(wpr)) {
    r.status = "fwsec-boot-prestart-state-changed"; return r;
  }
  r.aliasUsed = (r.cpuBeforeStart & 0x40) != 0;
  r.startAttempted = true; r.sideEffectsMayRemain = true; ++r.writeAttempts;
  if (!io.write(r.aliasUsed ? CpuAlias : CpuCtl, 2)) {
    r.failedRegister = r.aliasUsed ? CpuAlias : CpuCtl;
    r.status = "fwsec-boot-start-write-failed"; return r;
  }
  r.startWriteAccepted = true;
  // Exactly 20,000 delayed observations: maximum requested delay is 2 seconds.
  // Real elapsed wall time also includes MMIO/backend overhead.
  for (U32 i = 0; i < PollLimit; ++i) {
    io.delayUs(PollDelayUs); ++r.delayCalls;
    r.lastCpu = io.read(CpuCtl); ++r.haltPolls;
    if (!readable(r.lastCpu)) { r.status = "fwsec-boot-cpu-unreadable"; return r; }
    if (r.lastCpu & 0x10) { r.halted = true; break; }
    r.runningObserved = true;
  }
  // Gather outcome even on timeout. A timeout can leave FRTS/WPR partly set;
  // the caller must retain its ownership and recover separately.
  r.mailbox0 = io.read(Mailbox0); r.mailbox1 = io.read(Mailbox1);
  r.scratch = io.read(FrtsScratch); r.wprLo = io.read(WprLo); r.wprHi = io.read(WprHi);
  r.outcomeRead = true;
  if (!r.halted) { r.status = "fwsec-boot-halt-timeout"; return r; }
  if (!readable(r.mailbox0) || !readable(r.mailbox1) || !readable(r.scratch) ||
      !readable(r.wprLo) || !readable(r.wprHi)) {
    r.status = "fwsec-boot-outcome-unreadable"; return r;
  }
  if (r.mailbox0) { r.status = "fwsec-boot-mailbox-error"; return r; }
  if (r.scratch >> 16) { r.status = "fwsec-boot-frts-error"; return r; }
  // NVIDIA checks VAL (31:4), representing 4KiB pages. The low nibble is not
  // address data. The exact upper endpoint is deliberately not invented.
  r.wprTransitionObserved = wprPage(r.wprHi) != 0;
  if (!r.wprTransitionObserved || wprPage(r.wprLo) != U32(gate.frtsOffset >> 12)) {
    r.status = "fwsec-boot-wpr-outcome-mismatch"; return r;
  }
  r.passed = true; r.authenticatedExecutionInferred = true;
  r.status = "fwsec-frts-execution-verified";
  // Do not clear sideEffectsMayRemain/requiresCallerQuiescence: engine reset
  // cannot undo FRTS writes, pre-OS firmware changes or WPR initialization.
  return r;
}
} // namespace rtxfwsecboot
