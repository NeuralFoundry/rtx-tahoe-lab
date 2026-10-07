// The accelerator is initially discoverable by the root owner, but carries no
// Metal plugin name until an explicit, ready, generation-bound publication.
#include <IOKit/graphics/IOAccelerator.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOLocks.h>
#include <kern/task.h>
#include <libkern/c++/OSBoolean.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSSymbol.h>
#include "RTXAcceleratorIdentity103.hpp"
#include "RTXPublication200.hpp"
class RTXMetalAccelerator132:public IOAccelerator {
 OSDeclareDefaultStructors(RTXMetalAccelerator132)
 IOLock *publicationLock=nullptr;
 IOService *parentLease=nullptr;
 RTXPublication200::Controller publication;
 bool stopping=false;
 enum {Epoch,Session,Class,Name,Ready,PropertyCount};
 const OSSymbol *keys[PropertyCount]={};
 void hideLocked();
 struct Sink {
  RTXMetalAccelerator132 &service;OSObject **values;
  bool publish(const RTXPublication200::Request&){
   for(unsigned i=0;i<PropertyCount;++i)
    if(!values||!values[i]||!service.IORegistryEntry::setProperty(service.keys[i],values[i]))return false;
   return true;
  }
  void hide(){service.hideLocked();}
 };
public:
 bool start(IOService *)override;
 void stop(IOService *)override;
 void free()override;
 IOReturn setProperties(OSObject *)override;
 using IOAccelerator::newUserClient;
 IOReturn newUserClient(task_t,void *,UInt32,OSDictionary *,IOUserClient **handler)override{
  if(handler)*handler=nullptr;return kIOReturnUnsupported;
 }
};
OSDefineMetaClassAndStructors(RTXMetalAccelerator132,IOAccelerator)
namespace {
bool number(IOService *p,const char *key,uint64_t &value){OSObject *o=p->copyProperty(key);OSNumber *n=OSDynamicCast(OSNumber,o);if(n)value=n->unsigned64BitValue();if(o)o->release();return n!=nullptr;}
bool boolean(IOService *p,const char *key,bool expected=true){OSObject *o=p->copyProperty(key);OSBoolean *b=OSDynamicCast(OSBoolean,o);bool value=b&&(b->isTrue()==expected);if(o)o->release();return value;}
bool eligible(IOService *p){
 if(!p||p->isInactive())return false;
 uint64_t program=0,input=0;
 if(!number(p,"OwnedProgramABI",program)||program!=205||!number(p,"OwnedProgramInputABI",input)||input!=206)return false;
 RTXAccelerator103::Node n;n.registry=p->getRegistryEntryID();n.isProbe=p->metaCast("RTXProbe")!=nullptr;
 OSObject *o=p->copyProperty("ProbeVersion");OSString *v=OSDynamicCast(OSString,o);n.parentVersion=v&&v->isEqualTo(RTXAccelerator103::ParentVersion);if(o)o->release();
 n.complete=boolean(p,"ProbeComplete");n.passed=boolean(p,"ProbePassed");n.validTargets=number(p,"TargetIdentity",n.targetIdentity)&&number(p,"TargetSubsystem",n.targetSubsystem);
 o=p->copyProperty(RTXMemory107::Property);OSData *data=OSDynamicCast(OSData,o);
 if(data&&data->getLength()==RTXMemory107::Bytes)n.validMemory=RTXMemory107::decode(static_cast<const unsigned char *>(data->getBytesNoCopy()),data->getLength(),n.memory);
 if(o)o->release();return RTXAccelerator103::eligibleParent(n);
}
RTXPublication200::Observation observe(IOService *provider,uint64_t child){
 RTXPublication200::Observation o;o.child=child;o.parent=provider->getRegistryEntryID();o.eligible=eligible(provider);
 IORegistryEntry *ancestor=provider->copyParentEntry(gIOServicePlane);IOService *pci=OSDynamicCast(IOService,ancestor);
 o.providerOpen=boolean(provider,"GSPDmaProviderOpen")&&pci&&pci->metaCast("IOPCIDevice")&&pci->isOpen(provider);
 if(ancestor)ancestor->release();
 o.programReady=boolean(provider,"GSPProgramReady")&&boolean(provider,"GSPInitDoneObserved");
 o.hostFence=boolean(provider,"GSPHostFencePassed");number(provider,"GSPResidentProgramEpoch",o.epoch);
 number(provider,"OwnedRootABI",o.root.rootABI);number(provider,"HostBufferABI",o.root.hostABI);
 number(provider,"OwnedDataABI",o.root.dataABI);number(provider,"OwnedDispatchABI",o.root.dispatchABI);
 o.root.acknowledged=boolean(provider,"OwnedRootAcknowledged");o.root.runtimeEnabled=boolean(provider,"OwnedRootRuntimeEnabled");
 o.root.dispatchActive=boolean(provider,"OwnedDispatchActive");
 number(provider,"OwnedGraphicsABI",o.root.graphicsABI);
 o.root.graphicsActive=boolean(provider,"OwnedGraphicsActive");
 o.root.graphicsRetained=!boolean(provider,"OwnedGraphicsRetained",false);
 return o;
}
}
void RTXMetalAccelerator132::hideLocked(){
 // Removing the ready marker cannot fail due to allocation pressure. All
 // property writers, including stop(), use the same publication mutex.
 if(keys[Ready])IORegistryEntry::removeProperty(keys[Ready]);
 for(unsigned i=0;i<Ready;++i)if(keys[i])IORegistryEntry::removeProperty(keys[i]);
}
bool RTXMetalAccelerator132::start(IOService *provider){
 if(!eligible(provider))return false;publicationLock=IOLockAlloc();if(!publicationLock)return false;
 const char *names[]={"RTXMetalPublicationEpoch","RTXMetalPublicationSession","MetalPluginClassName","MetalPluginName","RTXMetalGPUReady"};
 for(unsigned i=0;i<PropertyCount;++i){keys[i]=OSSymbol::withCString(names[i]);if(!keys[i])return false;}
 if(!IOAccelerator::start(provider))return false;
 parentLease=provider;parentLease->retain();
 IOLockLock(publicationLock);hideLocked();IOLockUnlock(publicationLock);
 if(!setProperty("RTXMetalAcceleratorVersion",RTXAccelerator103::ChildVersion)||!setProperty("RTXMetalParentProbeVersion",RTXAccelerator103::ParentVersion)||!setProperty("RTXMetalParentRegistryID",provider->getRegistryEntryID(),64)||!setProperty("RTXMetalStage","awaiting-ready-publication")||!setProperty("RTXMetalPublicationABI",RTXPublication200::ABI,32)){
  IOAccelerator::stop(provider);return false;
 }
 registerService();return true;
}
IOReturn RTXMetalAccelerator132::setProperties(OSObject *properties){
 using namespace RTXPublication200;
 if(IOUserClient::clientHasPrivilege(current_task(),kIOClientPrivilegeAdministrator)!=kIOReturnSuccess)return kIOReturnNotPrivileged;
 OSDictionary *dictionary=OSDynamicCast(OSDictionary,properties);if(!dictionary||dictionary->getCount()!=1)return kIOReturnBadArgument;
 OSData *data=OSDynamicCast(OSData,dictionary->getObject(RequestProperty));Request request;
 if(!data||!decode(data->getBytesNoCopy(),data->getLength(),request))return kIOReturnBadArgument;
 if(!publicationLock)return kIOReturnNotReady;
 if(request.operation==Operation::Withdraw){
  Sink sink{*this,nullptr};IOLockLock(publicationLock);
  const Error result=publication.withdraw(sink,true,request);IOLockUnlock(publicationLock);
  return result==Error::Ok?kIOReturnSuccess:(result==Error::Busy?kIOReturnBusy:(result==Error::Identity?kIOReturnBadArgument:kIOReturnNotReady));
 }
 IOService *provider=parentLease;if(!provider)return kIOReturnNotReady;provider->retain();
 Observation o=observe(provider,getRegistryEntryID());
 IOLockLock(publicationLock);Error result=stopping||isInactive()?Error::Stopped:publication.begin(true,request,o);
 if(result==Error::Ok)retain();IOLockUnlock(publicationLock);
 if(result!=Error::Ok){provider->release();return result==Error::Busy?kIOReturnBusy:(result==Error::Identity?kIOReturnBadArgument:kIOReturnNotReady);}
 // Prepare values and observe the provider without the state mutex. Base
 // IORegistryEntry property-table updates are serialized with stop/withdraw;
 // no provider/framework/registration callback occurs while holding it.
 OSObject *values[PropertyCount]={OSNumber::withNumber(request.epoch,64),OSNumber::withNumber(request.session,64),OSString::withCString(PluginClass),OSString::withCString(PluginName),OSBoolean::withBoolean(true)};
 bool prepared=true;for(auto *value:values)if(!value)prepared=false;
 Observation now=observe(provider,getRegistryEntryID());
 const bool active=!isInactive()&&!provider->isInactive();Sink sink{*this,values};
 IOLockLock(publicationLock);bool committed=publication.commit(sink,prepared,!stopping&&active,now);IOLockUnlock(publicationLock);
 for(auto *value:values)if(value)value->release();
 // registerService can re-enter client code; it must remain outside the lock.
 // A concurrent stop/withdraw can remove properties here but cannot be undone.
 if(committed){registerService();IOLockLock(publicationLock);committed=publication.phase()==Phase::Published;IOLockUnlock(publicationLock);}
 provider->release();release();return committed?kIOReturnSuccess:kIOReturnNotReady;
}
void RTXMetalAccelerator132::stop(IOService *provider){
 if(publicationLock){Sink sink{*this,nullptr};IOLockLock(publicationLock);stopping=true;publication.stop(sink);IOLockUnlock(publicationLock);}
 IOAccelerator::stop(provider);
}
void RTXMetalAccelerator132::free(){if(parentLease){parentLease->release();parentLease=nullptr;}for(auto &key:keys)if(key){key->release();key=nullptr;}if(publicationLock){IOLockFree(publicationLock);publicationLock=nullptr;}IOAccelerator::free();}
extern "C" {
extern const unsigned long rtx_accelerator132_layout[]={sizeof(IOService),alignof(IOService),sizeof(IOAccelerator),alignof(IOAccelerator),sizeof(RTXMetalAccelerator132),alignof(RTXMetalAccelerator132)};
kern_return_t _start(kmod_info_t *,void *){return KERN_SUCCESS;}
kern_return_t _stop(kmod_info_t *,void *){return KERN_SUCCESS;}
}
KMOD_EXPLICIT_DECL(local.emre.RTXMetalAccelerator132,"0.253.0",_start,_stop)
