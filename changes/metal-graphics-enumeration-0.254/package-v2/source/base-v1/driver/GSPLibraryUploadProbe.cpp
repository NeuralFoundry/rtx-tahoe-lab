// RTXProbe 0.36: caller-uploaded immutable shader library before GPU bootstrap.
// All mappings and provider ownership are retained after exposure until reboot.
#include <IOKit/IOUserClient.h>
#include <IOKit/IOLocks.h>
#include <kern/clock.h>
#include "MacGSPIdentity.hpp"
#include "MacGSPRuntimeDma.hpp"
#include "RuntimeDmaProtocol.hpp"
#include "MacGSPContext.hpp"
#include "GSPExecutionOwner.hpp"
#include "MacGSP15FWSEC.hpp"
#include "MacSEC2FirstBootStage.hpp"
#include "MacGSPFirstBoot151.hpp"
#include "GSPFirstStatus.hpp"
#include "GSPEventProtocol.hpp"
#include "GSPInitEventProtocol.hpp"
#include "GSPQueueConsume.hpp"
#include "MacGSPSequencer.hpp"
#include "MacGSPComputePrep.hpp"
#include "MacGSPBar1.hpp"
#include "MacGSPPageTables.hpp"
#include "../changes/gsp-channel-0.23/memory/MacChannelMemory.hpp"
#include "../changes/gsp-channel-0.23/native/ChannelABI.hpp"
#include "../changes/gsp-submit-0.24/fence/MacHostFence.hpp"
#include "../changes/gsp-submit-0.24/native/ExecutionABI.hpp"
#include "../MacReusableRuntime.hpp"
#include "../ReusableABI.hpp"
#include "../changes/gsp-program-library-0.33/entry/ProgramABI.hpp"
#include "../LibraryUploadABI.hpp"

namespace P = RuntimeDmaProtocol;
class RTXGSPLaunchUserClient;
class RTXProbe : public IOService {
  OSDeclareDefaultStructors(RTXProbe)
  IOPCIDevice *pci = nullptr;
  IOLock *mutex = nullptr;
  MacGSPRuntimeDma *dma = nullptr;
  MacGSPContext *context = nullptr;
  GSPExecutionOwner::Owner coordinator;
  GSPContentSeal::Result sealResult;
  GSPContentSeal::Result postFwsecSeal;
  MacGSP15Boot0 *rom=nullptr;
  Preparation::RomResult romResult;
  MacGSP15FWSEC *fwsec=nullptr;
  FWSECExecution::Result *fwsecResult=nullptr;
  MacSEC2FirstBootStage *sec2=nullptr;
  SEC2FirstBootStage::Result *sec2Result=nullptr;
  GSPFirstBoot151::Result bootResult;
  GSPFirstStatus::Result firstStatus;
  GSPEvents::Result events;
  GSPInitEvents::Result afterEvents;
  GSPSequencer::Result sequence;
  GSPQueueConsume::Result queueConsume;
  GSPComputePrep::Result rm;
  GSPBar1::Result bar1;
  MacGSPBar1Mapping*bar1Map=nullptr;
  bool bar1Claimed=false,bar1LeaseOwned=false;
  unsigned char *bar1Original=nullptr,*bar1Capture=nullptr;
  GSPPageTables::Result pageTables;
  GSPPageTablesRM::Result pdRm;
  GSPPageTablesSnapshot::Result pagePost;
  MacGSPPageTablesMapping*pageMap=nullptr;
  bool pageClaimed=false,pageLeaseOwned=false,pdRmClaimed=false;
  unsigned char *pageCapture=nullptr,*pdRecords=nullptr,*pdRequest=nullptr;
  MacChannelMemoryMapping *channelMap=nullptr;
  MacChannelMemoryState *channelState=nullptr;
  ChannelTransactions::Result channel;
  ChannelSnapshot::Result channelSnapshot;
  unsigned char *channelRecords=nullptr,*channelRequests=nullptr,*channelRootCapture=nullptr,*channelChildCapture=nullptr;
  MacExecutionMemoryState *executionState=nullptr;
  MacExecutionQueueState executionQueue;
  MacHostFenceState fenceState;
  ExecutionTransactions::Result execution;
  HostFence::Result hostFence;
  ChannelSnapshot::Result executionSnapshot;
  ExecutionCapture::Result executionCapture;
  unsigned char *executionRecords=nullptr,*executionRequests=nullptr,*executionRootCapture=nullptr,*executionChildCapture=nullptr,*executionDeviceCapture=nullptr;
  unsigned char *externalRecords=nullptr,*externalRequests=nullptr;
  MacProgramMemoryState *computeState=nullptr;
  RtxReusableRuntime035::State *runtimeState=nullptr;
  RtxReusableABI035::Snapshot runtimeSnapshot;
  RtxLibraryUpload036::State shaderLibrary;
  ProgramCapture::Result computeCapture;
  unsigned char *computeRootCapture=nullptr,*computeChildCapture=nullptr,*computeDeviceCapture=nullptr;
  bool rmClaimed=false;
  unsigned char *rmRecords=nullptr,*rmRequests=nullptr;
  bool sequenceWorkspaceOwned=false,consumeClaimed=false,sequenceClaimed=false;
  bool sequenceGspStartNoted=false,sequenceSec2StartNoted=false;
  unsigned char *afterRecords=nullptr;
  unsigned char *firstRecord=nullptr;
  const char *launchStatus="not-requested";
  bool borrowedOwnerVerified=false, postFwsecVerified=false;
  bool startupPrepared = false, nativePagesValid = false, directPages = false;
  unsigned char *sealScratch = nullptr, *sealExpected = nullptr;
  P::U64 *pageScratch = nullptr;
  RTXGSPLaunchUserClient *activeClient = nullptr; // User client retains this service.
  P::Session session;
  GSPBooterPreflight::Snapshot fuse;
  bool preflightPassed = false, consumed = false, providerOpen = false, pinned = false, stopping = false;
  IOReturn lastNativeError = kIOReturnSuccess;
  bool cleanupNative(); // Called only with mutex held, in client context.
  void publishState();
  IOReturn failRequest(P::Error error, IOReturn nativeError = kIOReturnBadArgument);
  IOReturn methodLocked(UInt32 selector, IOExternalMethodArguments *args);
  void fillInfo(P::U64 *output);
  IOReturn launch();
  void launchInfo(P::U64 *out);
  ChannelABI::Owner channelOwner();
  ChannelABI::Owner executionOwner();
  ChannelABI::Owner computeOwner();
  bool computeAfterHost(MacExecutionMemory &memory);
  IOReturn computeMethod(UInt32 selector,IOExternalMethodArguments *a);
  IOReturn runtimeMethod(UInt32 selector,IOExternalMethodArguments *a);
  P::U64 runtimeCaller()const{return static_cast<P::U64>(reinterpret_cast<uintptr_t>(activeClient));}
  RtxLibraryUpload036::Scope libraryScope(){
    return {getRegistryEntryID(),runtimeCaller(),activeClient&&!stopping&&!isInactive()&&pci&&!pci->isInactive()&&
      session.state==P::State::Idle&&!providerOpen&&!pinned&&coordinator.phase()==GSPExecutionOwner::Phase::Empty};
  }
  IOReturn libraryMethod(UInt32 selector,IOExternalMethodArguments *a);
  void retireRuntime(){if(runtimeState){runtimeState->closed=true;runtimeState->core.ownershipLost();runtimeState->backing.retain();}}
public:
  bool baselineHeld() const { return pci && pci->configRead16(4)==0 &&
    (!providerOpen || pci->isOpen(this)); }
  void pin() { if(!pinned){retain();pinned=true;} }
  bool releaseHost() { return !dma || dma->cleanup(); }
  void closeProvider() { if(context)context->unmap(); if(providerOpen){pci->close(this);providerOpen=false;} }
  bool sealInputsValid(P::U64 generation);
  bool readSeal(P::U32 resource,P::U64 offset,unsigned char *out,P::U32 bytes) {
    return dma && dma->copyOut(resource,offset,out,bytes);
  }
  bool start(IOService *provider) override;
  void stop(IOService *provider) override;
  void free() override;
  bool willTerminate(IOService *provider, IOOptionBits options) override;
  bool didTerminate(IOService *provider, IOOptionBits options, bool *defer) override;
  bool finalize(IOOptionBits options) override;
  IOReturn newUserClient(task_t task, void *securityID, UInt32 type,
    OSDictionary *properties, IOUserClient **handler) override;
  IOReturn dispatch(RTXGSPLaunchUserClient *client, UInt32 selector, IOExternalMethodArguments *args);
  void disconnect(RTXGSPLaunchUserClient *client, bool allowCleanup = true);
};

class RTXGSPLaunchUserClient : public IOUserClient {
  OSDeclareDefaultStructors(RTXGSPLaunchUserClient)
  RTXProbe *owner = nullptr;
public:
  bool start(IOService *provider) override {
    auto *service = OSDynamicCast(RTXProbe, provider);
    if (!service || !IOUserClient::start(provider)) return false;
    owner = service;
    owner->retain(); // Remains stable across concurrent close/externalMethod.
    return true;
  }
  IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
    IOExternalMethodDispatch *dispatch = nullptr, OSObject *target = nullptr,
    void *reference = nullptr) override {
    return owner ? owner->dispatch(this, selector, args) : kIOReturnNotAttached;
  }
  IOReturn clientClose() override {
    if (owner) owner->disconnect(this);
    terminate(); // Outside the service mutex; stop() can call disconnect again.
    return kIOReturnSuccess;
  }
  IOReturn clientDied() override { return clientClose(); }
  void stop(IOService *provider) override {
    // Unexpected framework stop may inherit a workloop. Never complete DMA
    // mappings here; clientClose/clientDied perform ordinary teardown.
    if (owner) owner->disconnect(this, false);
    IOUserClient::stop(provider);
  }
  void free() override {
    if (owner) { owner->release(); owner = nullptr; }
    IOUserClient::free();
  }
};
OSDefineMetaClassAndStructors(RTXProbe, IOService)
OSDefineMetaClassAndStructors(RTXGSPLaunchUserClient, IOUserClient)

bool RTXProbe::start(IOService *provider) {
  auto *device = OSDynamicCast(IOPCIDevice, provider);
  if (!device || !IOService::start(provider)) return false;
  if (device->configRead32(0) != 0x252010de || device->configRead32(0x2c) != 0x104c1043 ||
      device->getBusNumber() != 1 || device->getDeviceNumber() != 0 || device->getFunctionNumber() != 0) {
    IOService::stop(provider); return false;
  }
  mutex = IOLockAlloc();
  if (!mutex) { IOService::stop(provider); return false; }
  pci = device; pci->retain();
  setProperty("ProbeVersion", "0.36.0");
  setProperty("Mode", "gsp-program-runtime");
  setProperty("ProbeComplete", false);
  setProperty("MMIOReadOnly", true);
  setProperty("FirmwareExecuted", false);
  setProperty("DMATransferExecuted", false);
  setProperty("ResetExecuted", false);
  setProperty("BusMasterEnabled", false);
  MacGSPIdentity io(pci, this);
  const Boot0::Result result = Boot0::run(io, fuse);
  const Boot0::Facts &facts = io.capturedFacts;
  preflightPassed = result.passed && result.restoreVerified && result.commandAfter == 0 &&
    result.reads == 2 && result.first == 0xb76000a1U && result.second == result.first &&
    fuse.reads == 2 && fuse.first == 1 && fuse.second == 1 && fuse.index == 0;
  setProperty("TargetIdentity", facts.identity, 32);
  setProperty("TargetSubsystem", facts.subsystem, 32);
  setProperty("TargetBDF", facts.targetBDF);
  setProperty("PMCSR", facts.pmcsr, 32);
  setProperty("LinkStatus", facts.link, 32);
  setProperty("BAR0", facts.bar0, 64);
  setProperty("BAR1", facts.bar1, 64);
  setProperty("BAR0Descriptor", facts.descriptor0, 64);
  setProperty("BAR1Descriptor", facts.descriptor1, 64);
  setProperty("BAR0Length", facts.length0, 64);
  setProperty("BAR1Length", facts.length1, 64);
  setProperty("BARTypesValid", facts.barTypesValid);
  setProperty("Boot0Status", result.status);
  setProperty("Boot0First", result.first, 32);
  setProperty("Boot0Second", result.second, 32);
  setProperty("Boot0Reads", result.reads, 32);
  setProperty("CommandBefore", result.commandBefore, 32);
  setProperty("CommandDuring", result.commandDuring, 32);
  setProperty("CommandAfter", result.commandAfter, 32);
  setProperty("MemoryEnableAttempted", result.enableAttempted);
  setProperty("RestoreVerified", result.restoreVerified);
  setProperty("BooterFuseOffset", 0x824148U, 32);
  setProperty("BooterFuseFirst", fuse.first, 32);
  setProperty("BooterFuseSecond", fuse.second, 32);
  setProperty("BooterFuseReads", fuse.reads, 32);
  setProperty("BooterFuseStatus", fuse.status);
  setProperty("BooterSignatureIndex", UInt32(fuse.index), 32);
  setProperty("ProbePassed", preflightPassed);
  setProperty("ProbeComplete", true); // Identity measurement only; DMA completion is separate.
  publishState();
  registerService();
  return true;
}

