#pragma once
#include "FalconProtocol.hpp"
#include "FWSECBootProtocol.hpp"

// Host staging-buffer teardown only. FWSEC FRTS writes and WPR initialization
// are persistent device state: resetting Falcon does not roll them back.
// Nouveau v6.15 gsp/fwsec.c destroys its DMA image after synchronous FWSEC;
// this experiment additionally resets, drains and verifies before host release.
// A started but unverified firmware run conservatively retains all host memory.
namespace FWSECHostCleanup {
template<class IO> void finish(IO &io, FalconDMA::Result &r, bool stageGood,
                              bool anyDMA, const rtxfwsecboot::Result &boot) {
  using namespace FalconDMA;
  const bool started = boot.startAttempted;
  const bool uncertainFirmware = started && !boot.passed;
  const bool needsQuiescence = anyDMA || r.targetsAttempted || started;
  bool finalValid = false;
  // Do not let earlier or caller-supplied cleanup flags authorize a release.
  r.quiescent = r.targetsCleared = r.cleanupVerified = r.resourcesRetained = r.passed = false;
  r.finalReads = 0; r.drainPolls = 0; r.deviceStatusAfter = 0xffff;
  for (unsigned i = 0; i < SnapshotCount; ++i) r.final[i] = 0;

  const unsigned commandBeforeCleanup = io.command();
  if ((r.resetAttempted || started) &&
      (commandBeforeCleanup == 2 || commandBeforeCleanup == 6)) {
    bool stopped = reset(io, r);
    if (stopped) stopped = waitBits(io, DMACMD, 3, 2, 200, 100, r.drainPolls);
    if (stopped) {
      for (unsigned i = 0; i < 200; ++i) {
        r.deviceStatusAfter = io.deviceStatus(); ++r.drainPolls;
        if (r.deviceStatusAfter == 0xffff) break;
        if (!(r.deviceStatusAfter & 0x20)) { r.quiescent = true; break; }
        io.delayUs(100);
      }
    }
    if (r.quiescent) {
      const unsigned ctl = io.read(FBIFCTL);
      r.targetsCleared = readable(ctl) && writeCheck(io, DMACTL, 1, 1) &&
        writeCheck(io, FBIFCTL, ctl & ~0x80U) && writeCheck(io, DMABASE, 0) &&
        writeCheck(io, DMABASE1, 0) && writeCheck(io, DMAOFFSET, 0) && writeCheck(io, FBOFFSET, 0);
    }
    const unsigned snapshotCommand = io.command();
    if (snapshotCommand == 2 || snapshotCommand == 6) {
      finalValid = snapshot(io, r.final, r.finalReads);
      if (finalValid) finalValid = !(r.final[Engine] & 1) && !(r.final[HWCFG2] & 0x1000) &&
        !(r.final[CPUCTL] & 2) && !(r.final[RISCVCPU] & 0x80) && (r.final[DMACMD] & 3) == 2 &&
        (r.final[DMACTL] & 1) && !(r.final[FBIFCTL] & 0x80) && !r.final[DMABASE] &&
        !r.final[DMABASE1] && !r.final[DMAOFFSET] && !r.final[FBOFFSET];
      // After running firmware, explicitly establish a halted Falcon and a
      // valid Falcon selection in addition to the original DMA cleanup checks.
      if (finalValid && started) finalValid = (r.final[CPUCTL] & 0x12) == 0x10 &&
        (r.final[BCR] & 0x11) == 1;
    }
    if (!finalValid) { r.quiescent = false; r.targetsCleared = false; }
  }
  if (r.masterAttempted) io.setMaster(false);
  if (r.memoryAttempted) io.setMemory(false);
  r.commandAfter = io.command();
  io.unmapFalcon();

  bool hostSafe = r.commandAfter == 0 &&
    (!needsQuiescence || (r.quiescent && r.targetsCleared && finalValid));
  if (started) {
    // This check is intentionally fresh, after DMA quiescence and PCI disable.
    // An old successful staging comparison is not proof against firmware DMA.
    r.cpuIntact = false;
    if (!uncertainFirmware && hostSafe) r.cpuIntact = io.verifyFWSEC();
    // Guard the interval spent inspecting the CPU-side buffer too.
    r.commandAfter = io.command();
    hostSafe = hostSafe && r.commandAfter == 0 && r.cpuIntact;
  }
  r.resourcesRetained = uncertainFirmware || !hostSafe;
  const bool cleanupAttempted = !r.resourcesRetained;
  if (cleanupAttempted) {
    r.cleanupVerified = io.cleanup();
    // Native cleanup must retain its objects when complete/clear fails. Keep
    // the service/provider too so those live objects cannot become orphaned.
    if (!r.cleanupVerified) r.resourcesRetained = true;
  }
  if (uncertainFirmware) r.status = "fwsec-host-retained-for-uncertain-firmware";
  else if (cleanupAttempted && !r.cleanupVerified)
    r.status = "fwsec-host-cleanup-failed-resources-retained";
  else if (r.resourcesRetained && started && r.quiescent && r.targetsCleared &&
           finalValid && r.commandAfter == 0 && !r.cpuIntact)
    r.status = "fwsec-host-buffer-changed-resources-retained";
  else if (r.resourcesRetained) r.status = "fwsec-host-quiescence-failed-resources-retained";
  else if (started) r.status = "fwsec-host-cleanup-verified-vram-retained";

  // Device ownership and FRTS-region bookkeeping live in the caller's retained
  // IOService/transport. Never close it after even an uncertain START write.
  if (!started && !r.resourcesRetained) io.close();
  r.passed = stageGood && (!started || boot.passed) && r.cpuIntact && r.quiescent &&
    r.targetsCleared && r.cleanupVerified && !r.resourcesRetained &&
    r.commandBefore == 0 && r.commandAfter == 0 && finalValid && r.finalReads == SnapshotCount;
}
} // namespace FWSECHostCleanup
