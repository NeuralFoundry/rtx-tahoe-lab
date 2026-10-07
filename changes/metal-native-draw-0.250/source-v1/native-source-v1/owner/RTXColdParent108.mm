#import "RTXColdParent108.h"
#import "RTXAcceleratorIdentity103.h"
#include "../probe/kernel/gpu242/GraphicsLayout242.hpp"
namespace {
bool zero(id value){
 if(![value isKindOfClass:[NSNumber class]]||CFGetTypeID((CFTypeRef)value)!=CFNumberGetTypeID()||CFNumberIsFloatType((CFNumberRef)value))return false;
 int64_t integer=1;return CFNumberGetValue((CFNumberRef)value,kCFNumberSInt64Type,&integer)&&integer==0;
}
class NativeReader final:public RTXColdParent108::Reader {
public:
 bool read(uint32_t handle,RTXColdParent108::Node &n)override{
  n={};uint64_t registry=0;if(IORegistryEntryGetRegistryEntryID(handle,&registry)!=KERN_SUCCESS||!registry)return false;
  CFMutableDictionaryRef p=nullptr;const auto status=IORegistryEntryCreateCFProperties(handle,&p,kCFAllocatorDefault,0);
  if(status!=KERN_SUCCESS||!p){if(p)CFRelease(p);return false;}
  struct DictionaryOwner{CFMutableDictionaryRef value;~DictionaryOwner(){CFRelease(value);}} owned{p};
  RTXDecodeColdParent108((NSDictionary *)p,n);n.entry.registry=registry;
  n.entry.isProbe=IOObjectConformsTo(handle,"RTXProbe");n.entry.isPCI=IOObjectConformsTo(handle,"IOPCIDevice");
  n.entry.isAccelerator=IOObjectConformsTo(handle,"IOAccelerator");n.entry.isChild=IOObjectConformsTo(handle,RTXAccelerator103::ChildClass);
  return true;
 }
 bool parent(uint32_t handle,uint32_t &owned)override{owned=0;return IORegistryEntryGetParentEntry(handle,kIOServicePlane,&owned)==KERN_SUCCESS;}
 void release(uint32_t handle)override{IOObjectRelease(handle);}
};
}
void RTXDecodeColdParent108(NSDictionary *p,RTXColdParent108::Node &n){
 n={};if(![p isKindOfClass:[NSDictionary class]])return;
 id abi=p[@"OwnedDataABI"];int64_t value=0;
 n.dataABI=[abi isKindOfClass:[NSNumber class]]&&CFGetTypeID((CFTypeRef)abi)==CFNumberGetTypeID()&&!CFNumberIsFloatType((CFNumberRef)abi)&&CFNumberGetValue((CFNumberRef)abi,kCFNumberSInt64Type,&value)&&value==181;
 abi=p[@"OwnedDispatchABI"];value=0;
 n.dispatchABI=[abi isKindOfClass:[NSNumber class]]&&CFGetTypeID((CFTypeRef)abi)==CFNumberGetTypeID()&&!CFNumberIsFloatType((CFNumberRef)abi)&&CFNumberGetValue((CFNumberRef)abi,kCFNumberSInt64Type,&value)&&value==183;
 abi=p[@"OwnedRootABI"];value=0;
 n.rootABI=[abi isKindOfClass:[NSNumber class]]&&CFGetTypeID((CFTypeRef)abi)==CFNumberGetTypeID()&&!CFNumberIsFloatType((CFNumberRef)abi)&&CFNumberGetValue((CFNumberRef)abi,kCFNumberSInt64Type,&value)&&value==RTXGraphicsLayout242::ABI;
 abi=p[@"OwnedProgramABI"];value=0;
 n.programABI=[abi isKindOfClass:[NSNumber class]]&&CFGetTypeID((CFTypeRef)abi)==CFNumberGetTypeID()&&!CFNumberIsFloatType((CFNumberRef)abi)&&CFNumberGetValue((CFNumberRef)abi,kCFNumberSInt64Type,&value)&&value==205;
 abi=p[@"OwnedProgramInputABI"];value=0;
 n.programInputABI=[abi isKindOfClass:[NSNumber class]]&&CFGetTypeID((CFTypeRef)abi)==CFNumberGetTypeID()&&!CFNumberIsFloatType((CFNumberRef)abi)&&CFNumberGetValue((CFNumberRef)abi,kCFNumberSInt64Type,&value)&&value==206;
 RTXDecodeAcceleratorProperties103(p,n.entry);n.numbersZero=n.flagsClear=true;
 for(NSString *key in @[@"GSPOwnerPhase",@"GSPFirmwareStartMask",@"GSPProgramCompleted",@"GSPShaderUploadPhase",@"GSPShaderUploadBytes",@"GSPShaderUploadError",@"GSPResidentProgramEpoch",@"GSPResidentProgramPhase"])
  if(!zero(p[key]))n.numbersZero=false;
 for(NSString *key in @[@"GSPExecutionAttempted",@"GSPDmaResourcesHeld",@"GSPDmaProviderOpen",@"GSPDmaPinnedUntilRestart",@"FirmwareStartAttempted",@"FirmwareExecuted",@"GSPResidentProgramReplaced",@"GSPHostFencePassed",@"GSPInitDoneObserved",@"RTXMetalVerified",@"OwnedRootAcknowledged",@"OwnedRootRuntimeEnabled"])
  if(p[key]!=(id)kCFBooleanFalse)n.flagsClear=false;
}
BOOL RTXReadColdParent108(io_service_t service,uint64_t &generation){
 NativeReader reader;return RTXColdParent108::validate(reader,service,generation)?YES:NO;
}