void RTXProbe::publishState() {
  setProperty("GSPShaderUploadPhase",unsigned(shaderLibrary.phase()),32);
  setProperty("GSPShaderUploadBytes",shaderLibrary.written(),32);
  setProperty("GSPShaderUploadError",unsigned(shaderLibrary.terminalError()),32);
  setProperty("GSPChannelRingPassed",channelState&&channelState->ring.passed);
  setProperty("GSPChannelContextPassed",channelState&&channelState->contexts.passed);
  setProperty("GSPChannelRMPassed",channel.rpc.passed);
  setProperty("GSPChannelRMCompleted",channel.rpc.completed,32);
  setProperty("GSPChannelRMStatus",channel.rpc.status);
  setProperty("GSPChannelSnapshotPassed",channelSnapshot.passed);
  setProperty("GSPExecutionRMPassed",execution.rpc.passed);
  setProperty("GSPHostFencePassed",hostFence.passed);
  setProperty("GSPExecutionSnapshotPassed",executionSnapshot.passed);
  setProperty("GSPExecutionCapturePassed",executionCapture.passed);
  setProperty("GSPComputeMemoryPassed",computeState&&computeState->result.memory.passed);
  setProperty("GSPComputeSubmitPassed",runtimeState&&!runtimeState->closed&&runtimeState->core.result().passed&&runtimeState->core.completed()==runtimeState->backing.completed());
  setProperty("GSPProgramReady",runtimeState&&!runtimeState->closed&&runtimeState->core.phase()==RtxReusable035::Phase::Ready);
  setProperty("GSPProgramCompleted",runtimeState?runtimeState->core.completed():0,64);
  setProperty("GSPComputeCapturePassed",computeCapture.passed);
  setProperty("RTXComputeVerified",false);
  setProperty("RTXMetalVerified",false);
  setProperty("GSPChannelRootCapturedBytes",channelSnapshot.rootBytes,32);
  setProperty("GSPChannelChildrenCapturedBytes",channelSnapshot.childBytes,32);
  setProperty("GSPChannelWindowRestored",channelState&&channelState->ring.windowRestored);
  setProperty("GSPChannelComputeExecuted",false);
  setProperty("GSPChannelMetalVerified",false);
  setProperty("GSPSequenceStatus",sequence.status);
  setProperty("GSPSequenceValidated",sequence.validated);
  setProperty("GSPSequenceAttempted",sequence.attempted);
  setProperty("GSPSequencePassed",sequence.passed);
  setProperty("GSPSequenceFailure",unsigned(sequence.failure),32);
  setProperty("GSPSequenceCompleted",sequence.completed,32);
  setProperty("GSPSequenceLastAddress",sequence.lastAddress,32);
  setProperty("GSPSequenceLastValue",sequence.lastValue,32);
  setProperty("GSPSequenceResumed",sequence.resumed);
  setProperty("GSPSequenceWorkspaceOwned",sequenceWorkspaceOwned);
  setProperty("GSPSequenceWorkspaceStart",GSPSequencer::WorkspaceStart,64);
  setProperty("GSPSequenceWorkspaceEnd",GSPSequencer::WorkspaceEnd,64);
  setProperty("GSPQueueConsumerAttempted",queueConsume.attempted);
  setProperty("GSPQueueConsumerVerified",queueConsume.verified);
  setProperty("GSPQueueConsumerAfter",queueConsume.readerAfter,32);
  setProperty("GSPAfterSequenceStatus",afterEvents.status);
  setProperty("GSPAfterSequenceCount",afterEvents.count,32);
  setProperty("GSPAfterSequencePassed",afterEvents.passed);
  setProperty("GSPAfterSequenceInitDone",afterEvents.initDone);
  setProperty("GSPPageTablesStatus",pageTables.status);
  setProperty("GSPPageTablesStaged",pageTables.passed);
  setProperty("GSPPageTablesAttempted",pageTables.attempted);
  setProperty("GSPPageTablesFailure",unsigned(pageTables.failure),32);
  setProperty("GSPPageTablesWindowRestored",pageTables.windowRestored);
  setProperty("GSPPageTablesCheckedWords",pageTables.checked,32);
  setProperty("GSPPageTablesPostCaptured",pagePost.passed);
  setProperty("GSPPageTablesPostBytes",pagePost.bytes,32);
  setProperty("GSPPageTablesRMStatus",pdRm.status);
  setProperty("GSPPageTablesRMPassed",pdRm.passed);
  setProperty("GSPPageTablesRMAttempted",pdRm.attempted);
  setProperty("GSPPageTablesRMFailure",unsigned(pdRm.failure),32);
  setProperty("GSPBar1Status",bar1.status);
  setProperty("GSPBar1Passed",bar1.passed);
  setProperty("GSPBar1Attempted",bar1.attempted);
  setProperty("GSPBar1Failure",unsigned(bar1.failure),32);
  setProperty("GSPBar1LeaseOwned",bar1LeaseOwned);
  setProperty("GSPBar1CheckedWords",bar1.checkedWords,32);
  setProperty("GSPBar1OriginalRestored",bar1.originalRestored);
  setProperty("GSPBar1WindowRestored",bar1.windowRestored);
  setProperty("GSPComputePrepStatus",rm.status);
  setProperty("GSPComputePrepValidated",rm.validated);
  setProperty("GSPComputePrepAttempted",rm.attempted);
  setProperty("GSPComputePrepPassed",rm.passed);
  setProperty("GSPComputePrepFailure",unsigned(rm.failure),32);
  setProperty("GSPComputePrepCompleted",rm.completed,32);
  setProperty("GSPComputePrepRequestsSent",rm.sent,32);
  setProperty("GSPComputePrepDoorbells",rm.doorbells,32);
  setProperty("GSPComputePrepRecordCount",rm.count,32);
  setProperty("GSPComputePrepReader",rm.rxReader,32);
  setProperty("GSPComputePrepLastFunction",rm.lastFunction,32);
  setProperty("GSPComputePrepLastResult",rm.lastResult,32);
  setProperty("GSPComputePrepLastParamStatus",rm.lastParamStatus,32);
  setProperty("GSPComputePrepElapsedNs",rm.elapsedNs,64);
  setProperty("GSPInitDoneObserved",events.initDone || afterEvents.initDone);
  setProperty("GSPEventStatus",events.status);
  setProperty("GSPEventStop",unsigned(events.stop),32);
  setProperty("GSPEventFailure",unsigned(events.failure),32);
  setProperty("GSPEventCapturePassed",events.passed);
  setProperty("GSPEventCount",events.count,32);
  setProperty("GSPEventBytes",events.bytes,32);
  setProperty("GSPEventElapsedNs",events.elapsedNs,64);
  setProperty("GSPEventInitDone",events.initDone);
  setProperty("GSPEventSequencerRequired",events.sequencer);
  setProperty("GSPEventNocatCount",events.nocatCount,32);
  setProperty("GSPEventReadPtr",events.reader,32);
  setProperty("GSPEventQueueConsumed",queueConsume.written);
  setProperty("GSPLaunchStatus",launchStatus);
  setProperty("GSPExecutionAttempted",coordinator.executionAttempted());
  setProperty("GSPFirmwareStartMask",coordinator.startMask(),32);
  setProperty("GSPBorrowedOwnerVerified",borrowedOwnerVerified);
  setProperty("GSPPostFwsecSealPassed",postFwsecVerified);
  setProperty("GSPFirstBootStatus",bootResult.status);
  setProperty("GSPFirstBootPassed",bootResult.passed);
  setProperty("GSPFirstStatus",firstStatus.status);
  setProperty("GSPFirstRecordCaptured",firstStatus.captured);
  setProperty("GSPFirstRecordFunction",firstStatus.function,32);
  setProperty("GSPFirstRecordBytes",firstStatus.recordBytes,32);
  setProperty("GSPFirstRecordInitDone",firstStatus.initDone);
  setProperty("FirmwareStartAttempted",bool(coordinator.startMask()));
  setProperty("FirmwareExecuted",fwsecResult && fwsecResult->boot.passed);
  setProperty("MMIOReadOnly",!coordinator.executionAttempted());
  setProperty("DMATransferExecuted",(fwsecResult && fwsecResult->anyDMA) || (sec2Result && sec2Result->anyDMA));
  setProperty("ResetExecuted",(fwsecResult && fwsecResult->stage.lifecycle.resetAttempted) || bootResult.gspResetAttempted);
  setProperty("BusMasterEnabled",bool(pci->configRead16(4)&4));
  if(fwsecResult){setProperty("GSPFWSECStatus",fwsecResult->boot.status);setProperty("GSPFWSECPassed",fwsecResult->passed);}
  if(sec2Result){setProperty("GSPSEC2StageStatus",sec2Result->status);setProperty("GSPSEC2Staged",sec2Result->staged);}
  setProperty("GSPOwnerPhase", UInt32(coordinator.phase()), 32);
  setProperty("GSPRegionOwned", coordinator.ledger().owned());
  setProperty("GSPUploadsFrozen", coordinator.ledger().uploadsFrozen());
  setProperty("GSPStartAttempted", coordinator.ledger().startAttempted());
  setProperty("GSPDeviceExposed", coordinator.ledger().exposed());
  setProperty("GSPNativeSealStatus", sealResult.status);
  setProperty("GSPNativeSealPassed", sealResult.seal.completeFor(getRegistryEntryID()));
  setProperty("GSPNativeSealBytes", sealResult.checkedBytes, 64);
  setProperty("GSPDirectPages", directPages);
  setProperty("GSPDmaSessionState", UInt32(session.state), 32);
  setProperty("GSPDmaProtocolError", P::errorName(session.lastError()));
  setProperty("GSPDmaLastNativeError", UInt32(lastNativeError), 32);
  setProperty("GSPDmaCleanupVerified", session.cleanupVerified);
  setProperty("GSPDmaResourcesHeld", session.resourcesHeld());
  setProperty("GSPDmaProviderOpen", providerOpen);
  setProperty("GSPDmaPinnedUntilRestart", pinned);
  setProperty("GSPDmaCommand", pci->configRead16(4), 32);
  setProperty("GSPDmaNativeLifecyclePassed", session.state == P::State::Finished && session.cleanupVerified);
  if (dma) setProperty("GSPDmaMapperMode", dma->mapperMode);
}

bool RTXProbe::cleanupNative() {
  retireRuntime();
  const bool released=coordinator.cleanup(*this);
  if(released)sequenceWorkspaceOwned=false;
  return released;
}

bool RTXProbe::sealInputsValid(P::U64 generation) {
  const bool afterFwsec=coordinator.phase()==GSPExecutionOwner::Phase::FwsecDone;
  if(!generation || generation!=getRegistryEntryID() || !providerOpen || !pci->isOpen(this) ||
     pci->configRead16(4)!=0 || !dma || !dma->ready() || !pageScratch || !context ||
     (coordinator.phase()!=GSPExecutionOwner::Phase::Sealing && !afterFwsec) || !startupPrepared ||
     session.state!=P::State::Published || !coordinator.ledger().uploadsFrozen())return false;
  for(unsigned r=0;r<P::ResourceCount;++r)
    if(session.writeEpoch[r]!=session.outEpoch[r] || !dma->row(r).dmaPrepared || !dma->row(r).synchronized)return false;
  nativePagesValid=P::validatePages(dma->pages(),P::TotalPages,pageScratch,P::TotalPages)==P::Error::Ok;
  directPages=dma->directlyMapped();
  if(afterFwsec)return nativePagesValid && directPages && fwsec && fwsecResult && fwsecResult->passed &&
    fwsec->providerHeld() && fwsec->regionPersistent() && coordinator.ledger().startAttempted();
  return nativePagesValid && directPages && coordinator.ledger().matchesBeforeDeviceUse(
    context->identity,context->board,context->display,generation,true);
}

IOReturn RTXProbe::failRequest(P::Error error, IOReturn nativeError) {
  retireRuntime();
  lastNativeError = nativeError;
  coordinator.fail();
  session.fail(error);
  if (session.resourcesHeld()) session.cleanupResult(cleanupNative());
  publishState();
  return nativeError;
}

IOReturn RTXProbe::newUserClient(task_t task, void *securityID, UInt32 type,
  OSDictionary *properties, IOUserClient **handler) {
  if (!handler || type != 0) return kIOReturnBadArgument;
  *handler = nullptr;
  if (IOUserClient::clientHasPrivilege(task, kIOClientPrivilegeAdministrator) != kIOReturnSuccess)
    return kIOReturnNotPrivileged;
  if (!mutex) return kIOReturnNotReady;
  IOLockLock(mutex);
  if (!preflightPassed || consumed || stopping || isInactive() || pci->isInactive()) {
    IOLockUnlock(mutex); return kIOReturnNotReady;
  }
  // Reserve the one client before framework calls. No attach/start/detach or
  // destruction runs under our mutex, avoiding callback lock inversion.
  consumed = true;
  retain();
  IOLockUnlock(mutex);
  auto *client = new RTXGSPLaunchUserClient;
  if (!client) { release(); return kIOReturnNoMemory; }
  if (!client->initWithTask(task, securityID, type, properties)) {
    client->release(); release(); return kIOReturnError;
  }
  if (!client->attach(this)) {
    client->release(); release(); return kIOReturnError;
  }
  if (!client->start(this)) {
    client->detach(this); client->release(); release(); return kIOReturnError;
  }
  IOLockLock(mutex);
  const bool accept = !stopping && !isInactive() && !pci->isInactive();
  if (accept) { activeClient = client; *handler = client; }
  IOLockUnlock(mutex);
  if (!accept) { client->stop(this); client->detach(this); client->release(); }
  release();
  return accept ? kIOReturnSuccess : kIOReturnOffline;
}

void RTXProbe::fillInfo(P::U64 *out) {
  bzero(out, 160 * sizeof(*out));
  bool allocated = false;
  if (dma) for (unsigned i = 0; i < P::ResourceCount; ++i)
    allocated = allocated || dma->row(i).allocated || dma->row(i).dmaAllocated;
  out[0] = 0x525458444d413133ULL; out[1] = 1; out[2] = P::U32(session.state);
  out[3] = P::ResourceCount; out[4] = P::Page; out[5] = 0x824148;
  out[6] = fuse.first; out[7] = fuse.second; out[8] = fuse.reads; out[9] = P::U32(fuse.index);
  out[10] = P::TotalBytes; out[11] = getRegistryEntryID();
  out[12] = allocated; out[13] = session.state == P::State::Published;
  out[14] = session.cleanupVerified; out[15] = pci->configRead16(4);
  for (unsigned i = 0; i < P::ResourceCount; ++i) {
    P::U64 *r = out + 16 + 16 * i;
    r[0] = i; r[1] = P::Sizes[i]; r[2] = P::Pages[i];
    r[3] = session.written[i]; r[5] = session.readBytes[i];
    for (unsigned e = 7; e <= 13; ++e) r[e] = UInt32(kIOReturnNotReady);
    if (!dma) continue;
    const auto &m = dma->row(i);
    r[4] = m.synchronized; r[6] = m.pagesEnumerated;
    r[7] = UInt32(m.memoryPrepareError); r[8] = UInt32(m.setDescriptorError);
    r[9] = UInt32(m.prepareError); r[10] = UInt32(m.synchronizeError);
    r[11] = UInt32(m.completeError); r[12] = UInt32(m.clearError);
    r[13] = UInt32(m.memoryCompleteError); r[14] = m.retained; r[15] = m.everPrepared;
  }
}

