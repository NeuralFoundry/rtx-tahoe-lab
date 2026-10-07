#import "RTXApplicationPort.h"
#import "RTXApplicationClient.h"
#import "RTXDeviceConstruction.h"
#import <objc/message.h>
#include "RTXLibraryContainer.hpp"
#include <atomic>
#include <mutex>
#include <unistd.h>
static const pid_t process=getpid();static char portKey;
static std::atomic<uint64_t> baseInitializations{0},deviceDeallocations{0},successes{0},retains{0},releases{0},identities{0},handshakes{0};
static thread_local unsigned errorCode;
@interface RTXApplicationPortLease049:NSObject {
 std::mutex _mutex;io_service_t _port;uint64_t _registry;
}
- (id)initWithPort:(io_service_t)port registry:(uint64_t)registry;
- (io_service_t)port;
- (uint64_t)registry;
- (void)close;
@end
@implementation RTXApplicationPortLease049
- (id)initWithPort:(io_service_t)port registry:(uint64_t)registry {
 if((self=[super init])){if(!port||IOObjectRetain(port)!=KERN_SUCCESS){[self release];return nil;}_port=port;_registry=registry;++retains;}return self;
}
- (uint64_t)registry {return _registry;}
- (io_service_t)port {std::lock_guard<std::mutex> lock(_mutex);return _port;}
- (void)close {
 io_service_t previous=0;{std::lock_guard<std::mutex> lock(_mutex);previous=_port;_port=0;}
 if(previous){IOObjectRelease(previous);++releases;}
}
- (void)dealloc {[self close];[super dealloc];}
@end
static NSDictionary *properties(io_registry_entry_t entry){
 CFMutableDictionaryRef value=nullptr;if(IORegistryEntryCreateCFProperties(entry,&value,kCFAllocatorDefault,0)!=KERN_SUCCESS)return nil;
 return [(NSDictionary *)value autorelease];
}
static bool word(NSDictionary *p,NSString *key,uint32_t expected){
 id value=p[key];return [value isKindOfClass:[NSData class]]&&[value length]==4&&RtxProgram033::get32(static_cast<const uint8_t *>([value bytes]))==expected;
}
// Each ancestor is owned locally even if a property query raises an exception.
struct RegistryOwner049 {
 io_registry_entry_t entry=0;
 RegistryOwner049()=default;
 RegistryOwner049(const RegistryOwner049 &)=delete;
 RegistryOwner049 &operator=(const RegistryOwner049 &)=delete;
 ~RegistryOwner049(){if(entry)IOObjectRelease(entry);}
 void adopt(io_registry_entry_t next){if(entry)IOObjectRelease(entry);entry=next;}
};
static bool identity(io_service_t port,uint64_t &childRegistry,uint64_t &parentGeneration){
 if(!port||!IOObjectConformsTo(port,"RTXMetalAccelerator048")||!IOObjectConformsTo(port,"IOAccelerator"))return false;
 NSDictionary *child=properties(port);
 if(![child[@"RTXMetalAcceleratorVersion"]isEqual:@"0.48.0"]||
    ![child[@"RTXMetalParentRegistryID"]isKindOfClass:[NSNumber class]])return false;
 if(IORegistryEntryGetRegistryEntryID(port,&childRegistry)!=KERN_SUCCESS||!childRegistry)return false;
 RegistryOwner049 cursor;
 if(IORegistryEntryGetParentEntry(port,kIOServicePlane,&cursor.entry)!=KERN_SUCCESS||!cursor.entry||
    !IOObjectConformsTo(cursor.entry,"RTXProbe"))return false;
 NSDictionary *probe=properties(cursor.entry);
 if(![probe[@"ProbeVersion"]isEqual:@"0.37.0"]||probe[@"ProbeComplete"]!=(id)kCFBooleanTrue)return false;
 if(IORegistryEntryGetRegistryEntryID(cursor.entry,&parentGeneration)!=KERN_SUCCESS||!parentGeneration||
    parentGeneration==childRegistry||[child[@"RTXMetalParentRegistryID"]unsignedLongLongValue]!=parentGeneration)return false;
 for(unsigned depth=0;depth<16;++depth){
  if(IOObjectConformsTo(cursor.entry,"IOPCIDevice")){
   NSDictionary *pci=properties(cursor.entry);
   if(!word(pci,@"vendor-id",0x10de)||!word(pci,@"device-id",0x2520)||
      !word(pci,@"subsystem-vendor-id",0x1043)||!word(pci,@"subsystem-id",0x104c))return false;
   ++identities;return true;
  }
  io_registry_entry_t parent=0;if(IORegistryEntryGetParentEntry(cursor.entry,kIOServicePlane,&parent)!=KERN_SUCCESS||!parent)break;
  cursor.adopt(parent);
 }
 return false;
}
static NSData *bundleContainer(id device){
 NSBundle *bundle=[NSBundle bundleForClass:object_getClass(device)];NSString *path=[bundle pathForResource:@"selected" ofType:@"rtxlib"];
 if(!path)return nil;NSDictionary *attributes=[[NSFileManager defaultManager]attributesOfItemAtPath:path error:nil];
 if(![attributes[NSFileType]isEqual:NSFileTypeRegular]||[attributes[NSFileSize]unsignedLongLongValue]!=RTXLibrary036::Bytes)return nil;
 NSData *bytes=[NSData dataWithContentsOfFile:path options:0 error:nil];RTXLibrary036::Catalog catalog;
 return RTXLibrary036::decode(static_cast<const uint8_t *>(bytes.bytes),bytes.length,catalog)?bytes:nil;
}
uint64_t RTXApplicationRegistry049(id device){return [(RTXApplicationPortLease049 *)objc_getAssociatedObject(device,&portKey)registry];}
io_service_t RTXApplicationPort044(id device){return [(RTXApplicationPortLease049 *)objc_getAssociatedObject(device,&portKey)port];}
void RTXCloseApplicationPort044(id device){
 RTXCloseAcceleratorService(device);[(RTXApplicationPortLease049 *)objc_getAssociatedObject(device,&portKey)close];
}
void RTXApplicationDeviceWillDeallocate044(id device){RTXCloseApplicationPort044(device);++deviceDeallocations;}
extern "C" id RTXApplicationCopyService044(id device){return RTXCopyAcceleratorService(device);}
void RTXApplicationSetService044(id device,SEL,id service){
 NSDictionary *info=RTXCopyApplicationDeviceInfo(device);BOOL active=[info[@"phase"]unsignedIntValue]==2;[info release];
 if(getpid()!=process||!active||!RTXApplicationPort044(device)||!RTXAttachAcceleratorService(device,service))
  [NSException raise:NSInternalInconsistencyException format:@"RTX application accelerator attachment rejected"];
}
id RTXApplicationInitializeWithPort044(id device,SEL,io_service_t port){
 // Even rejected ports must complete base init before _MTLDevice.dealloc.
 struct objc_super base={device,objc_getClass("_MTLDevice")};
 device=reinterpret_cast<id(*)(struct objc_super *,SEL)>(objc_msgSendSuper)(&base,sel_registerName("init"));
 errorCode=0;if(!device){errorCode=1;return nil;}++baseInitializations;
 RTXApplicationPortLease049 *lease=nil;id<MTLLibrary> library=nil;id<MTLDevice> initialized=nil;
 @try {
  uint64_t registry=0,parentGeneration=0;
  if(getpid()!=process||geteuid()==0)errorCode=2;
  else if(!identity(port,registry,parentGeneration))errorCode=3;
  else {
   NSData *image=bundleContainer(device);
   if(!image)errorCode=4;
   else if(!(lease=[[RTXApplicationPortLease049 alloc]initWithPort:port registry:registry]))errorCode=5;
   else {
    // Same root-pinned, generation-pinned production transport as the explicit
    // factory. No IOServiceOpen, raw selector, firmware or MMIO path exists here.
    NSError *error=nil;++handshakes;
    id<MTLDevice> consumed=device;device=nil;
    initialized=RTXConsumeInitializedApplicationDevice044(consumed,@"local.emre.RTXMetalBroker040.port049",image,parentGeneration,&library,&error);
    if(!initialized)errorCode=6;
    else {objc_setAssociatedObject(initialized,&portKey,lease,OBJC_ASSOCIATION_RETAIN_NONATOMIC);++successes;}
   }
  }
 }@catch(NSException *exception){(void)exception;errorCode=7;[initialized release];initialized=nil;}
 [library release];[lease release];[device release];return initialized;
}
extern "C" NSDictionary *RTXApplicationPortInfo044(){
 return [@{@"base_initializations":@(baseInitializations.load()),@"device_deallocations":@(deviceDeallocations.load()),@"successes":@(successes.load()),@"port_retains":@(retains.load()),@"port_releases":@(releases.load()),@"identities":@(identities.load()),@"handshakes":@(handshakes.load()),@"thread_error":@(errorCode),@"process":@(getpid())}copy];
}
