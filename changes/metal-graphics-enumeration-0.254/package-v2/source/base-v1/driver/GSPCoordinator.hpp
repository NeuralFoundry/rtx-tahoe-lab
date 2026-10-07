#pragma once
#include "GSPLaunchOwnership.hpp"

// Shared native lifetime gate. Upload state never authorizes resource release.
// The service serializes calls and keeps the provider open across the lifetime.
namespace GSPCoordinator {
using namespace GSPLaunchOwnership;
enum class Phase : U32 { Empty, Uploading, Sealing, Sealed, Exposed, Started, Failed, Retained, Released };
class Owner {
  Ledger ledger_;
  Phase phase_=Phase::Empty;
  bool sealed_=false;
public:
  const Ledger &ledger() const { return ledger_; }
  Phase phase() const { return phase_; }
  bool claim(const Boot0::Facts &identity,const FWSECPreflight::Snapshot &board,
             const FWSECDisplay::Snapshot &display,U64 generation,bool exclusive) {
    if(phase_!=Phase::Empty || !ledger_.claim(identity,board,display,generation,exclusive))return false;
    phase_=Phase::Uploading;return true;
  }
  bool canUpload() const { return phase_==Phase::Uploading && ledger_.canMutateUploads(); }
  bool freeze(U64 generation) {
    if(!canUpload() || !ledger_.freezeUploads(generation))return false;
    phase_=Phase::Sealing;return true;
  }
  bool accept(const ContentSeal &seal) {
    if(phase_!=Phase::Sealing || !seal.completeFor(ledger_.generation()))return false;
    sealed_=true;phase_=Phase::Sealed;return true;
  }
  bool sealed() const { return sealed_ && phase_==Phase::Sealed; }
  // Call BEFORE any potentially effective device write. Failed late guards
  // retain the claim too. These methods do not implement a launch sequence.
  bool expose(U64 generation) {
    const bool ready=sealed();
    const bool allowed=ledger_.noteDeviceExposureAttempted(generation);
    phase_=ready && allowed?Phase::Exposed:Phase::Retained;
    return ready && allowed;
  }
  bool start(U64 generation) {
    const bool ready=sealed_ && (phase_==Phase::Sealed || phase_==Phase::Exposed);
    const bool allowed=ledger_.noteStartAttempted(generation);
    phase_=ready && allowed?Phase::Started:Phase::Retained;
    return ready && allowed;
  }
  void fail() { if(phase_!=Phase::Released)phase_=ledger_.requiresPin()?Phase::Retained:Phase::Failed; }
  // Every malformed IPC, Abort, Finish and client-death route uses this gate.
  // Never call complete/clear or close after an uncertain exposure/start.
  // Empty covers a partial native allocation before the full claim succeeded.
  template<class IO> bool cleanup(IO &io) {
    if(phase_==Phase::Released)return true;
    if(phase_==Phase::Retained || (ledger_.owned() && !ledger_.canReleaseHost()) || !io.baselineHeld()) {
      if(ledger_.owned())ledger_.noteCleanupFailure();
      phase_=Phase::Retained;io.pin();return false;
    }
    if(!io.releaseHost()) {
      ledger_.noteCleanupFailure();phase_=Phase::Retained;io.pin();return false;
    }
    if(ledger_.owned() && !ledger_.releaseBeforeStart(ledger_.generation(),true)) {
      phase_=Phase::Retained;io.pin();return false;
    }
    io.closeProvider();phase_=Phase::Released;return true;
  }
};
}