struct GSPRomCapture {
  Preparation::RomResult &result;
  template<class IO> const char *operator()(IO &io){return Preparation::captureRom(io,result);}
};
IOReturn RTXProbe::launch(){
  const auto gen=getRegistryEntryID();
  if(!coordinator.sealed() || !dma || !dma->ready() || !context || !context->capture() ||
     !dma->directlyMapped() || !providerOpen || !pci->isOpen(this))return kIOReturnNotReady;
  // Allocate diagnostics before the first device mutation; large results and
  // firmware record buffers must never live on the kernel stack.
  rom=new MacGSP15Boot0(pci,this);fwsecResult=new FWSECExecution::Result;
  sec2Result=new SEC2FirstBootStage::Result;
  firstRecord=static_cast<unsigned char *>(IOMalloc(GSPEvents::MaxBytes));
  afterRecords=static_cast<unsigned char *>(IOMalloc(GSPInitEvents::MaxBytes));
  rmRecords=static_cast<unsigned char *>(IOMalloc(GSPComputePrep::MaxBytes));
  rmRequests=static_cast<unsigned char *>(IOMalloc(GSPComputePrep::RequestBytes));
  bar1LeaseOwned=sequenceWorkspaceOwned&&GSPBar1::LeaseStart<=GSPBar1::Start&&GSPBar1::End<=GSPBar1::LeaseEnd;
  bar1Map=new MacGSPBar1Mapping(pci,*context);
  const bool bar1Mapped=bar1Map&&bar1Map->map();
  bar1Original=static_cast<unsigned char*>(IOMalloc(GSPBar1::Bytes));
  bar1Capture=static_cast<unsigned char*>(IOMalloc(GSPBar1::CaptureBytes));
  if(bar1Original)bzero(bar1Original,GSPBar1::Bytes);
  if(bar1Capture)bzero(bar1Capture,GSPBar1::CaptureBytes);
  if(rmRecords)bzero(rmRecords,GSPComputePrep::MaxBytes);
  if(rmRequests)bzero(rmRequests,GSPComputePrep::RequestBytes);
  pageLeaseOwned=bar1LeaseOwned&&GSPPageTables::Start>=GSPBar1::End&&GSPPageTables::End<=GSPBar1::LeaseEnd;
  pageMap=new MacGSPPageTablesMapping(pci,*context);
  const bool pageMapped=pageMap&&pageMap->map();
  pageCapture=static_cast<unsigned char*>(IOMalloc(GSPPageTables::Bytes*2));
  pdRecords=static_cast<unsigned char*>(IOMalloc(GSPComputePrep::MaxBytes));
  pdRequest=static_cast<unsigned char*>(IOMalloc(GSPComputePrep::Page));
  if(pageCapture)bzero(pageCapture,GSPPageTables::Bytes*2);
  if(pdRecords)bzero(pdRecords,GSPComputePrep::MaxBytes);
  if(pdRequest)bzero(pdRequest,GSPComputePrep::Page);
  channelMap=new MacChannelMemoryMapping(pci,*context);
  const bool channelMapped=channelMap&&channelMap->map();
  channelState=new MacChannelMemoryState;
  if(channelState){
    channelState->leaseOwned=pageLeaseOwned&&GMMULeaves::NewBase>=GSPPageTables::End&&GMMULeaves::LeaseEnd<=GSPBar1::LeaseEnd&&
      ChannelMemory::RingStart>=GMMULeaves::LeaseEnd&&ChannelMemory::RingStart+ChannelMemory::RingBytes<=0x1200000;
    channelState->oldCapture=pageCapture?pageCapture+GSPPageTables::Bytes:nullptr;
    channelState->ringImage=static_cast<unsigned char*>(IOMalloc(8192));
    channelState->children=static_cast<unsigned char*>(IOMalloc(GMMULeaves::MaxChildBytes));
    channelState->scratch=static_cast<unsigned char*>(IOMalloc(4096));
    if(channelState->ringImage)bzero(channelState->ringImage,8192);
    if(channelState->children)bzero(channelState->children,GMMULeaves::MaxChildBytes);
    if(channelState->scratch)bzero(channelState->scratch,4096);
  }
  channelRecords=static_cast<unsigned char*>(IOMalloc(GSPComputePrep::MaxBytes));
  channelRequests=static_cast<unsigned char*>(IOMalloc(ChannelCodec::Steps*GSPComputePrep::Page));
  channelRootCapture=static_cast<unsigned char*>(IOMalloc(ChannelSnapshot::RootBytes));
  channelChildCapture=static_cast<unsigned char*>(IOMalloc(ChannelSnapshot::MaxChildren));
  if(channelRecords)bzero(channelRecords,GSPComputePrep::MaxBytes);
  if(channelRequests)bzero(channelRequests,ChannelCodec::Steps*GSPComputePrep::Page);
  if(channelRootCapture)bzero(channelRootCapture,ChannelSnapshot::RootBytes);
  if(channelChildCapture)bzero(channelChildCapture,ChannelSnapshot::MaxChildren);
  if(!channelMapped||!channelState||!channelState->leaseOwned||!channelState->oldCapture||!channelState->ringImage||!channelState->children||
    !channelState->scratch||!channelRecords||!channelRequests||!channelRootCapture||!channelChildCapture)
    return failRequest(P::Error::TransportFailed,kIOReturnNoMemory);
  executionState=new MacExecutionMemoryState;
  if(executionState){
    executionState->leaseOwned=channelState->leaseOwned&&ExecutionPlan::Base>=0x3300000&&ExecutionPlan::FixedEnd<0x4000000;
    auto &s=executionState->storage;s.root=channelRootCapture;s.goldenChildren=channelChildCapture;
    s.rootScratch=static_cast<unsigned char*>(IOMalloc(12288));
    s.expectedScratch=static_cast<unsigned char*>(IOMalloc(GMMULeaves::MaxChildBytes));
    s.fixedChildren=static_cast<unsigned char*>(IOMalloc(GMMULeaves::MaxChildBytes));
    s.fullChildren=static_cast<unsigned char*>(IOMalloc(GMMULeaves::MaxChildBytes));
    s.scratch=static_cast<unsigned char*>(IOMalloc(4096));
    if(s.rootScratch)bzero(s.rootScratch,12288);
    if(s.expectedScratch)bzero(s.expectedScratch,GMMULeaves::MaxChildBytes);
    if(s.fixedChildren)bzero(s.fixedChildren,GMMULeaves::MaxChildBytes);
    if(s.fullChildren)bzero(s.fullChildren,GMMULeaves::MaxChildBytes);
    if(s.scratch)bzero(s.scratch,4096);
  }
  executionQueue.scratch=static_cast<unsigned char*>(IOMalloc(4096));
  executionRecords=static_cast<unsigned char*>(IOMalloc(GSPComputePrep::MaxBytes));
  executionRequests=static_cast<unsigned char*>(IOMalloc(ExecutionCodec::RequestBytes));
  externalRecords=static_cast<unsigned char*>(IOMalloc(GSPComputePrep::MaxBytes));
  externalRequests=static_cast<unsigned char*>(IOMalloc(ExternalVAS::RequestBytes));
  executionRootCapture=static_cast<unsigned char*>(IOMalloc(12288));
  executionChildCapture=static_cast<unsigned char*>(IOMalloc(GMMULeaves::MaxChildBytes));
  executionDeviceCapture=static_cast<unsigned char*>(IOMalloc(ExecutionCapture::Bytes));
  if(executionQueue.scratch)bzero(executionQueue.scratch,4096);
  if(executionRecords)bzero(executionRecords,GSPComputePrep::MaxBytes);
  if(executionRequests)bzero(executionRequests,ExecutionCodec::RequestBytes);
  if(externalRecords)bzero(externalRecords,GSPComputePrep::MaxBytes);
  if(externalRequests)bzero(externalRequests,ExternalVAS::RequestBytes);
  if(executionRootCapture)bzero(executionRootCapture,12288);
  if(executionChildCapture)bzero(executionChildCapture,GMMULeaves::MaxChildBytes);
  if(executionDeviceCapture)bzero(executionDeviceCapture,ExecutionCapture::Bytes);
  if(!executionState||!executionState->leaseOwned||!executionState->storage.rootScratch||!executionState->storage.expectedScratch||
     !executionState->storage.fixedChildren||!executionState->storage.fullChildren||!executionState->storage.scratch||!executionQueue.scratch||
     !externalRecords||!externalRequests||!executionRecords||!executionRequests||!executionRootCapture||!executionChildCapture||!executionDeviceCapture)
    return failRequest(P::Error::TransportFailed,kIOReturnNoMemory);
  runtimeState=new RtxReusableRuntime035::State;
  computeState=new MacProgramMemoryState;
  if(computeState){
    computeState->leaseOwned=executionState->leaseOwned&&ProgramMemory::Base==ExecutionPlan::FixedEnd&&
      ProgramMemory::Base+ProgramMemory::Bytes<ExecutionPlan::ContextBase;
    auto &s=computeState->storage;const auto &e=executionState->storage;
    unsigned libraryBytes=0,codeBytes=0;
    if(shaderLibrary.phase()!=RtxLibraryUpload036::Phase::Consumed||
       !shaderLibrary.data(libraryScope(),0,s.library,libraryBytes)||
       !shaderLibrary.data(libraryScope(),1,s.code,codeBytes)||libraryBytes!=512||codeBytes!=4096)
      return failRequest(P::Error::InvalidState,kIOReturnNotReady);
    s.root=e.root;s.goldenChildren=e.goldenChildren;s.liveChildren=e.fullChildren;
    s.rootScratch=static_cast<unsigned char*>(IOMalloc(12288));
    s.expectedScratch=static_cast<unsigned char*>(IOMalloc(GMMULeaves::MaxChildBytes));
    s.children=static_cast<unsigned char*>(IOMalloc(GMMULeaves::MaxChildBytes));
    s.image=static_cast<unsigned char*>(IOMalloc(ProgramMemory::Bytes));
    s.command=static_cast<unsigned char*>(IOMalloc(32));
    s.scratch=static_cast<unsigned char*>(IOMalloc(4096));
    if(s.rootScratch)bzero(s.rootScratch,12288);
    if(s.expectedScratch)bzero(s.expectedScratch,GMMULeaves::MaxChildBytes);
    if(s.children)bzero(s.children,GMMULeaves::MaxChildBytes);
    if(s.image)bzero(s.image,ProgramMemory::Bytes);
    if(s.command)bzero(s.command,32);
    if(s.scratch)bzero(s.scratch,4096);
  }
  computeRootCapture=static_cast<unsigned char*>(IOMalloc(ProgramCapture::RootBytes));
  computeChildCapture=static_cast<unsigned char*>(IOMalloc(ProgramCapture::MaxChildren));
  computeDeviceCapture=static_cast<unsigned char*>(IOMalloc(ProgramCapture::DeviceBytes));
  if(computeRootCapture)bzero(computeRootCapture,ProgramCapture::RootBytes);
  if(computeChildCapture)bzero(computeChildCapture,ProgramCapture::MaxChildren);
  if(computeDeviceCapture)bzero(computeDeviceCapture,ProgramCapture::DeviceBytes);
  bool jobCapturesReady=runtimeState&&computeState;
  if(runtimeState&&computeState){
    auto &a=runtimeState->storage;const auto &m=computeState->storage;
    a.root=m.root;a.children=m.children;a.library=m.library;a.code=m.code;a.childBytes=GMMULeaves::MaxChildBytes;
    a.captureRoot=static_cast<unsigned char*>(IOMalloc(12288));
    a.captureChildren=static_cast<unsigned char*>(IOMalloc(GMMULeaves::MaxChildBytes));
    a.captureDevice=static_cast<unsigned char*>(IOMalloc(RtxReusableBacking035::DeviceBytes));
    runtimeSnapshot.request=static_cast<unsigned char*>(IOMalloc(RtxReusable035::WireBytes));
    runtimeSnapshot.plan=static_cast<unsigned char*>(IOMalloc(RtxReusableABI035::PlanBytes));
    if(a.captureRoot)bzero(a.captureRoot,12288);
    if(a.captureChildren)bzero(a.captureChildren,GMMULeaves::MaxChildBytes);
    if(a.captureDevice)bzero(a.captureDevice,RtxReusableBacking035::DeviceBytes);
    if(runtimeSnapshot.request)bzero(runtimeSnapshot.request,RtxReusable035::WireBytes);
    if(runtimeSnapshot.plan)bzero(runtimeSnapshot.plan,RtxReusableABI035::PlanBytes);
    jobCapturesReady=runtimeSnapshot.request&&runtimeSnapshot.plan&&RtxReusableRuntime035::bound(*runtimeState,a);
  }
  if(!runtimeState||!jobCapturesReady||!computeState||!computeState->leaseOwned||!computeState->storage.rootScratch||!computeState->storage.expectedScratch||
     !computeState->storage.children||!computeState->storage.image||!computeState->storage.command||!computeState->storage.scratch||
     !computeRootCapture||!computeChildCapture||!computeDeviceCapture)return failRequest(P::Error::TransportFailed,kIOReturnNoMemory);
  if(!rom || !fwsecResult || !sec2Result || !firstRecord || !afterRecords || !rmRecords || !rmRequests || !sequenceWorkspaceOwned || !bar1Original || !bar1Capture || !bar1LeaseOwned || !bar1Mapped || !pageLeaseOwned || !pageMapped || !pageCapture || !pdRecords || !pdRequest)
    return failRequest(P::Error::TransportFailed,kIOReturnNoMemory);
  bzero(firstRecord,GSPEvents::MaxBytes);
  bzero(afterRecords,GSPInitEvents::MaxBytes);
  GSPRomCapture capture{romResult};const auto captured=Boot0::run(*rom,capture);
  if(!captured.passed || !romResult.stable || context->command()!=0){
    launchStatus="fresh-rom-capture-failed";return failRequest(P::Error::TransportFailed);
  }
  fwsec=new MacGSP15FWSEC(pci,this,rom->romBytes(),context->fwsecFuse,2,coordinator,gen);
  sec2=new MacSEC2FirstBootStage(pci,this,*dma);
  if(!fwsec || !sec2)return failRequest(P::Error::TransportFailed,kIOReturnNoMemory);
  fwsec->protectedPages=dma->pages();
  if(!fwsec->imageMatchesBoard() || !sec2->map()){
    launchStatus="native-launch-preparation-failed";return failRequest(P::Error::TransportFailed);
  }
  if(!coordinator.beginFwsec(gen))return failRequest(P::Error::InvalidState);
  pin();launchStatus="running-fwsec";publishState();
  FWSECExecution::run(*fwsec,*fwsecResult);
  borrowedOwnerVerified=providerOpen && pci->isOpen(this) && fwsec->providerHeld() && fwsec->regionPersistent();
  if(!coordinator.fwsecComplete(gen,fwsecResult->passed && borrowedOwnerVerified && context->command()==0)){
    launchStatus="fwsec-failed-retained";publishState();return kIOReturnError;
  }
  // The fresh FWSEC result belongs to this call and this continuous owner.
  // Reimport and revalidate all nine buffers before SEC2 can see their pointers.
  for(unsigned r=0;r<P::ResourceCount;++r)if(!dma->synchronizeOne(r,1)){
    coordinator.fail();launchStatus="post-fwsec-import-failed-retained";publishState();return kIOReturnError;
  }
  postFwsecVerified=GSPContentSeal::run(*this,dma->pages(),gen,context->system,sealScratch,sealExpected,postFwsecSeal);
  if(!postFwsecVerified || !coordinator.beginSec2(gen)){
    coordinator.fail();launchStatus="post-fwsec-seal-failed-retained";publishState();return kIOReturnError;
  }
  sec2->frtsLo=fwsecResult->boot.wprLo;sec2->frtsHi=fwsecResult->boot.wprHi;
  launchStatus="staging-sec2";publishState();
  SEC2FirstBootStage::run(*sec2,*sec2Result);
  if(!coordinator.sec2Complete(gen,sec2Result->staged && context->command()==6)){
    launchStatus="sec2-stage-failed-retained";publishState();return kIOReturnError;
  }
  if(!coordinator.beginBoot(gen)){launchStatus="boot-order-rejected";publishState();return kIOReturnError;}
  MacGSPFirstBoot151 boot(*context,*dma,coordinator,*sec2Result,*fwsecResult,gen);
  launchStatus="starting-sec2-and-gsp";publishState();
  GSPFirstBoot151::run(boot,bootResult);
  if(!coordinator.bootReturned(gen,bootResult.passed)){
    launchStatus="first-boot-failed-retained";publishState();return kIOReturnError;
  }
  struct StatusIO {
    MacGSPContext &context;MacGSPRuntimeDma &dma;GSPExecutionOwner::Owner &owner;
    bool ready(){return context.owned() && context.command()==6 && dma.ready() &&
      owner.phase()==GSPExecutionOwner::Phase::BootReturned && owner.ledger().requiresPin();}
    bool import(){return dma.synchronizeOne(P::Queues,1);}
    bool read(unsigned off,unsigned char *out,unsigned bytes){return dma.copyOut(P::Queues,off,out,bytes);}
    void delayUs(unsigned us){context.delayUs(us);}
    GSPEvents::U64 nowNs(){uint64_t absolute=0,ns=0;clock_get_uptime(&absolute);
      absolutetime_to_nanoseconds(absolute,&ns);return ns;}
  } statusIO{*context,*dma,coordinator};
  GSPEvents::capture(statusIO,firstRecord,sealScratch,events);
  // Preserve the prior first-record ABI, including a valid prefix when a
  // later event fails. This is immutable captured memory, not live queue I/O.
  firstStatus.headerValid=events.headerValid;firstStatus.polls=events.polls;
  for(unsigned i=0;i<8;++i)firstStatus.header[i]=events.header[i];
  if(events.count && GSPFirstStatus::record(firstRecord,events.records[0].bytes,firstStatus))
    firstStatus.status="first-record-from-event-capture";
  bool complete=events.initDone;
  if(!complete){
    GSPSequencer::Profile profile;
    const bool request=events.passed && events.sequencer && events.count==2 && events.pages==3 &&
      events.records[0].function==0x1020 && events.records[0].bytes==4096 &&
      events.records[1].function==0x1002 && events.records[1].result==0 &&
      events.records[1].offset==4096 && events.records[1].bytes==8192 &&
      events.records[1].payloadBytes==GSPSequencer::PayloadBytes &&
      GSPSequencer::profile(firstRecord+4096+80,GSPSequencer::PayloadBytes,profile);
    if(request){
      struct ConsumeIO {
        StatusIO &status;bool &claimed;
        bool ready(){return status.ready();}
        bool profileValidated(){return true;} // Constructed only after complete canonical validation above.
        bool import(){return status.import();}
        bool read(unsigned off,unsigned char *out,unsigned bytes){return status.read(off,out,bytes);}
        bool claimConsumption(){if(claimed)return false;claimed=true;return true;}
        bool writeConsumerThree(){const unsigned char value[4]={3,0,0,0};return status.dma.copyIn(P::Queues,0x1020,value,4);}
        bool publish(){return status.dma.synchronizeOne(P::Queues,2);}
      } consumeIO{statusIO,consumeClaimed};
      if(GSPQueueConsume::consume(consumeIO,queueConsume)){
        MacGSPSequencer engine(*context,*dma,coordinator,gen,sequenceWorkspaceOwned,
          sequenceClaimed,sequenceGspStartNoted,sequenceSec2StartNoted);
        GSPSequencer::execute(engine,firstRecord+4096+80,GSPSequencer::PayloadBytes,sequence);
        if(sequence.passed){
          GSPInitEvents::capture(statusIO,afterRecords,sealScratch,afterEvents,3,2);
          complete=afterEvents.passed&&afterEvents.initDone;
        }
      }else sequence.status="sequence-prefix-consumption-failed";
    }else GSPSequencer::fail(sequence,GSPSequencer::InvalidProfile,"sequence-request-profile-rejected");
  }
  if(complete){
    MacGSPComputePrep runtime(*context,*dma,coordinator,gen,sequenceWorkspaceOwned,sequence,rmClaimed);
    GSPComputePrep::execute(runtime,afterEvents,afterRecords,rmRecords,rmRequests,sealScratch,rm);
    complete=rm.passed;
  }
  if(complete){
    MacGSPBar1 memory(*context,*bar1Map,coordinator,rm,gen,bar1LeaseOwned,bar1Claimed);
    GSPBar1::execute(memory,bar1Original,bar1Capture,bar1);complete=bar1.passed;
  }
  if(complete){
    MacGSPPageTablesMemory memory(*context,*pageMap,coordinator,rm,bar1,gen,pageLeaseOwned,pageClaimed);
    GSPPageTables::stage(memory,pageCapture,pageTables);complete=pageTables.passed;
    if(complete){
      MacGSPPageTablesRM runtime(*context,*dma,coordinator,gen,sequenceWorkspaceOwned,sequence,pageTables,pdRmClaimed);
      GSPPageTablesRM::execute(runtime,rm,rmRecords,pageTables,pdRecords,pdRequest,sealScratch,pdRm);complete=pdRm.passed;
    }
    if(complete){GSPPageTablesSnapshot::capture(memory,pageCapture+GSPPageTables::Bytes,pagePost);complete=pagePost.passed;}
    GSPPageTables::restoreWindow(memory,pageTables);
    complete=complete&&pageTables.passed&&pageTables.windowRestored;
  }
  if(complete){
    MacChannelMemory memory(*context,*channelMap,coordinator,pageTables,pdRm,pagePost,gen,*channelState);
    complete=ChannelTransactions::prefix(rm,rmRecords,pdRm,pdRecords)&&memory.stageRing();
    if(complete){
      MacChannelTransactions runtime(*context,*dma,memory);
      ChannelTransactions::execute(runtime,rm,rmRecords,pdRm,pdRecords,channelRecords,channelRequests,sealScratch,channel);
      complete=channel.rpc.passed&&channelState->contexts.passed;
    }
    if(channelState->ring.attempted&&channelState->physicalMode){
      const unsigned bytes=channelState->contexts.attempted?channelState->contexts.childBytes:channelState->ring.childBytes;
      ChannelSnapshot::capture(memory,bytes,channelRootCapture,channelChildCapture,channelSnapshot);
    }
    complete=complete&&channelSnapshot.passed;
    memory.restoreWindow();complete=complete&&channelState->ring.passed&&channelState->ring.windowRestored;
    if(complete){
      executionState->storage.goldenBytes=channelSnapshot.childBytes;
      MacExecutionMemory executionMemory(*context,*channelMap,memory,channel,channelSnapshot,coordinator,gen,*executionState);
      MacExecutionTransactions runtime(*context,*dma,executionMemory,channel,executionQueue);
      complete=ExecutionTransactions::execute(runtime,rm,rmRecords,pdRm,pdRecords,channel,channelRecords,
        executionRecords,executionRequests,sealScratch,execution,externalRecords,externalRequests);
      if(complete){
        MacHostFence fence(*context,*channelMap,executionMemory,executionQueue,execution,executionState->storage.scratch,fenceState);
        complete=HostFence::execute(fence,channel.context,execution,executionRequests,executionRecords,sealScratch,hostFence);
      }
      if(executionState->fixed.attempted&&executionState->physicalMode){
        const unsigned bytes=executionState->contexts.attempted?executionState->contexts.childBytes:executionState->fixed.childBytes;
        ChannelSnapshot::capture(executionMemory,bytes,executionRootCapture,executionChildCapture,executionSnapshot);
        ExecutionCapture::capture(executionMemory,executionDeviceCapture,executionCapture);
      }
      // Preserve the HOST snapshots above before adding compute PTEs or work.
      complete=complete&&executionSnapshot.passed&&executionCapture.passed;
      if(complete)complete=computeAfterHost(executionMemory);
      executionMemory.restoreWindow();
      complete=complete&&executionSnapshot.passed&&executionCapture.passed&&executionState->fixed.windowRestored;
    }
  }
  if(complete){
    MacReusableBackend035 backend(*context,*channelMap,coordinator,*executionState,*computeState,execution,hostFence,*runtimeState,getRegistryEntryID(),runtimeCaller());
    MacReusableRuntime035 runtime(backend,*runtimeState);
    complete=runtime.open();
  }
  if(complete)launchStatus="program-runtime-ready-no-shader-submitted";
  else {complete=false;retireRuntime();coordinator.fail();launchStatus="gsp-init-host-or-program-preparation-failed-retained";}
  publishState();return complete?kIOReturnSuccess:kIOReturnError;
}

