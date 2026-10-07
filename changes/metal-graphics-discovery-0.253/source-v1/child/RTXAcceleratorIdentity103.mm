#import "RTXAcceleratorIdentity103.h"
#include <cstring>
namespace {
bool integer(id value,uint64_t &output){
 if(![value isKindOfClass:[NSNumber class]]||CFGetTypeID((CFTypeRef)value)!=CFNumberGetTypeID()||CFNumberIsFloatType((CFNumberRef)value))return false;
 int64_t signedValue=0;
 if(!CFNumberGetValue((CFNumberRef)value,kCFNumberSInt64Type,&signedValue)||signedValue<0)return false;
 output=uint64_t(signedValue);return true;
}
bool text(id value,const char *expected){return [value isKindOfClass:[NSString class]]&&[value isEqualToString:[NSString stringWithUTF8String:expected]];}
bool pciword(id value,uint32_t &out){
 if(![value isKindOfClass:[NSData class]]||[value length]!=4)return false;
 const uint8_t *p=static_cast<const uint8_t *>([value bytes]);out=uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);return true;
}
class NativeReader final:public RTXAccelerator103::Reader {
public:
 bool read(uint32_t handle,RTXAccelerator103::Node &node)override{
  node={};uint64_t registry=0;
  if(IORegistryEntryGetRegistryEntryID(handle,&registry)!=KERN_SUCCESS||!registry)return false;
  CFMutableDictionaryRef raw=nullptr;
  kern_return_t status=IORegistryEntryCreateCFProperties(handle,&raw,kCFAllocatorDefault,0);
  if(status!=KERN_SUCCESS||!raw){if(raw)CFRelease(raw);return false;}
  // Own the property dictionary across Objective-C exceptions as well.
  struct DictionaryOwner{CFMutableDictionaryRef p;~DictionaryOwner(){CFRelease(p);}} owner{raw};
  RTXDecodeAcceleratorProperties103((NSDictionary *)raw,node);node.registry=registry;
  node.isChild=IOObjectConformsTo(handle,RTXAccelerator103::ChildClass);
  node.isAccelerator=IOObjectConformsTo(handle,"IOAccelerator");
  node.isProbe=IOObjectConformsTo(handle,"RTXProbe");node.isPCI=IOObjectConformsTo(handle,"IOPCIDevice");return true;
 }
 bool parent(uint32_t handle,uint32_t &owned)override{owned=0;return IORegistryEntryGetParentEntry(handle,kIOServicePlane,&owned)==KERN_SUCCESS;}
 void release(uint32_t handle)override{IOObjectRelease(handle);}
};
}
void RTXDecodeAcceleratorProperties103(NSDictionary *properties,RTXAccelerator103::Node &node){
 using namespace RTXAccelerator103;node={};
 if(![properties isKindOfClass:[NSDictionary class]])return;
 node.childVersion=text(properties[@"RTXMetalAcceleratorVersion"],ChildVersion);
 node.advertisedParentVersion=text(properties[@"RTXMetalParentProbeVersion"],ParentVersion);
 node.validParentRegistry=integer(properties[@"RTXMetalParentRegistryID"],node.parentRegistry)&&node.parentRegistry>0;
 node.parentVersion=text(properties[@"ProbeVersion"],ParentVersion);
 integer(properties[@"OwnedRootABI"],node.root.rootABI);integer(properties[@"HostBufferABI"],node.root.hostABI);
 node.root.acknowledged=properties[@"OwnedRootAcknowledged"]==(id)kCFBooleanTrue;
 node.root.runtimeEnabled=properties[@"OwnedRootRuntimeEnabled"]==(id)kCFBooleanTrue;
 integer(properties[@"OwnedDataABI"],node.root.dataABI);integer(properties[@"OwnedDispatchABI"],node.root.dispatchABI);
 node.root.dispatchActive=properties[@"OwnedDispatchActive"]==(id)kCFBooleanTrue;
 integer(properties[@"OwnedGraphicsABI"],node.root.graphicsABI);
 node.root.graphicsActive=properties[@"OwnedGraphicsActive"]==(id)kCFBooleanTrue;
 node.root.graphicsRetained=properties[@"OwnedGraphicsRetained"]!=(id)kCFBooleanFalse;
 node.providerOpen=properties[@"GSPDmaProviderOpen"]==(id)kCFBooleanTrue;
 node.programReady=properties[@"GSPProgramReady"]==(id)kCFBooleanTrue;
 node.hostFence=properties[@"GSPHostFencePassed"]==(id)kCFBooleanTrue;
 node.initDone=properties[@"GSPInitDoneObserved"]==(id)kCFBooleanTrue;
 integer(properties[@"GSPResidentProgramEpoch"],node.programEpoch);
 integer(properties[@"RTXMetalPublicationABI"],node.publicationABI);
 integer(properties[@"RTXMetalPublicationEpoch"],node.publicationEpoch);
 integer(properties[@"RTXMetalPublicationSession"],node.publicationSession);
 node.pluginIdentity=text(properties[@"MetalPluginName"],RTXPublication200::PluginName)&&text(properties[@"MetalPluginClassName"],RTXPublication200::PluginClass);
 node.published=properties[@"RTXMetalGPUReady"]==(id)kCFBooleanTrue;
 id memory=properties[@"ProbeMemoryEvidence107"];
 if([memory isKindOfClass:[NSData class]]&&[memory length]==RTXMemory107::Bytes)node.validMemory=RTXMemory107::decode(static_cast<const unsigned char *>([memory bytes]),RTXMemory107::Bytes,node.memory);
 node.complete=properties[@"ProbeComplete"]==(id)kCFBooleanTrue;node.passed=properties[@"ProbePassed"]==(id)kCFBooleanTrue;
 node.validTargets=integer(properties[@"TargetIdentity"],node.targetIdentity)&&integer(properties[@"TargetSubsystem"],node.targetSubsystem);
 node.validPCI=pciword(properties[@"vendor-id"],node.vendor)&&pciword(properties[@"device-id"],node.device)&&pciword(properties[@"subsystem-vendor-id"],node.subvendor)&&pciword(properties[@"subsystem-id"],node.subdevice);
}
BOOL RTXReadAcceleratorBinding103(io_service_t port,RTXAccelerator103::Binding &value){
 NativeReader reader;return RTXAccelerator103::binding(reader,port,value)?YES:NO;
}
