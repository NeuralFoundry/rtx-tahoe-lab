// RTXProbe 0.13: owned host buffers + IODMACommand mappings only.
// The sole MMIO operation is the already-tested read-only identity preflight.
#include <IOKit/IOUserClient.h>
#include <IOKit/IOLocks.h>
#include "MacGSPIdentity.hpp"
#include "MacRuntimeDma.hpp"
#include "RuntimeDmaProtocol.hpp"

namespace P = RuntimeDmaProtocol;
class RTXRuntimeDmaUserClient;
class RTXProbe : public IOService {
  OSDeclareDefaultStructors(RTXProbe)
  IOPCIDevice *pci = nullptr;
  IOLock *mutex = nullptr;
  MacRuntimeDma *dma = nullptr;
  RTXRuntimeDmaUserClient *activeClient = nullptr; // User client retains this service.
  P::Session session;
  GSPBooterPreflight::Snapshot fuse;
  bool preflightPassed = false, consumed = false, providerOpen = false, pinned = false, stopping = false;
  IOReturn lastNativeError = kIOReturnSuccess;
  bool cleanupNative(); // Called only with mutex held, in client context.
  void publishState();
  IOReturn failRequest(P::Error error, IOReturn nativeError = kIOReturnBadArgument);
  IOReturn methodLocked(UInt32 selector, IOExternalMethodArguments *args);
  void fillInfo(P::U64 *output);
public:
  bool start(IOService *provider) override;
  void stop(IOService *provider) override;
  void free() override;
  bool willTerminate(IOService *provider, IOOptionBits options) override;
  bool didTerminate(IOService *provider, IOOptionBits options, bool *defer) override;
  bool finalize(IOOptionBits options) override;
  IOReturn newUserClient(task_t task, void *securityID, UInt32 type,
    OSDictionary *properties, IOUserClient **handler) override;
  IOReturn dispatch(RTXRuntimeDmaUserClient *client, UInt32 selector, IOExternalMethodArguments *args);
  void disconnect(RTXRuntimeDmaUserClient *client, bool allowCleanup = true);
};

class RTXRuntimeDmaUserClient : public IOUserClient {
  OSDeclareDefaultStructors(RTXRuntimeDmaUserClient)
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
OSDefineMetaClassAndStructors(RTXRuntimeDmaUserClient, IOUserClient)

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
  setProperty("ProbeVersion", "0.13.0");
  setProperty("Mode", "runtime-dma-host-only");
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
  // Never free a potentially device-visible mapping if an unexpected agent
  // enabled PCI access. This probe has no path that enables bus mastering.
  if (pci->configRead16(4) != 0 || (dma && !dma->cleanup())) {
    if (!pinned) { retain(); pinned = true; }
    return false;
  }
  if (providerOpen) { pci->close(this); providerOpen = false; }
  return true;
}

IOReturn RTXProbe::failRequest(P::Error error, IOReturn nativeError) {
  lastNativeError = nativeError;
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
  auto *client = new RTXRuntimeDmaUserClient;
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
  if (selector == 7) {
    if (!shape(0, 0, 0)) return failRequest(P::Error::TransportFailed);
    const bool good = session.resourcesHeld() ? cleanupNative() : true;
    session.abort(good); publishState();
    return good ? kIOReturnSuccess : kIOReturnError;
  }
  if (stopping || isInactive() || pci->isInactive())
    return failRequest(P::Error::InvalidState, kIOReturnOffline);
  if (pci->configRead16(4) != 0) return failRequest(P::Error::TransportFailed, kIOReturnNotReady);
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
    dma = new MacRuntimeDma(pci);
    const bool good = dma && dma->begin();
    const IOReturn error = dma ? dma->lastError : kIOReturnNoMemory;
    const P::Error pageError = dma ? dma->pageValidationError : P::Error::TransportFailed;
    session.begin(good ? P::Error::Ok : (pageError == P::Error::Ok ? P::Error::TransportFailed : pageError));
    if (!good) return failRequest(session.lastError(), error);
    publishState(); return kIOReturnSuccess;
  }
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

IOReturn RTXProbe::dispatch(RTXRuntimeDmaUserClient *client, UInt32 selector, IOExternalMethodArguments *args) {
  if (!mutex) return kIOReturnNotReady;
  // Sleepable mutex, not a command gate: IODMACommand prepare can allocate/wait.
  IOLockLock(mutex);
  const IOReturn result = client == activeClient ? methodLocked(selector, args) : kIOReturnNotOpen;
  IOLockUnlock(mutex);
  return result;
}
void RTXProbe::disconnect(RTXRuntimeDmaUserClient *client, bool allowCleanup) {
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
  if (pci) { pci->release(); pci = nullptr; }
  if (mutex) { IOLockFree(mutex); mutex = nullptr; }
  IOService::free();
}

extern "C" kern_return_t _start(kmod_info_t *, void *) { return KERN_SUCCESS; }
extern "C" kern_return_t _stop(kmod_info_t *, void *) { return KERN_SUCCESS; }
KMOD_EXPLICIT_DECL(local.emre.RTXProbe, "0.13.0", _start, _stop)