bool RTXProbe::computeAfterHost(MacExecutionMemory &memory){
  if(!runtimeState||!activeClient||!computeState||!computeState->leaseOwned||!hostFence.passed)return false;
  auto &s=computeState->storage;s.goldenBytes=executionState->storage.goldenBytes;s.liveBytes=executionState->contexts.childBytes;
  MacReusableMemory035 preparation(*context,*channelMap,memory,executionQueue,execution,fenceState,hostFence,*computeState);
  bool complete=preparation.stage(channel.context);
  if(computeState->claimed){
    ProgramCapture::Reader<MacReusableMemory035,MacExecutionMemory> reader(preparation,memory);
    ProgramCapture::capture(reader,s.liveBytes,computeRootCapture,computeChildCapture,computeDeviceCapture,computeCapture);
  }
  if(!complete||!computeCapture.passed)return false;
  MacReusableBackend035 backend(*context,*channelMap,coordinator,*executionState,*computeState,execution,hostFence,*runtimeState,getRegistryEntryID(),runtimeCaller());
    MacReusableRuntime035 runtime(backend,*runtimeState);
  auto storage=runtimeState->storage;storage.childBytes=s.liveBytes;
  return runtime.prepare(getRegistryEntryID(),runtimeCaller(),storage);
}

void RTXProbe::launchInfo(P::U64 *out){
  bzero(out,512);out[0]=0x525458424f4f5431ULL;out[1]=2;out[2]=getRegistryEntryID();
  out[3]=P::U32(coordinator.phase());out[4]=coordinator.executionAttempted();out[5]=coordinator.startMask();
  out[6]=providerOpen;out[7]=pinned;out[8]=coordinator.ledger().owned();out[9]=pci->configRead16(4);
  out[10]=borrowedOwnerVerified;out[11]=postFwsecVerified;
  if(fwsecResult){const auto &f=*fwsecResult;out[12]=f.passed;out[13]=f.boot.startAttempted;out[14]=f.boot.startWriteAccepted;
    out[15]=f.boot.mailbox0;out[16]=f.boot.wprLo;out[17]=f.boot.wprHi;out[18]=f.stage.imemCompleted;out[19]=f.stage.dmemCompleted;
    out[20]=f.stage.dmemMatched;out[21]=f.stage.lifecycle.cleanupVerified;out[22]=f.stage.lifecycle.commandAfter;}
  if(sec2Result){const auto &s=*sec2Result;out[23]=s.staged;out[24]=s.imemCompleted;out[25]=s.dmemCompleted;
    out[26]=s.dmemMatched;out[27]=s.anyDMA;out[28]=s.commandEnabled;}
  const auto &b=bootResult;out[29]=b.gspResetVerified;out[30]=b.startAttempted;out[31]=b.startWriteAccepted;
  out[32]=b.aliasUsed;out[33]=b.cpuBefore;out[34]=b.cpuAfter;out[35]=b.mailbox0;out[36]=b.mailbox1;
  out[37]=b.sec2Halted;out[38]=b.gspActive;out[39]=b.gspRiscv;out[40]=b.gspBcr;out[41]=b.wprLo;out[42]=b.wprHi;
  out[43]=b.handoff;out[44]=b.passed;out[45]=b.failedReg;out[46]=b.writes;out[47]=b.haltPolls;
  const auto &s=firstStatus;out[48]=s.headerValid;out[49]=s.captured;out[50]=s.recordBytes;out[51]=s.function;
  out[52]=s.result;out[53]=s.sequence;out[54]=s.payloadBytes;out[55]=s.initDone;out[56]=s.sequencer;
  out[57]=s.polls;out[58]=s.header[4];out[59]=romResult.stable;out[60]=session.resourcesHeld();
  out[61]=b.lastPollReg;out[62]=b.lastPollValue;out[63]=b.lastPollCount;
}

ChannelABI::Owner RTXProbe::channelOwner(){
  ChannelABI::Owner out;out.generation=getRegistryEntryID();out.phase=unsigned(coordinator.phase());out.pinned=pinned;
  out.owned=coordinator.ledger().owned();out.command=pci->configRead16(4);
  if(channelMap){out.mapped=channelMap->mapped();out.barBase=channelMap->barBase;out.physical=out.mapped?out.barBase+MacChannelMemoryMapping::Start:0;}
  if(channelState){const auto &s=*channelState;out.lease=s.leaseOwned;out.ringClaimed=s.ringClaimed;out.contextsClaimed=s.contextsClaimed;
    out.windowObserved=s.windowObserved;out.physicalMode=s.physicalMode;out.excludedNs=s.excludedStagingNs;out.queueClaimed=s.queueClaimed;}
  return out;
}

