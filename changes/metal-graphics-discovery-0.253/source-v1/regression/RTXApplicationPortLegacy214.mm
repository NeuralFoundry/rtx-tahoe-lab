#import "RTXApplicationPort.h"
#import "RTXDeviceMemory107.h"
#import "RTXAcceleratorIdentity103.h"
#import "RTXApplicationClient.h"
#import "RTXDeviceConstruction.h"
#import "RTXApplicationRuntime115.h"
#import <objc/message.h>
#include "RTXLibraryCatalog187.hpp"
#include <atomic>
#include <mutex>
#include <unistd.h>
static const pid_t process=getpid();static char portKey;
static std::atomic<uint64_t> baseInitializations{0},deviceDeallocations{0},successes{0},retains{0},releases{0},identities{0},handshakes{0};
static thread_local unsigned errorCode;
@interface RTXApplication209_RTXApplicationPortLease049:NSObject {
 std::mutex _mutex;io_service_t _port;uint64_t _registry;
}
- (id)initWithPort:(io_service_t)port registry:(uint64_t)registry;
- (io_service_t)port;
- (uint64_t)registry;
- (void)close;
@end
@implementation RTXApplication209_RTXApplicationPortLease049
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
static bool identity(io_service_t port,RTXAccelerator103::Binding &value){
 if(!RTXReadAcceleratorBinding103(port,value))return false;
 ++identities;return true;
}
static NSData *bundleContainer(id device){
 NSBundle *bundle=[NSBundle bundleForClass:object_getClass(device)];NSString *path=[bundle pathForResource:@"selected" ofType:@"rtxlib"];
 if(!path)return nil;NSDictionary *attributes=[[NSFileManager defaultManager]attributesOfItemAtPath:path error:nil];
 if(![attributes[NSFileType]isEqual:NSFileTypeRegular]||[attributes[NSFileSize]unsignedLongLongValue]!=RTXCatalog187::Bytes)return nil;
 NSData *bytes=[NSData dataWithContentsOfFile:path options:0 error:nil];RTXCatalog187::Catalog catalog;
 return RTXCatalog187::decode(static_cast<const uint8_t *>(bytes.bytes),bytes.length,catalog)&&catalog.abi==2?bytes:nil;
}
uint64_t RTXApplicationRegistry049(id device){return [(RTXApplication209_RTXApplicationPortLease049 *)objc_getAssociatedObject(device,&portKey)registry];}
io_service_t RTXApplicationPort044(id device){return [(RTXApplication209_RTXApplicationPortLease049 *)objc_getAssociatedObject(device,&portKey)port];}
void RTXCloseApplicationPort044(id device){
 RTXCloseAcceleratorService(device);[(RTXApplication209_RTXApplicationPortLease049 *)objc_getAssociatedObject(device,&portKey)close];
}
void RTXApplicationDeviceWillDeallocate044(id device){RTXCloseApplicationPort044(device);++deviceDeallocations;}
extern "C" id RTXApplicationCopyService044(id device){return RTXCopyAcceleratorService(device);}
void RTXApplicationSetService044(id device,SEL,id service){
 NSDictionary *info=RTXCopyApplicationDeviceInfo(device);BOOL active=[info[@"phase"]unsignedIntValue]==3;[info release];
 if(getpid()!=process||!active||!RTXApplicationPort044(device)||!RTXAttachAcceleratorService(device,service))
  [NSException raise:NSInternalInconsistencyException format:@"RTX application accelerator attachment rejected"];
}
id RTXApplicationInitializeWithPort044(id device,SEL,io_service_t port){
 // Even rejected ports must complete base init before _MTLDevice.dealloc.
 struct objc_super base={device,objc_getClass("_MTLDevice")};
 device=reinterpret_cast<id(*)(struct objc_super *,SEL)>(objc_msgSendSuper)(&base,sel_registerName("init"));
 errorCode=0;if(!device){errorCode=1;return nil;}++baseInitializations;
 RTXApplication209_RTXApplicationPortLease049 *lease=nil;id<MTLLibrary> library=nil;id<MTLDevice> initialized=nil;NSDictionary *runtime=nil;
 @try {
  RTXAccelerator103::Binding binding;
  if(getpid()!=process||geteuid()==0)errorCode=2;
  else if(!identity(port,binding))errorCode=3;
  else {
   NSData *image=bundleContainer(device);
   if(!image)errorCode=4;
   else if(!(lease=[[RTXApplication209_RTXApplicationPortLease049 alloc]initWithPort:port registry:binding.childRegistry]))errorCode=5;
   else if(!RTXAttachDeviceMemory107(device,binding.memory,binding.parentGeneration))errorCode=8;
   else {
    // Same root-pinned, generation-pinned production transport as the explicit
    // factory. No IOServiceOpen, raw selector, firmware or MMIO path exists here.
    NSError *error=nil;
    runtime=[RTXPrepareApplicationRuntime115(device,binding,&error)retain];
    if(!runtime){errorCode=9;[lease release];[device release];return nil;}
    ++handshakes;
    id<MTLDevice> consumed=device;device=nil;
    initialized=RTXConsumeInitializedApplicationDevice044(consumed,[NSString stringWithUTF8String:RTXAccelerator103::BrokerService],image,binding.parentGeneration,&library,&error);
    if(!initialized)errorCode=6;
    else if(!RTXActivateApplicationRuntime115(initialized,runtime,[lease port],&error)){errorCode=10;RTXCloseApplicationDevice(initialized);[initialized release];initialized=nil;}
    else {objc_setAssociatedObject(initialized,&portKey,lease,OBJC_ASSOCIATION_RETAIN_NONATOMIC);++successes;}
   }
  }
 }@catch(NSException *exception){(void)exception;errorCode=7;[initialized release];initialized=nil;}
 [runtime release];[library release];[lease release];[device release];return initialized;
}
extern "C" NSDictionary *RTXApplicationPortInfo044(){
 return [@{@"base_initializations":@(baseInitializations.load()),@"device_deallocations":@(deviceDeallocations.load()),@"successes":@(successes.load()),@"port_retains":@(retains.load()),@"port_releases":@(releases.load()),@"identities":@(identities.load()),@"handshakes":@(handshakes.load()),@"thread_error":@(errorCode),@"process":@(getpid())}copy];
}

// Read-only catalog diagnostic; uses the same bundle path as the automatic constructor.
extern "C" NSData *RTXCopyApplicationBundleCatalog214(id device){return device?[bundleContainer(device)copy]:nil;}
