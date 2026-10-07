#pragma once
#include "GSPLaunchOwnership.hpp"

// Shared native lifetime gate. Upload state never authorizes resource release.
// The service serializes calls and keeps the provider open across the lifetime.
namespace GSPExecutionOwner {
using namespace GSPLaunchOwnership;
enum class Phase : U32 { Empty, Uploading, Sealing, Sealed, Exposed, Started, Failed, Retained, Released,
  FwsecPreparing, FwsecStarted, FwsecDone, Sec2Staging, Sec2Staged, BootSetup, Sec2Started, BootReturned, FirstStatus, RuntimeReady };
class Owner {
  Ledger ledger_;
  Phase phase_=Phase::Empty;
  bool sealed_=false;
  bool execution_=false;
  U32 startMask_=0;
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
  bool executionAttempted() const{return execution_;}
  U32 startMask() const{return startMask_;}
  bool beginFwsec(U64 gen){
    if(!sealed() || gen!=ledger_.generation())return false;
    execution_=true;
    if(!ledger_.noteDeviceExposureAttempted(gen)){phase_=Phase::Retained;return false;}
    phase_=Phase::FwsecPreparing;return true;
  }
  bool startFwsec(U64 gen){
    const bool ready=phase_==Phase::FwsecPreparing && gen==ledger_.generation() && !startMask_;
    const bool allowed=ledger_.noteStartAttempted(gen);startMask_|=1;
    phase_=ready && allowed?Phase::FwsecStarted:Phase::Retained;return ready && allowed;
  }
  bool fwsecComplete(U64 gen,bool freshExecutionAndCleanup){
    if(phase_!=Phase::FwsecStarted || gen!=ledger_.generation() || !freshExecutionAndCleanup){fail();return false;}
    phase_=Phase::FwsecDone;return true;
  }
  bool beginSec2(U64 gen){
    if(phase_!=Phase::FwsecDone || gen!=ledger_.generation() || startMask_!=1)return false;
    phase_=Phase::Sec2Staging;return true;
  }
  bool sec2Complete(U64 gen,bool freshStage){
    if(phase_!=Phase::Sec2Staging || gen!=ledger_.generation() || !freshStage){fail();return false;}
    phase_=Phase::Sec2Staged;return true;
  }
  bool beginBoot(U64 gen){
    if(phase_!=Phase::Sec2Staged || gen!=ledger_.generation())return false;
    phase_=Phase::BootSetup;return true;
  }
  bool startSec2(U64 gen){
    // The irreversible lifetime START latch was set by FWSEC. This second
    // engine receives its own one-shot authorization only after fresh staging.
    const bool ready=phase_==Phase::BootSetup && gen==ledger_.generation() &&
      startMask_==1 && ledger_.startAttempted() && ledger_.owned();
    startMask_|=2;phase_=ready?Phase::Sec2Started:Phase::Retained;return ready;
  }
  bool bootReturned(U64 gen,bool hardwareOutcome){
    if(phase_!=Phase::Sec2Started || gen!=ledger_.generation() || !hardwareOutcome){fail();return false;}
    phase_=Phase::BootReturned;return true;
  }
  bool firstStatus(U64 gen){
    if(phase_!=Phase::BootReturned || gen!=ledger_.generation())return false;
    phase_=Phase::FirstStatus;return true;
  }
  // Only the native application adapter calls this after bootstrap and actual
  // window restoration. Existing startup readiness rules remain unchanged.
  bool beginRuntime(U64 gen){
    if(phase_!=Phase::BootReturned||gen!=ledger_.generation()||!ledger_.owned()||!ledger_.requiresPin()||startMask_!=3)return false;
    phase_=Phase::RuntimeReady;return true;
  }
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