ChannelABI::Owner RTXProbe::executionOwner(){
  auto out=channelOwner();out.excludedNs=executionQueue.excludedStagingNs;out.queueClaimed=executionQueue.claimed;
  if(executionState){const auto &s=*executionState;out.lease=s.leaseOwned;out.ringClaimed=s.fixedClaimed;out.contextsClaimed=s.contextsClaimed;
    out.windowObserved=s.windowObserved;out.physicalMode=s.physicalMode;}
  else {out.lease=out.ringClaimed=out.contextsClaimed=out.windowObserved=out.physicalMode=0;}
  return out;
}

ChannelABI::Owner RTXProbe::computeOwner(){
  auto out=executionOwner();out.lease=computeState&&computeState->leaseOwned;return out;
}

IOReturn RTXProbe::computeMethod(UInt32 selector,IOExternalMethodArguments *a){
  // methodLocked has already checked inline IPC pointers/descriptors.
  auto shape=[&](unsigned scalars,unsigned input,unsigned output){
    return a->scalarInputCount==scalars&&a->structureInputSize==input&&a->structureOutputSize==output;
  };
  if(selector==64){
    if(!shape(0,0,512))return kIOReturnBadArgument;
    ProgramMemory::Result empty;
    RtxProgramABI033::memory(computeState?computeState->result:empty,computeOwner(),computeState?computeState->writePhase:0,
      computeState?computeState->registerPhase:0,static_cast<P::U64*>(a->structureOutput));return kIOReturnSuccess;
  }
  if(selector==65)return kIOReturnUnsupported; // Superseded by serial-based selector70.
  if(selector==66){
    if(!shape(0,0,512))return kIOReturnBadArgument;
    RtxProgramABI033::capture(computeCapture,computeOwner(),static_cast<P::U64*>(a->structureOutput));return kIOReturnSuccess;
  }
  if(selector==67){
    if(a->scalarInputCount!=3||a->structureInputSize||a->scalarInput[0]>2||!a->scalarInput[2]||a->scalarInput[2]>4096||a->structureOutputSize!=a->scalarInput[2])return kIOReturnBadArgument;
    const unsigned which=unsigned(a->scalarInput[0]);
    const unsigned total=which==0?computeCapture.rootBytes:which==1?computeCapture.childBytes:computeCapture.deviceBytes;
    const unsigned char *source=which==0?computeRootCapture:which==1?computeChildCapture:computeDeviceCapture;
    if(!source||a->scalarInput[1]>total||a->scalarInput[2]>total-a->scalarInput[1])return kIOReturnBadArgument;
    bcopy(source+a->scalarInput[1],a->structureOutput,a->scalarInput[2]);return kIOReturnSuccess;
  }
  return kIOReturnBadArgument;
}

IOReturn RTXProbe::runtimeMethod(UInt32 selector,IOExternalMethodArguments *a){
  namespace N=RtxReusable035;namespace ABI=RtxReusableABI035;
  const auto shape=[&](unsigned scalars,unsigned input,unsigned output){return a->scalarInputCount==scalars&&a->structureInputSize==input&&a->structureOutputSize==output;};
  if(selector==ABI::InfoSelector){
    if(!shape(0,0,ABI::InfoBytes))return kIOReturnBadArgument;
    ABI::info(runtimeState,computeOwner(),computeCapture.passed,static_cast<P::U64*>(a->structureOutput));return kIOReturnSuccess;
  }
  if(!runtimeState||!context||!channelMap||!computeState||!executionState)return kIOReturnNotReady;
  if(selector==ABI::SubmitSelector){
    if(!shape(0,N::WireBytes,0))return kIOReturnBadArgument;
    if(stopping||isInactive()||pci->isInactive()){retireRuntime();coordinator.fail();publishState();return kIOReturnOffline;}
    const auto prior=runtimeState->core.result().serial;
    MacReusableBackend035 backend(*context,*channelMap,coordinator,*executionState,*computeState,execution,hostFence,*runtimeState,getRegistryEntryID(),runtimeCaller());
    MacReusableRuntime035 runtime(backend,*runtimeState);
    const auto result=runtime.submit(runtimeCaller(),static_cast<const unsigned char*>(a->structureInput),a->structureInputSize);
    if(runtimeState->core.result().serial!=prior&&!ABI::snapshot(*runtimeState,runtimeSnapshot)){
      retireRuntime();coordinator.fail();publishState();return kIOReturnError;
    }
    publishState();
    if(result==N::Failure::None)return kIOReturnSuccess;
    if(result==N::Failure::Shape||result==N::Failure::Identity||result==N::Failure::Order)return kIOReturnBadArgument;
    if(result==N::Failure::State)return kIOReturnNotReady;
    return kIOReturnError;
  }
  if(selector==ABI::JobSelector){
    if(!shape(1,0,ABI::JobBytes)||!ABI::job(*runtimeState,a->scalarInput[0],static_cast<P::U64*>(a->structureOutput)))return kIOReturnBadArgument;
    return kIOReturnSuccess;
  }
  if(selector==ABI::DataSelector){
    if(a->scalarInputCount!=4||a->structureInputSize||a->scalarInput[1]>=7||a->structureOutputSize!=a->scalarInput[3])return kIOReturnBadArgument;
    const unsigned char *source=nullptr;unsigned bytes=0;
    if(!ABI::data(*runtimeState,a->scalarInput[0],unsigned(a->scalarInput[1]),runtimeSnapshot,source,bytes)||!ABI::span(a->scalarInput[2],a->scalarInput[3],bytes))return kIOReturnBadArgument;
    bcopy(source+a->scalarInput[2],a->structureOutput,a->scalarInput[3]);return kIOReturnSuccess;
  }
  return kIOReturnBadArgument;
}

IOReturn RTXProbe::libraryMethod(UInt32 selector,IOExternalMethodArguments *a) {
  namespace A=RtxLibraryUploadABI036;namespace U=RtxLibraryUpload036;
  const A::Call call={reinterpret_cast<const uint64_t*>(a->scalarInput),a->scalarInputCount,
    static_cast<const uint8_t*>(a->structureInput),a->structureInputSize,
    static_cast<uint8_t*>(a->structureOutput),a->structureOutputSize};
  const auto result=A::dispatch(shaderLibrary,libraryScope(),selector,call);publishState();
  if(result==U::Error::None)return kIOReturnSuccess;
  if(result==U::Error::State||result==U::Error::Incomplete)return kIOReturnNotReady;
  if(result==U::Error::Scope)return kIOReturnNotPermitted;
  return kIOReturnBadArgument;
}

