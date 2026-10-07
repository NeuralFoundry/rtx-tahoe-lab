#pragma once
#include "FWSECStageProtocol.hpp"
#include "FWSECBootProtocol.hpp"
#include "FWSECHostCleanup.hpp"

// One owned transaction: fresh board/region -> image -> CPU -> host quiescence.
// A completed historical stage can never be supplied as a current boot gate.
namespace FWSECExecution {
struct Result {
  FWSECStage::Result stage;
  rtxfwsecboot::Result boot;
  bool stageVerified = false, anyDMA = false, regionRechecked = false;
  bool providerHeld = false, regionOwned = false, regionPersistent = false, passed = false;
};
template<class IO> struct BootBackend {
  IO &io;
  unsigned read(rtxfwsecboot::Reg reg) { return io.bootRead(reg); }
  bool write(rtxfwsecboot::Reg reg, unsigned value) { return io.bootWrite(reg, value); }
  void delayUs(unsigned us) { io.delayUs(us); }
};
template<class IO> void run(IO &io, Result &r) {
  const auto transaction = FWSECStage::stageOwned(io, r.stage);
  r.stageVerified = transaction.verified; r.anyDMA = transaction.anyDMA;
  if (!transaction.opened) return;
  if (transaction.verified) {
    r.regionRechecked = io.freshForBoot();
    if (r.regionRechecked) {
      rtxfwsecboot::Gate gate;
      gate.stageVerified = r.stage.imemCompleted == FWSECStage::ImemBlocks &&
        r.stage.dmemCompleted == FWSECStage::DmemBlocks && r.stage.dmemMatched == FWSECStage::DmemWords;
      const unsigned dma = io.read(FalconDMA::DMACMD);
      gate.ramDMAIdle = FalconDMA::readable(dma) && (dma & 3) == 2 && io.command() == 6;
      gate.hostBufferIntact = io.verifyFWSEC();
      gate.regionOwned = io.regionOwned() && io.providerHeld();
      gate.vramSize = rtxfwsecboot::VramBytes;
      gate.frtsOffset = rtxfwsecboot::FrtsOffset;
      gate.frtsSize = rtxfwsecboot::FrtsBytes;
      BootBackend<IO> backend{io};
      r.boot = rtxfwsecboot::run(backend, gate);
    } else {
      r.boot.status = "fwsec-boot-fresh-region-check-failed";
    }
  }
  FWSECHostCleanup::finish(io, r.stage.lifecycle, transaction.verified, transaction.anyDMA, r.boot);
  r.providerHeld = io.providerHeld(); r.regionOwned = io.regionOwned();
  r.regionPersistent = io.regionPersistent();
  r.passed = r.boot.passed && r.stage.lifecycle.passed && r.providerHeld && r.regionOwned && r.regionPersistent;
}
} // namespace FWSECExecution
