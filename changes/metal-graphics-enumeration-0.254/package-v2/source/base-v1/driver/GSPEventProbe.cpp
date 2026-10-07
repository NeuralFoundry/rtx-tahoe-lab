// RTXProbe 0.16: known bootstrap followed by bounded passive event collection.
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
  setProperty("ProbeVersion", "0.16.0");
  setProperty("Mode", "gsp-passive-events");
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
  setProperty("GSPEventQueueConsumed",false);
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
  return coordinator.cleanup(*this);
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
  if(!rom || !fwsecResult || !sec2Result || !firstRecord)return failRequest(P::Error::TransportFailed,kIOReturnNoMemory);
  bzero(firstRecord,GSPEvents::MaxBytes);
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
  if(events.passed && coordinator.firstStatus(gen))launchStatus=events.status;
  else {coordinator.fail();launchStatus="gsp-event-capture-failed-retained";}
  publishState();return events.passed?kIOReturnSuccess:kIOReturnError;
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
    return failRequest(P::Error::TransportFailed);
  const auto shape = [&](unsigned scalars, unsigned input, unsigned output) {
    return a->scalarInputCount == scalars && a->structureInputSize == input &&
      a->structureOutputSize == output;
  };
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
  // Diagnostics and explicit close are the only operations after exposure.
  if(coordinator.executionAttempted())return kIOReturnNotPermitted;
  if (stopping || isInactive() || pci->isInactive())
    return failRequest(P::Error::InvalidState, kIOReturnOffline);
  if (pci->configRead16(4) != 0) return failRequest(P::Error::TransportFailed, kIOReturnNotReady);
  if(selector==13){if(!shape(0,0,0))return failRequest(P::Error::TransportFailed);return launch();}
  if (selector == 0) {
    if (!shape(0, 0, 0)) return failRequest(P::Error::TransportFailed);
    if (session.state != P::State::Idle) return failRequest(P::Error::InvalidState);
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
    bool good = !session.resourcesHeld();
    if (!good && allowCleanup) good = cleanupNative();
    if (!good && !pinned) { retain(); pinned = true; }
    session.abort(good); activeClient = nullptr; publishState();
  }
  IOLockUnlock(mutex);
}
bool RTXProbe::willTerminate(IOService *provider, IOOptionBits options) {
  if (!mutex) return IOService::willTerminate(provider, options);
  IOLockLock(mutex);
  stopping = true;
  IOLockUnlock(mutex);
  // This is a notification, not a veto. Ownership is protected by the open
  // provider and didTerminate/close handshake below.
  return IOService::willTerminate(provider, options);
}
bool RTXProbe::didTerminate(IOService *provider, IOOptionBits options, bool *defer) {
  if (!mutex) return IOService::didTerminate(provider, options, defer);
  IOLockLock(mutex);
  stopping = true;
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
    if(rom)delete rom;
    if(fwsec)delete fwsec;
    if(fwsecResult)delete fwsecResult;
    if(sec2){sec2->unmap();delete sec2;}
    if(sec2Result)delete sec2Result;
    if(firstRecord)IOFree(firstRecord,GSPEvents::MaxBytes);
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
KMOD_EXPLICIT_DECL(local.emre.RTXProbe, "0.16.0", _start, _stop)