IOReturn RTXProbe::methodLocked(UInt32 selector, IOExternalMethodArguments *a) {
  // Only bounded synchronous inline IPC is accepted. No caller-supplied
  // descriptors, mappings, physical addresses, async callbacks or output arrays.
  if (!a || a->version != kIOExternalMethodArgumentsCurrentVersion || a->asyncWakePort ||
      a->asyncReference || a->asyncReferenceCount || a->structureInputDescriptor ||
      a->structureOutputDescriptor || a->structureOutputDescriptorSize ||
      a->scalarOutputCount || a->structureVariableOutputData ||
      (a->scalarInputCount && !a->scalarInput) ||
      (a->structureInputSize && !a->structureInput) ||
      (a->structureOutputSize && !a->structureOutput))
    return (RtxReusableABI035::selector(selector)||RtxLibraryUploadABI036::selector(selector))?kIOReturnBadArgument:failRequest(P::Error::TransportFailed);
  const auto shape = [&](unsigned scalars, unsigned input, unsigned output) {
    return a->scalarInputCount == scalars && a->structureInputSize == input &&
      a->structureOutputSize == output;
  };
  if(RtxLibraryUploadABI036::selector(selector))return libraryMethod(selector,a);
  if (selector == 1) {
    if (!shape(0, 0, 1280)) return failRequest(P::Error::TransportFailed);
    fillInfo(static_cast<P::U64 *>(a->structureOutput));
    return kIOReturnSuccess;
  }
  if (selector == 9) {
    if (!shape(0, 0, 640)) return failRequest(P::Error::TransportFailed);
    auto *out = static_cast<P::U64 *>(a->structureOutput);
    bzero(out, 640);
    out[0] = 0x52545853594e4331ULL; out[1] = 1; out[2] = getRegistryEntryID();
    out[3] = P::ResourceCount; out[4] = providerOpen; out[5] = pinned;
    out[6] = session.resourcesHeld(); out[7] = pci->configRead16(4);
    for (unsigned i = 0; i < P::ResourceCount; ++i) {
      auto *r = out + 8 + 8 * i;
      r[0] = i; r[1] = session.writeEpoch[i]; r[2] = session.outEpoch[i]; r[3] = session.inEpoch[i];
      r[4] = session.outCount[i]; r[5] = session.inCount[i];
      r[6] = session.written[i]; r[7] = session.readBytes[i];
    }
    return kIOReturnSuccess;
  }
  if(selector==12) {
    if(!shape(0,0,256))return failRequest(P::Error::TransportFailed);
    auto *out=static_cast<P::U64 *>(a->structureOutput);bzero(out,256);
    out[0]=0x5254585345414c31ULL;out[1]=1;out[2]=getRegistryEntryID();
    out[3]=P::U32(coordinator.phase());out[4]=coordinator.ledger().owned();
    out[5]=coordinator.ledger().uploadsFrozen();out[6]=coordinator.ledger().exposed();
    out[7]=coordinator.ledger().startAttempted();out[8]=coordinator.sealed();
    out[9]=sealResult.checkedBytes;out[10]=sealResult.comparedPages;out[11]=sealResult.hashedPages;
    out[12]=sealResult.resource;out[13]=sealResult.page;out[14]=sealResult.byte;
    out[15]=directPages;out[16]=nativePagesValid;out[17]=startupPrepared;
    if(context){out[18]=context->system.bar0;out[19]=context->system.bar1;out[20]=context->system.bar3;
      out[21]=context->system.maxUserVa;out[22]=context->system.revision;out[23]=context->system.linkCap;}
    out[24]=GSPLaunchOwnership::ReservedStart;out[25]=GSPLaunchOwnership::ReservedEnd;
    out[26]=sealResult.seal.structuralMask;out[27]=sealResult.seal.firmwareMask;
    out[28]=pci->configRead16(4);out[29]=providerOpen;out[30]=pinned;
    return kIOReturnSuccess;
  }
  if (selector == 7) {
    if (!shape(0, 0, 0)) return failRequest(P::Error::TransportFailed);
    const bool good = session.resourcesHeld() ? cleanupNative() : true;
    session.abort(good); publishState();
    return good ? kIOReturnSuccess : kIOReturnError;
  }
  if(selector==14){
    if(!shape(0,0,512))return failRequest(P::Error::TransportFailed);
    launchInfo(static_cast<P::U64 *>(a->structureOutput));return kIOReturnSuccess;
  }
  if(selector==15){
    if(a->scalarInputCount!=2 || a->structureInputSize || !a->scalarInput[1] || a->scalarInput[1]>4096 ||
       a->structureOutputSize!=a->scalarInput[1] || !firstRecord || !firstStatus.captured ||
       a->scalarInput[0]>firstStatus.recordBytes || a->scalarInput[1]>firstStatus.recordBytes-a->scalarInput[0])
      return kIOReturnBadArgument;
    bcopy(firstRecord+a->scalarInput[0],a->structureOutput,a->scalarInput[1]);return kIOReturnSuccess;
  }
  if(selector==16){
    if(!shape(0,0,256))return kIOReturnBadArgument;
    auto *out=static_cast<P::U64 *>(a->structureOutput);bzero(out,256);
    const auto &r=events;
    out[0]=0x52545845564e5431ULL;out[1]=1;out[2]=getRegistryEntryID();
    out[3]=r.stop;out[4]=r.headerValid;out[5]=r.passed;out[6]=r.count;
    out[7]=r.pages;out[8]=r.bytes;out[9]=r.producer;out[10]=r.polls;
    out[11]=r.elapsedNs;out[12]=r.initDone;out[13]=r.sequencer;
    out[14]=r.nocatCount;out[15]=r.failedSlot;out[16]=r.failure;out[17]=r.reader;
    for(unsigned i=0;i<8;++i)out[18+i]=r.header[i];
    out[26]=GSPEvents::MaxRecords;out[27]=GSPEvents::MaxPages;
    out[28]=GSPEvents::DurationNs;out[29]=GSPEvents::MaxPolls;
    out[30]=r.partialPolls;out[31]=r.pendingPages;return kIOReturnSuccess;
  }
  if(selector==17){
    if(a->scalarInputCount!=2 || a->structureInputSize || !a->scalarInput[1] || a->scalarInput[1]>32 ||
       a->structureOutputSize!=a->scalarInput[1]*72 || a->scalarInput[0]>events.count ||
       a->scalarInput[1]>events.count-a->scalarInput[0])return kIOReturnBadArgument;
    auto *out=static_cast<P::U64 *>(a->structureOutput);
    for(unsigned i=0;i<a->scalarInput[1];++i){
      const auto &r=events.records[a->scalarInput[0]+i];auto *row=out+9*i;
      row[0]=r.offset;row[1]=r.bytes;row[2]=r.function;row[3]=r.result;row[4]=r.sequence;
      row[5]=r.payloadBytes;row[6]=r.flags;row[7]=r.slot;row[8]=r.elapsedUs;
    }
    return kIOReturnSuccess;
  }
  if(selector==18){
    if(a->scalarInputCount!=2 || a->structureInputSize || !a->scalarInput[1] || a->scalarInput[1]>4096 ||
       a->structureOutputSize!=a->scalarInput[1] || !firstRecord || a->scalarInput[0]>events.bytes ||
       a->scalarInput[1]>events.bytes-a->scalarInput[0])return kIOReturnBadArgument;
    bcopy(firstRecord+a->scalarInput[0],a->structureOutput,a->scalarInput[1]);return kIOReturnSuccess;
  }
  if(selector==19){
    if(!shape(0,0,512))return kIOReturnBadArgument;
    auto *out=static_cast<P::U64 *>(a->structureOutput);bzero(out,512);
    const auto &r=sequence;out[0]=0x5254585345513031ULL;out[1]=1;out[2]=getRegistryEntryID();
    out[3]=r.validated;out[4]=r.attempted;out[5]=r.passed;out[6]=r.failure;
    out[7]=r.completed;out[8]=r.word;out[9]=r.opcode;out[10]=r.ticks;out[11]=r.reads;
    out[12]=r.writes;out[13]=r.polls;out[14]=r.resetCount;out[15]=r.imemCommands;out[16]=r.dmemCommands;
    out[17]=r.lastAddress;out[18]=r.lastValue;out[19]=r.falconStart;out[20]=r.falconHalted;
    out[21]=r.sec2Start;out[22]=r.resumed;out[23]=r.falconCpu;out[24]=r.falconMailbox0;
    out[25]=r.falconMailbox1;out[26]=r.sec2Cpu;out[27]=r.sec2Mailbox0;out[28]=r.riscv;
    out[29]=r.bcr;out[30]=r.handoff;out[31]=r.elapsedNs;out[32]=r.libosArgs;
    out[33]=sequenceWorkspaceOwned;out[34]=GSPSequencer::WorkspaceStart;out[35]=GSPSequencer::WorkspaceEnd;
    out[36]=queueConsume.attempted;out[37]=queueConsume.written;out[38]=queueConsume.verified;
    out[39]=queueConsume.failure;out[40]=queueConsume.readerBefore;out[41]=queueConsume.readerAfter;
    out[42]=queueConsume.producer;out[43]=sequenceClaimed;out[44]=sequenceGspStartNoted;out[45]=sequenceSec2StartNoted;
    out[46]=afterEvents.count;out[47]=afterEvents.pages;out[48]=afterEvents.bytes;
    out[49]=afterEvents.startSlot;out[50]=afterEvents.startSequence;out[51]=afterEvents.published;
    out[52]=afterEvents.stop;out[53]=afterEvents.passed;out[54]=afterEvents.initDone;out[55]=afterEvents.sequencer;
    out[56]=events.initDone||afterEvents.initDone;out[57]=GSPSequencer::BudgetNs;out[58]=GSPSequencer::MaxTicks;
    out[59]=GSPSequencer::PayloadBytes;out[60]=GSPSequencer::UsedWords;
    out[61]=GSPSequencer::CapacityWords;out[62]=GSPSequencer::OperationCount;return kIOReturnSuccess;
  }
  if(selector==20){
    if(!shape(0,0,256))return kIOReturnBadArgument;
    auto *out=static_cast<P::U64 *>(a->structureOutput);bzero(out,256);
    const auto &r=afterEvents;
    out[0]=0x52545845564e5431ULL;out[1]=3;out[2]=getRegistryEntryID();
    out[3]=r.stop;out[4]=r.headerValid;out[5]=r.passed;out[6]=r.count;
    out[7]=r.pages;out[8]=r.bytes;out[9]=r.producer;out[10]=r.polls;
    out[11]=r.elapsedNs;out[12]=r.initDone;out[13]=r.sequencer;
    out[14]=r.nocatCount;out[15]=r.failedSlot;out[16]=r.failure;out[17]=r.reader;
    for(unsigned i=0;i<8;++i)out[18+i]=r.header[i];
    out[26]=GSPInitEvents::MaxRecords;out[27]=GSPInitEvents::MaxPages;
    out[28]=GSPInitEvents::DurationNs;out[29]=GSPInitEvents::MaxPolls;
    out[30]=r.partialPolls;out[31]=r.pendingPages;return kIOReturnSuccess;
  }
  if(selector==21){
    if(a->scalarInputCount!=2 || a->structureInputSize || !a->scalarInput[1] || a->scalarInput[1]>32 ||
       a->structureOutputSize!=a->scalarInput[1]*72 || a->scalarInput[0]>afterEvents.count ||
       a->scalarInput[1]>afterEvents.count-a->scalarInput[0])return kIOReturnBadArgument;
    auto *out=static_cast<P::U64 *>(a->structureOutput);
    for(unsigned i=0;i<a->scalarInput[1];++i){const auto &r=afterEvents.records[a->scalarInput[0]+i];auto *row=out+9*i;
      row[0]=r.offset;row[1]=r.bytes;row[2]=r.function;row[3]=r.result;row[4]=r.sequence;
      row[5]=r.payloadBytes;row[6]=r.flags;row[7]=r.slot;row[8]=r.elapsedUs;
    }
    return kIOReturnSuccess;
  }
  if(selector==22){
    if(a->scalarInputCount!=2 || a->structureInputSize || !a->scalarInput[1] || a->scalarInput[1]>4096 ||
       a->structureOutputSize!=a->scalarInput[1] || !afterRecords || a->scalarInput[0]>afterEvents.bytes ||
       a->scalarInput[1]>afterEvents.bytes-a->scalarInput[0])return kIOReturnBadArgument;
    bcopy(afterRecords+a->scalarInput[0],a->structureOutput,a->scalarInput[1]);return kIOReturnSuccess;
  }
  if(selector==27){
    if(!shape(0,0,512))return kIOReturnBadArgument;
    auto*out=static_cast<P::U64*>(a->structureOutput);bzero(out,512);const auto&r=bar1;
    out[0]=0x5254584241523231ULL;out[1]=1;out[2]=getRegistryEntryID();
    out[3]=r.validated;out[4]=r.attempted;out[5]=r.passed;out[6]=r.failure;out[7]=r.savedWords;
    out[8]=r.writtenWords;out[9]=r.checkedWords;out[10]=r.capturedBytes;out[11]=r.restoredWords;
    out[12]=r.reads;out[13]=r.writes;out[14]=r.ticks;out[15]=r.cleanupReads;out[16]=r.cleanupWrites;
    out[17]=r.windowChanges;out[18]=r.windowBefore;out[19]=r.windowCurrent;out[20]=r.windowAfter;
    out[21]=r.lastAddress;out[22]=r.lastValue;out[23]=r.expected;out[24]=r.pass;out[25]=r.failedWord;
    out[26]=r.startNs;out[27]=r.elapsedNs;out[28]=r.cleanupNs;out[29]=r.windowSaved;out[30]=r.modified;
    out[31]=r.originalRestored;out[32]=r.windowRestored;out[33]=r.cleanupAttempted;
    out[34]=GSPBar1::LeaseStart;out[35]=GSPBar1::LeaseEnd;out[36]=GSPBar1::Start;out[37]=GSPBar1::End;
    out[38]=GSPBar1::Bytes;out[39]=GSPBar1::CaptureBytes;out[40]=GSPBar1::BudgetNs;out[41]=GSPBar1::CleanupBudgetNs;
    out[42]=GSPBar1::MaxTicks;out[43]=bar1Claimed;out[44]=bar1LeaseOwned;out[45]=rm.passed;
    out[46]=unsigned(coordinator.phase());out[47]=pinned;out[48]=pci->configRead16(4);out[49]=coordinator.ledger().owned();
    out[50]=bar1Map&&bar1Map->mapped();out[51]=bar1Map?bar1Map->barBase:0;out[52]=bar1Map?bar1Map->physical():0;
    out[53]=GSPBar1::Start;out[54]=GSPBar1::Bytes;out[55]=0x4000000;return kIOReturnSuccess;
  }
  if(selector==28){
    if(!bar1Capture||a->scalarInputCount!=2||a->structureInputSize||!a->scalarInput[1]||a->scalarInput[1]>4096||
      a->structureOutputSize!=a->scalarInput[1]||a->scalarInput[0]>bar1.capturedBytes||a->scalarInput[1]>bar1.capturedBytes-a->scalarInput[0])return kIOReturnBadArgument;
    bcopy(bar1Capture+a->scalarInput[0],a->structureOutput,a->structureOutputSize);return kIOReturnSuccess;
  }
  if(selector==23){
    if(!shape(0,0,512))return kIOReturnBadArgument;
    auto*out=static_cast<P::U64*>(a->structureOutput);bzero(out,512);const auto&r=rm;
    out[0]=0x525458524d303139ULL;out[1]=1;out[2]=getRegistryEntryID();
    out[3]=r.validated;out[4]=r.attempted;out[5]=r.passed;out[6]=r.failure;out[7]=r.step;
    out[8]=r.completed;out[9]=r.sent;out[10]=r.doorbells;out[11]=r.count;out[12]=r.pages;out[13]=r.bytes;
    out[14]=r.txWriter;out[15]=r.txReader;out[16]=r.rxReader;out[17]=r.rxProducer;out[18]=r.rxSequence;
    out[19]=r.ticks;out[20]=r.polls;out[21]=r.imports;out[22]=r.reads;out[23]=r.writes;out[24]=r.publishes;
    out[25]=r.elapsedNs;out[26]=GSPComputePrep::BudgetNs;out[27]=GSPComputePrep::MaxTicks;
    out[28]=r.initialReader;out[29]=r.initialSequence;out[30]=r.consumerWrites;out[31]=r.prefixConsumed;
    out[32]=r.lastFunction;out[33]=r.lastResult;out[34]=r.lastParamStatus;out[35]=r.lastAddress;out[36]=r.lastValue;
    out[37]=GSPComputePrep::Client;out[38]=GSPComputePrep::RootObject;out[39]=GSPComputePrep::Device;out[40]=GSPComputePrep::Subdevice;
    out[41]=GSPComputePrep::RequestBytes;out[42]=GSPComputePrep::MaxRecords;out[43]=GSPComputePrep::MaxPages;
    out[44]=rmClaimed;out[45]=coordinator.ledger().owned();out[46]=sequenceWorkspaceOwned;out[47]=pinned;
    out[48]=pci->configRead16(4);out[49]=r.startNs;return kIOReturnSuccess;
  }
  if(selector==24){
    if(a->scalarInputCount!=2||a->structureInputSize||!a->scalarInput[1]||a->scalarInput[1]>GSPComputePrep::MaxRecords||
      a->structureOutputSize!=a->scalarInput[1]*72||a->scalarInput[0]>rm.count||a->scalarInput[1]>rm.count-a->scalarInput[0])return kIOReturnBadArgument;
    auto*out=static_cast<P::U64*>(a->structureOutput);
    for(unsigned i=0;i<a->scalarInput[1];++i){const auto&r=rm.records[a->scalarInput[0]+i];auto*row=out+9*i;
      row[0]=r.offset;row[1]=r.bytes;row[2]=r.function;row[3]=r.result;row[4]=r.sequence;
      row[5]=r.payload;row[6]=r.step;row[7]=r.slot;row[8]=r.elapsedUs;}
    return kIOReturnSuccess;
  }
  if(selector==25||selector==26){
    const unsigned total=selector==25?rm.bytes:GSPComputePrep::RequestBytes;const unsigned char*source=selector==25?rmRecords:rmRequests;
    if(a->scalarInputCount!=2||a->structureInputSize||!a->scalarInput[1]||a->scalarInput[1]>4096||
      a->structureOutputSize!=a->scalarInput[1]||!source||a->scalarInput[0]>total||a->scalarInput[1]>total-a->scalarInput[0])return kIOReturnBadArgument;
    bcopy(source+a->scalarInput[0],a->structureOutput,a->scalarInput[1]);return kIOReturnSuccess;
  }
  if(selector==29){
    if(!shape(0,0,512))return kIOReturnBadArgument;
    auto*out=static_cast<P::U64*>(a->structureOutput);bzero(out,512);const auto&r=pageTables;
    out[0]=0x5254585047543232ULL;out[1]=1;out[2]=getRegistryEntryID();
    out[3]=r.validated;out[4]=r.attempted;out[5]=r.passed;out[6]=r.failure;
    out[7]=r.inspected;out[8]=r.written;out[9]=r.checked;out[10]=r.captured;
    out[11]=r.reads;out[12]=r.writes;out[13]=r.ticks;out[14]=r.windowBefore;out[15]=r.windowAfter;
    out[16]=r.lastAddress;out[17]=r.lastValue;out[18]=r.expected;out[19]=r.failedWord;
    out[20]=r.startNs;out[21]=r.elapsedNs;out[22]=r.cleanupNs;out[23]=r.windowSaved;out[24]=r.modified;out[25]=r.windowRestored;
    out[26]=pageClaimed;out[27]=pageLeaseOwned;out[28]=pageMap&&pageMap->mapped();out[29]=pageMap?pageMap->barBase:0;out[30]=pageMap?pageMap->physical():0;
    out[31]=GSPPageTables::Start;out[32]=GSPPageTables::End;out[33]=GSPPageTables::Bytes;out[34]=GSPPageTables::RootBytes;
    out[35]=GSPPageTables::VirtualStart;out[36]=GSPPageTables::VirtualEnd;out[37]=unsigned(coordinator.phase());out[38]=pinned;
    out[39]=pci->configRead16(4);out[40]=coordinator.ledger().owned();out[41]=bar1.passed;out[42]=rm.passed;
    out[43]=pagePost.passed;out[44]=pagePost.words;out[45]=pagePost.bytes;out[46]=pagePost.failure;out[47]=pagePost.elapsedNs;
    out[48]=GSPPageTables::BudgetNs;out[49]=GSPPageTables::CleanupBudgetNs;out[50]=GSPPageTables::MaxTicks;
    out[51]=pdRmClaimed;out[52]=pdRm.passed;out[53]=pagePost.attempted;
    out[54]=r.passed&&r.windowRestored&&pdRm.passed&&pagePost.passed;return kIOReturnSuccess;
  }
  if(selector==30){
    if(!pageCapture||a->scalarInputCount!=3||a->structureInputSize||a->scalarInput[0]>1||!a->scalarInput[2]||
       a->scalarInput[2]>4096||a->structureOutputSize!=a->scalarInput[2])return kIOReturnBadArgument;
    const unsigned total=a->scalarInput[0]?pagePost.bytes:pageTables.captured;
    if(a->scalarInput[1]>total||a->scalarInput[2]>total-a->scalarInput[1])return kIOReturnBadArgument;
    bcopy(pageCapture+a->scalarInput[0]*GSPPageTables::Bytes+a->scalarInput[1],a->structureOutput,a->scalarInput[2]);return kIOReturnSuccess;
  }
  if(selector==31){
    if(!shape(0,0,512))return kIOReturnBadArgument;
    auto*out=static_cast<P::U64*>(a->structureOutput);bzero(out,512);const auto&r=pdRm;
    out[0]=0x5254585044523232ULL;out[1]=1;out[2]=getRegistryEntryID();
    out[3]=r.validated;out[4]=r.attempted;out[5]=r.passed;out[6]=r.failure;out[7]=r.step;
    out[8]=r.completed;out[9]=r.sent;out[10]=r.doorbells;out[11]=r.count;out[12]=r.pages;out[13]=r.bytes;
    out[14]=r.txWriter;out[15]=r.txReader;out[16]=r.rxReader;out[17]=r.rxProducer;out[18]=r.rxSequence;
    out[19]=r.ticks;out[20]=r.polls;out[21]=r.imports;out[22]=r.reads;out[23]=r.writes;out[24]=r.publishes;
    out[25]=r.elapsedNs;out[26]=GSPComputePrep::BudgetNs;out[27]=GSPComputePrep::MaxTicks;
    out[28]=r.initialReader;out[29]=r.initialSequence;out[30]=r.consumerWrites;out[31]=r.prefixConsumed;
    out[32]=r.lastFunction;out[33]=r.lastResult;out[34]=r.lastParamStatus;out[35]=r.lastAddress;out[36]=r.lastValue;
    out[37]=GSPComputePrep::Client;out[38]=GSPComputePrep::Vaspace;out[39]=GSPPageTables::Control;out[40]=GSPPageTables::ParamsBytes;
    out[41]=GSPComputePrep::Page;out[42]=GSPComputePrep::MaxRecords;out[43]=GSPComputePrep::MaxPages;
    out[44]=pdRmClaimed;out[45]=coordinator.ledger().owned();out[46]=sequenceWorkspaceOwned;out[47]=pinned;
    out[48]=pci->configRead16(4);out[49]=r.startNs;return kIOReturnSuccess;
  }
  if(selector==32){
    if(a->scalarInputCount!=2||a->structureInputSize||!a->scalarInput[1]||a->scalarInput[1]>GSPComputePrep::MaxRecords||
      a->structureOutputSize!=a->scalarInput[1]*72||a->scalarInput[0]>pdRm.count||a->scalarInput[1]>pdRm.count-a->scalarInput[0])return kIOReturnBadArgument;
    auto*out=static_cast<P::U64*>(a->structureOutput);
    for(unsigned i=0;i<a->scalarInput[1];++i){const auto&r=pdRm.records[a->scalarInput[0]+i];auto*row=out+9*i;
      row[0]=r.offset;row[1]=r.bytes;row[2]=r.function;row[3]=r.result;row[4]=r.sequence;
      row[5]=r.payload;row[6]=r.step;row[7]=r.slot;row[8]=r.elapsedUs;}
    return kIOReturnSuccess;
  }
  if(selector==33||selector==34){
    const unsigned total=selector==33?pdRm.bytes:GSPComputePrep::Page;const unsigned char*source=selector==33?pdRecords:pdRequest;
    if(a->scalarInputCount!=2||a->structureInputSize||!a->scalarInput[1]||a->scalarInput[1]>4096||
      a->structureOutputSize!=a->scalarInput[1]||!source||a->scalarInput[0]>total||a->scalarInput[1]>total-a->scalarInput[0])return kIOReturnBadArgument;
    bcopy(source+a->scalarInput[0],a->structureOutput,a->scalarInput[1]);return kIOReturnSuccess;
  }
  if(selector==35||selector==36){
    if(!shape(0,0,512))return kIOReturnBadArgument;
    ChannelMemory::Result empty;const auto &r=channelState?(selector==35?channelState->ring:channelState->contexts):empty;
    ChannelABI::memory(r,selector-35,channelOwner(),static_cast<P::U64*>(a->structureOutput));return kIOReturnSuccess;
  }
  if(selector==37){
    if(!shape(0,0,512))return kIOReturnBadArgument;
    ChannelABI::rm(channel,channelOwner(),channelState&&channelState->ring.passed,channelState&&channelState->contexts.passed,
      channelSnapshot.passed,static_cast<P::U64*>(a->structureOutput));return kIOReturnSuccess;
  }
  if(selector==38){
    const auto &r=channel.rpc;
    if(a->scalarInputCount!=2||a->structureInputSize||!a->scalarInput[1]||a->scalarInput[1]>GSPComputePrep::MaxRecords||
      a->structureOutputSize!=a->scalarInput[1]*72||a->scalarInput[0]>r.count||a->scalarInput[1]>r.count-a->scalarInput[0])return kIOReturnBadArgument;
    auto *out=static_cast<P::U64*>(a->structureOutput);
    for(unsigned i=0;i<a->scalarInput[1];++i){const auto &e=r.records[a->scalarInput[0]+i];auto *row=out+9*i;
      row[0]=e.offset;row[1]=e.bytes;row[2]=e.function;row[3]=e.result;row[4]=e.sequence;row[5]=e.payload;row[6]=e.step;row[7]=e.slot;row[8]=e.elapsedUs;}
    return kIOReturnSuccess;
  }
  if(selector==39||selector==40){
    const unsigned total=selector==39?channel.rpc.bytes:ChannelCodec::Steps*4096;
    const unsigned char *source=selector==39?channelRecords:channelRequests;
    if(a->scalarInputCount!=2||a->structureInputSize||!a->scalarInput[1]||a->scalarInput[1]>4096||a->structureOutputSize!=a->scalarInput[1]||
      !source||a->scalarInput[0]>total||a->scalarInput[1]>total-a->scalarInput[0])return kIOReturnBadArgument;
    bcopy(source+a->scalarInput[0],a->structureOutput,a->scalarInput[1]);return kIOReturnSuccess;
  }
  if(selector==41){
    if(!shape(0,0,1024))return kIOReturnBadArgument;
    ChannelABI::plan(channel.context,getRegistryEntryID(),static_cast<P::U64*>(a->structureOutput));return kIOReturnSuccess;
  }
  if(selector==42){
    if(a->scalarInputCount!=3||a->structureInputSize||a->scalarInput[0]>1||!a->scalarInput[2]||a->scalarInput[2]>4096||a->structureOutputSize!=a->scalarInput[2])return kIOReturnBadArgument;
    const unsigned total=a->scalarInput[0]?channelSnapshot.childBytes:channelSnapshot.rootBytes;
    const unsigned char *source=a->scalarInput[0]?channelChildCapture:channelRootCapture;
    if(!source||a->scalarInput[1]>total||a->scalarInput[2]>total-a->scalarInput[1])return kIOReturnBadArgument;
    bcopy(source+a->scalarInput[1],a->structureOutput,a->scalarInput[2]);return kIOReturnSuccess;
  }
  if(selector==43){
    if(!shape(0,0,512))return kIOReturnBadArgument;
    ChannelABI::snapshot(channelSnapshot,channelOwner(),static_cast<P::U64*>(a->structureOutput));return kIOReturnSuccess;
  }
  if(selector==44||selector==45){
    if(!shape(0,0,512))return kIOReturnBadArgument;
    ChannelMemory::Result empty;const auto &r=executionState?(selector==44?executionState->fixed:executionState->contexts):empty;
    ExecutionABI::memory(r,selector-44,executionOwner(),static_cast<P::U64*>(a->structureOutput));return kIOReturnSuccess;
  }
  if(selector==46){
    if(!shape(0,0,640))return kIOReturnBadArgument;
    ExecutionABI::rm(execution,executionOwner(),executionState?executionState->executionRepliesConsumed:0,static_cast<P::U64*>(a->structureOutput));return kIOReturnSuccess;
  }
  if(selector==47||selector==61){
    const auto &r=selector==47?execution.rpc:execution.external.rpc;
    if(a->scalarInputCount!=2||a->structureInputSize||!a->scalarInput[1]||a->scalarInput[1]>GSPComputePrep::MaxRecords||
      a->structureOutputSize!=a->scalarInput[1]*72||a->scalarInput[0]>r.count||a->scalarInput[1]>r.count-a->scalarInput[0])return kIOReturnBadArgument;
    auto *out=static_cast<P::U64*>(a->structureOutput);
    for(unsigned i=0;i<a->scalarInput[1];++i){const auto &e=r.records[a->scalarInput[0]+i];auto *row=out+9*i;
      row[0]=e.offset;row[1]=e.bytes;row[2]=e.function;row[3]=e.result;row[4]=e.sequence;row[5]=e.payload;row[6]=e.step;row[7]=e.slot;row[8]=e.elapsedUs;}
    return kIOReturnSuccess;
  }
  if(selector==48||selector==49||selector==54||selector==62||selector==63){
    const unsigned total=selector==48?execution.rpc.bytes:selector==49?ExecutionCodec::RequestBytes:selector==62?execution.external.rpc.bytes:selector==63?ExternalVAS::RequestBytes:executionCapture.bytes;
    const unsigned char *source=selector==48?executionRecords:selector==49?executionRequests:selector==62?externalRecords:selector==63?externalRequests:executionDeviceCapture;
    if(a->scalarInputCount!=2||a->structureInputSize||!a->scalarInput[1]||a->scalarInput[1]>4096||a->structureOutputSize!=a->scalarInput[1]||
      !source||a->scalarInput[0]>total||a->scalarInput[1]>total-a->scalarInput[0])return kIOReturnBadArgument;
    bcopy(source+a->scalarInput[0],a->structureOutput,a->scalarInput[1]);return kIOReturnSuccess;
  }
  if(selector==50){
    if(!shape(0,0,512))return kIOReturnBadArgument;
    ExecutionABI::plan(execution.context,channel.context,getRegistryEntryID(),static_cast<P::U64*>(a->structureOutput));return kIOReturnSuccess;
  }
  if(selector==51){
    if(a->scalarInputCount!=3||a->structureInputSize||a->scalarInput[0]>1||!a->scalarInput[2]||a->scalarInput[2]>4096||a->structureOutputSize!=a->scalarInput[2])return kIOReturnBadArgument;
    const unsigned total=a->scalarInput[0]?executionSnapshot.childBytes:executionSnapshot.rootBytes;
    const unsigned char *source=a->scalarInput[0]?executionChildCapture:executionRootCapture;
    if(!source||a->scalarInput[1]>total||a->scalarInput[2]>total-a->scalarInput[1])return kIOReturnBadArgument;
    bcopy(source+a->scalarInput[1],a->structureOutput,a->scalarInput[2]);return kIOReturnSuccess;
  }
  if(selector==52){
    if(!shape(0,0,512))return kIOReturnBadArgument;
    ExecutionABI::snapshot(executionSnapshot,executionOwner(),static_cast<P::U64*>(a->structureOutput));return kIOReturnSuccess;
  }
  if(selector==53){
    if(!shape(0,0,512))return kIOReturnBadArgument;
    ExecutionABI::fence(hostFence,executionOwner(),fenceState.claimed,fenceState.notified,fenceState.phase,static_cast<P::U64*>(a->structureOutput));return kIOReturnSuccess;
  }
  if(selector==55){
    if(!shape(0,0,256))return kIOReturnBadArgument;
    ExecutionABI::capture(executionCapture,executionOwner(),static_cast<P::U64*>(a->structureOutput));return kIOReturnSuccess;
  }
  if(selector>=64&&selector<=67)return computeMethod(selector,a);
  if(RtxReusableABI035::selector(selector))return runtimeMethod(selector,a);
  if(selector==60){
    if(!shape(0,0,512))return kIOReturnBadArgument;
    ExecutionABI::external(execution.external,executionOwner(),executionQueue.externalReplies,executionQueue.externalComplete,static_cast<P::U64*>(a->structureOutput));return kIOReturnSuccess;
  }
  // Diagnostics and explicit close are the only operations after exposure.
  if(coordinator.executionAttempted())return kIOReturnNotPermitted;
  if (stopping || isInactive() || pci->isInactive())
    return failRequest(P::Error::InvalidState, kIOReturnOffline);
  if (pci->configRead16(4) != 0) return failRequest(P::Error::TransportFailed, kIOReturnNotReady);
  if(selector==13){if(!shape(0,0,0))return failRequest(P::Error::TransportFailed);return launch();}
  if (selector == 0) {
    if (!shape(0, 0, 0)) return failRequest(P::Error::TransportFailed);
    if (session.state != P::State::Idle) return failRequest(P::Error::InvalidState);
    // Admit the complete caller-selected library exactly once, before provider
    // acquisition or DMA allocation. A failed bootstrap does not allow replay.
    if(shaderLibrary.consume(libraryScope())!=RtxLibraryUpload036::Error::None)return kIOReturnNotReady;
    if (pci->isOpen() || !pci->open(this)) return failRequest(P::Error::TransportFailed, kIOReturnExclusiveAccess);
    providerOpen = true;
    // Recheck identity/power/PCI state inside exclusive ownership. No MMIO.
    MacGSPIdentity identity(pci, this);
    const auto facts = identity.facts();
    if (Boot0::preflight(facts) != nullptr) {
      pci->close(this); providerOpen = false;
      return failRequest(P::Error::TransportFailed, kIOReturnNotReady);
    }
    dma = new MacGSPRuntimeDma(pci);
    const bool good = dma && dma->begin();
    const IOReturn error = dma ? dma->lastError : kIOReturnNoMemory;
    const P::Error pageError = dma ? dma->pageValidationError : P::Error::TransportFailed;
    session.begin(good ? P::Error::Ok : (pageError == P::Error::Ok ? P::Error::TransportFailed : pageError));
    if (!good) return failRequest(session.lastError(), error);
    context=new MacGSPContext(pci,this);
    sealScratch=static_cast<unsigned char *>(IOMalloc(P::Page));
    sealExpected=static_cast<unsigned char *>(IOMalloc(P::Page));
    pageScratch=static_cast<P::U64 *>(IOMalloc(P::TotalPages*sizeof(P::U64)));
    if(!context || !sealScratch || !sealExpected || !pageScratch)
      return failRequest(P::Error::TransportFailed,kIOReturnNoMemory);
    if(!context->map() || !context->capture() ||
       !coordinator.claim(context->identity,context->board,context->display,getRegistryEntryID(),pci->isOpen(this)))
      return failRequest(P::Error::TransportFailed,kIOReturnNotReady);
    // Extend this same exclusive idle-GPU software lease before FWSEC/GSP
    // exposure to cover the observed VRAM source of the canonical sequencer.
    // This is an ownership claim, not a fabricated OS VRAM allocation.
    static_assert(GSPSequencer::WorkspaceEnd==GSPLaunchOwnership::ReservedStart,"adjacent lease");
    static_assert(GSPSequencer::SourceEnd<=GSPSequencer::WorkspaceEnd,"DMA source inside lease");
    sequenceWorkspaceOwned=providerOpen && pci->isOpen(this) && context->command()==0 &&
      coordinator.phase()==GSPExecutionOwner::Phase::Uploading && context->vramMiB==6144;
    if(!sequenceWorkspaceOwned)return failRequest(P::Error::InvalidState,kIOReturnNotReady);
    publishState(); return kIOReturnSuccess;
  }
  if(selector==10) {
    if(!shape(0,0,0) || !coordinator.canUpload() || !context || !dma || !sealExpected)
      return failRequest(P::Error::InvalidState);
    for(unsigned page=1;page<=3;++page) {
      if(!GSPContentSeal::expectedPage(P::Queues,page,dma->pages(),context->system,sealExpected) ||
         !dma->copyIn(P::Queues,P::U64(page)*P::Page,sealExpected,P::Page) ||
         !session.write(P::Queues,P::U64(page)*P::Page,P::Page).ok())
        return failRequest(P::Error::TransportFailed);
    }
    startupPrepared=true;publishState();return kIOReturnSuccess;
  }
  if(selector==11) {
    if(!shape(0,0,0) || !startupPrepared || session.state!=P::State::Published ||
       !context || !context->capture() || !coordinator.freeze(getRegistryEntryID()))
      return failRequest(P::Error::InvalidState);
    // Import the publication before hashing. No firmware has been started.
    for(unsigned r=0;r<P::ResourceCount;++r) {
      const bool good=dma && dma->synchronizeOne(r,1);
      if(!session.sync(r,1,good).ok())return failRequest(P::Error::TransportFailed);
    }
    if(!GSPContentSeal::run(*this,dma->pages(),getRegistryEntryID(),context->system,
       sealScratch,sealExpected,sealResult) || !coordinator.accept(sealResult.seal))
      return failRequest(P::Error::TransportFailed);
    publishState();return kIOReturnSuccess;
  }
  // After seal, rejected mutation does not invalidate the successful seal or
  // implicitly close the owner. Explicit Finish/Abort remain available.
  if((selector==3 || selector==4 || (selector==8 && a->scalarInputCount==2 && a->scalarInput[1]==2)) &&
     !coordinator.canUpload())return kIOReturnNotPermitted;
  if (selector == 8) {
    if (!shape(2, 0, 0) || a->scalarInput[0] >= P::ResourceCount ||
        (a->scalarInput[1] != 1 && a->scalarInput[1] != 2)) return failRequest(P::Error::TransportFailed);
    const auto resource = P::U32(a->scalarInput[0]), direction = P::U32(a->scalarInput[1]);
    const auto checked = session.checkSync(resource, direction);
    if (checked != P::Error::Ok) return failRequest(checked);
    const bool good = dma && dma->synchronizeOne(resource, direction);
    const auto result = session.sync(resource, direction, good);
    if (!result.ok()) return failRequest(result.error, dma ? dma->lastError : kIOReturnNotReady);
    publishState(); return kIOReturnSuccess;
  }
  if (selector == 2) {
    if (a->scalarInputCount != 3 || a->structureInputSize || a->scalarInput[0] >= P::ResourceCount ||
        !a->scalarInput[2] || a->scalarInput[2] > 256 ||
        a->structureOutputSize != a->scalarInput[2] * sizeof(P::U64))
      return failRequest(P::Error::TransportFailed);
    const P::U32 resource = P::U32(a->scalarInput[0]);
    const P::U64 first = a->scalarInput[1], count = a->scalarInput[2];
    if ((session.state != P::State::Writing && session.state != P::State::Published) || !dma || !dma->ready() ||
        first >= P::Pages[resource] || count > P::Pages[resource] - first)
      return failRequest(P::Error::InvalidState);
    unsigned base = 0;
    for (unsigned i = 0; i < resource; ++i) base += P::Pages[i];
    bcopy(dma->pages() + base + first, a->structureOutput, static_cast<size_t>(count * sizeof(P::U64)));
    return kIOReturnSuccess;
  }
  if (selector == 3 || selector == 5) {
    const bool writing = selector == 3;
    if (a->scalarInputCount != (writing ? 2U : 3U) || a->scalarInput[0] >= P::ResourceCount ||
        (writing ? a->structureOutputSize != 0 : a->structureInputSize != 0))
      return failRequest(P::Error::TransportFailed);
    const P::U32 resource = P::U32(a->scalarInput[0]);
    const P::U64 offset = a->scalarInput[1];
    const P::U64 length = writing ? a->structureInputSize : a->scalarInput[2];
    if (!length || length > P::Page || (!writing && a->structureOutputSize != length))
      return failRequest(P::Error::InvalidLength);
    const P::Error checked = writing ? session.checkWrite(resource, offset, length) :
      session.checkRead(resource, offset, length);
    if (checked != P::Error::Ok) return failRequest(checked);
    const bool good = dma && (writing ? dma->copyIn(resource, offset, a->structureInput, P::U32(length)) :
      dma->copyOut(resource, offset, a->structureOutput, P::U32(length)));
    const auto result = writing ? session.write(resource, offset, length, good) : session.read(resource, offset, length, good);
    if (!result.ok()) return failRequest(result.error, dma ? dma->lastError : kIOReturnNotReady);
    return kIOReturnSuccess;
  }
  if (selector == 4 || selector == 6) {
    if (!shape(0, 0, 0)) return failRequest(P::Error::TransportFailed);
    const bool publishing = selector == 4;
    const P::Error checked = publishing ? session.checkPublish() : session.checkFinish();
    if (checked != P::Error::Ok) return failRequest(checked);
    if (publishing) {
      const bool good = dma && dma->synchronizeAll();
      const IOReturn error = dma ? dma->lastError : kIOReturnNotReady;
      const auto result = session.publish(good);
      if (!result.ok()) return failRequest(result.error, error);
    } else {
      const bool good = cleanupNative();
      session.finish(good);
      if (!good) { lastNativeError = dma ? dma->lastError : kIOReturnError; publishState(); return kIOReturnError; }
    }
    publishState(); return kIOReturnSuccess;
  }
  return failRequest(P::Error::TransportFailed, kIOReturnUnsupported);
}

IOReturn RTXProbe::dispatch(RTXGSPLaunchUserClient *client, UInt32 selector, IOExternalMethodArguments *args) {
  if (!mutex) return kIOReturnNotReady;
  // Sleepable mutex, not a command gate: IODMACommand prepare can allocate/wait.
  IOLockLock(mutex);
  const IOReturn result = client == activeClient ? methodLocked(selector, args) : kIOReturnNotOpen;
  IOLockUnlock(mutex);
  return result;
}
void RTXProbe::disconnect(RTXGSPLaunchUserClient *client, bool allowCleanup) {
  if (!mutex) return;
  IOLockLock(mutex);
  if (client == activeClient) {
    retireRuntime();
    bool good = !session.resourcesHeld();
    if (!good && allowCleanup) good = cleanupNative();
    if (!good && !pinned) { retain(); pinned = true; }
    shaderLibrary.close(libraryScope());
    session.abort(good); activeClient = nullptr; publishState();
  }
  IOLockUnlock(mutex);
}
bool RTXProbe::willTerminate(IOService *provider, IOOptionBits options) {
  if (!mutex) return IOService::willTerminate(provider, options);
  IOLockLock(mutex);
  stopping = true;
  retireRuntime();
  IOLockUnlock(mutex);
  // This is a notification, not a veto. Ownership is protected by the open
  // provider and didTerminate/close handshake below.
  return IOService::willTerminate(provider, options);
}
bool RTXProbe::didTerminate(IOService *provider, IOOptionBits options, bool *defer) {
  if (!mutex) return IOService::didTerminate(provider, options, defer);
  IOLockLock(mutex);
  stopping = true;
  retireRuntime();
  const bool brokenOwnership = (session.resourcesHeld() || pinned) && !providerOpen;
  if (brokenOwnership && !pinned) { retain(); pinned = true; }
  IOLockUnlock(mutex);
  if (brokenOwnership) { *defer = true; return true; }
  // Apple defers stop while provider->isOpen(this). Successful client-context
  // cleanup closes the provider, which resumes pending termination.
  *defer = false;
  return IOService::didTerminate(provider, options, defer);
}
void RTXProbe::stop(IOService *provider) {
  // All resource teardown belongs to explicit Finish/Abort/clientClose.
  // didTerminate and the provider open keep pending mappings alive until then.
  IOService::stop(provider);
}
bool RTXProbe::finalize(IOOptionBits options) {
  if (!mutex) return IOService::finalize(options);
  IOLockLock(mutex);
  stopping = true;
  retireRuntime();
  const bool held = providerOpen || session.resourcesHeld() || pinned;
  if (held) {
    session.fail(P::Error::CleanupFailed);
    lastNativeError = kIOReturnNotReady;
    if (!pinned) { retain(); pinned = true; }
    publishState();
  }
  IOLockUnlock(mutex);
  // The return value is not a veto. Skipping superclass finalize prevents its
  // scheduleStop/auto-close path from dropping retained mappings' ownership.
  return held ? false : IOService::finalize(options);
}
void RTXProbe::free() {
  if (dma && !pinned) { delete dma; dma = nullptr; }
  if(!pinned) {
    if(runtimeState){
      const auto &a=runtimeState->storage;
      if(a.captureRoot)IOFree(a.captureRoot,12288);
      if(a.captureChildren)IOFree(a.captureChildren,GMMULeaves::MaxChildBytes);
      if(a.captureDevice)IOFree(a.captureDevice,RtxReusableBacking035::DeviceBytes);
      delete runtimeState;
    }
    if(runtimeSnapshot.request)IOFree(runtimeSnapshot.request,RtxReusable035::WireBytes);
    if(runtimeSnapshot.plan)IOFree(runtimeSnapshot.plan,RtxReusableABI035::PlanBytes);
    if(computeState){
      const auto &s=computeState->storage;
      if(s.rootScratch)IOFree(s.rootScratch,12288);
      if(s.expectedScratch)IOFree(s.expectedScratch,GMMULeaves::MaxChildBytes);
      if(s.children)IOFree(s.children,GMMULeaves::MaxChildBytes);
      if(s.image)IOFree(s.image,ProgramMemory::Bytes);
      if(s.command)IOFree(s.command,32);
      if(s.scratch)IOFree(s.scratch,4096);
      delete computeState;
    }
    if(computeRootCapture)IOFree(computeRootCapture,ProgramCapture::RootBytes);
    if(computeChildCapture)IOFree(computeChildCapture,ProgramCapture::MaxChildren);
    if(computeDeviceCapture)IOFree(computeDeviceCapture,ProgramCapture::DeviceBytes);
    if(executionState){
      const auto &s=executionState->storage;
      if(s.rootScratch)IOFree(s.rootScratch,12288);
      if(s.expectedScratch)IOFree(s.expectedScratch,GMMULeaves::MaxChildBytes);
      if(s.fixedChildren)IOFree(s.fixedChildren,GMMULeaves::MaxChildBytes);
      if(s.fullChildren)IOFree(s.fullChildren,GMMULeaves::MaxChildBytes);
      if(s.scratch)IOFree(s.scratch,4096);
      delete executionState;
    }
    if(executionQueue.scratch)IOFree(executionQueue.scratch,4096);
    if(executionRecords)IOFree(executionRecords,GSPComputePrep::MaxBytes);
    if(executionRequests)IOFree(executionRequests,ExecutionCodec::RequestBytes);
    if(externalRecords)IOFree(externalRecords,GSPComputePrep::MaxBytes);
    if(externalRequests)IOFree(externalRequests,ExternalVAS::RequestBytes);
    if(executionRootCapture)IOFree(executionRootCapture,12288);
    if(executionChildCapture)IOFree(executionChildCapture,GMMULeaves::MaxChildBytes);
    if(executionDeviceCapture)IOFree(executionDeviceCapture,ExecutionCapture::Bytes);
    if(channelMap)delete channelMap;
    if(channelState){
      if(channelState->ringImage)IOFree(channelState->ringImage,8192);
      if(channelState->children)IOFree(channelState->children,GMMULeaves::MaxChildBytes);
      if(channelState->scratch)IOFree(channelState->scratch,4096);
      delete channelState;
    }
    if(channelRecords)IOFree(channelRecords,GSPComputePrep::MaxBytes);
    if(channelRequests)IOFree(channelRequests,ChannelCodec::Steps*GSPComputePrep::Page);
    if(channelRootCapture)IOFree(channelRootCapture,ChannelSnapshot::RootBytes);
    if(channelChildCapture)IOFree(channelChildCapture,ChannelSnapshot::MaxChildren);
    if(rom)delete rom;
    if(fwsec)delete fwsec;
    if(fwsecResult)delete fwsecResult;
    if(sec2){sec2->unmap();delete sec2;}
    if(sec2Result)delete sec2Result;
    if(firstRecord)IOFree(firstRecord,GSPEvents::MaxBytes);
    if(afterRecords)IOFree(afterRecords,GSPInitEvents::MaxBytes);
    if(pageMap)delete pageMap;
    if(pageCapture)IOFree(pageCapture,GSPPageTables::Bytes*2);
    if(pdRecords)IOFree(pdRecords,GSPComputePrep::MaxBytes);
    if(pdRequest)IOFree(pdRequest,GSPComputePrep::Page);
    if(bar1Map)delete bar1Map;
    if(bar1Original)IOFree(bar1Original,GSPBar1::Bytes);
    if(bar1Capture)IOFree(bar1Capture,GSPBar1::CaptureBytes);
    if(rmRecords)IOFree(rmRecords,GSPComputePrep::MaxBytes);
    if(rmRequests)IOFree(rmRequests,GSPComputePrep::RequestBytes);
    if(context){context->unmap();delete context;context=nullptr;}
    if(sealScratch)IOFree(sealScratch,P::Page);
    if(sealExpected)IOFree(sealExpected,P::Page);
    if(pageScratch)IOFree(pageScratch,P::TotalPages*sizeof(P::U64));
  }
  if (pci) { pci->release(); pci = nullptr; }
  if (mutex) { IOLockFree(mutex); mutex = nullptr; }
  IOService::free();
}

extern "C" kern_return_t _start(kmod_info_t *, void *) { return KERN_SUCCESS; }
extern "C" kern_return_t _stop(kmod_info_t *, void *) { return KERN_SUCCESS; }
KMOD_EXPLICIT_DECL(local.emre.RTXProbe, "0.35.0", _start, _stop)
