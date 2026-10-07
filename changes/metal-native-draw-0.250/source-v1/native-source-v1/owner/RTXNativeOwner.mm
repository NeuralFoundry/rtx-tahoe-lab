#import "RTXNativeMetal.h"
#import "RTXColdParent108.h"
#import "RTXDeviceConstruction.h"
#import "RTXDeviceLimits.h"
#import "RTXDeviceFeatures.h"
#import "RTXNativeBroker.h"
#import "RTXMetalCommand.h"
#import "RTXNativeResident.h"
#import <IOKit/IOKitLib.h>
#import <objc/message.h>
#import <objc/runtime.h>
#include "ConnectionLease.hpp"
#include "RTXLibraryContainer.hpp"
#include "NativeCommandSession.hpp"
#include "NativeDataSession182.hpp"
#include "NativeGraphicsSession243.hpp"
#include "RTXNativeGraphics243.h"
#ifndef RTX_GRAPHICS242
#error Graphics owner243 requires the matching root242 kernel configuration
#endif
#include "NativeOwnedDispatch185.hpp"
#import "RTXOwnedDispatch185.h"
#import "RTXStandardOwned187.h"
#import "RTXNativeOwnedBroker188.h"
#include "OwnedCommand208.hpp"
#include "ProgramAdmission208.hpp"
#import "RTXMappedBufferTransfer182.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <time.h>
#include <unistd.h>

namespace E=RTXNativeEvidence036;namespace S=RTXNativeSession036;namespace P=E::P;
static const pid_t process=getpid();static std::mutex ownerMutex;static RTXProgramAdmission208::Gate admission208;
static std::atomic<uint64_t> opens{0},closes{0},releases{0},destroyed{0},calls{0},lastSelector{0};
static std::atomic<bool> graphicsClaimed243{false};
static bool brokerClaimed=false,ownedClaimed185=false,ownedStandard187=false;
static uint64_t attempts=0,registry=0;static uint32_t ownerPhase=RTXNativeCold,openResult=0,closeResult=0;
static std::atomic<uint64_t> baseInitializations{0},deviceDeallocations{0},constructionSuccesses{0};
static std::atomic<bool> connectionAttempted{false};static thread_local uint32_t constructionError=0;
static Class deviceBase;static char stateKey;static id<MTLDevice> ownerDevice;static id<MTLLibrary> ownerLibrary;
struct IOKitOps {
 int retain(uint32_t service){return IOObjectRetain(service);}
 int open(uint32_t service,uint32_t *connection){++opens;return IOServiceOpen(service,mach_task_self(),0,connection);}
 int close(uint32_t connection){++closes;return IOServiceClose(connection);}
 void release(uint32_t service){++releases;IOObjectRelease(service);}
};
@interface RTXNativeConnection108 : NSObject {
 @public std::mutex mutex;IOKitOps io;std::unique_ptr<ConnectionLease<IOKitOps>> lease;
 std::unique_ptr<RTXNativeRoot196::Capture> rootEvidence;
 std::unique_ptr<RTXNativeData182::Session> dataSession;
 std::unique_ptr<RTXNativeDispatch205::Session> dispatchSession;
 std::unique_ptr<RTXNativeGraphics243::Session> graphicsSession;
 std::array<uint8_t,4608>ownedPayload187;
 std::unique_ptr<S::Session> session;std::array<uint8_t,4608> payload;uint64_t generation;
 RtxLibraryUpload036::State upload;BOOL closed,writeFailed,armAttempted,armed;
 NSString *directory;uint32_t lastResult,firstError;
}
- (uint32_t)call:(unsigned)selector scalars:(const uint64_t *)scalars count:(unsigned)count input:(const void *)input bytes:(size_t)bytes output:(void *)output capacity:(size_t)capacity;
@end
@implementation RTXNativeConnection108
- (id)init{if((self=[super init])){ownedPayload187.fill(0);lease=std::make_unique<ConnectionLease<IOKitOps>>(io);session=std::make_unique<S::Session>();rootEvidence=std::make_unique<RTXNativeRoot196::Capture>();dataSession=std::make_unique<RTXNativeData182::Session>();dispatchSession=std::make_unique<RTXNativeDispatch205::Session>();graphicsSession=std::make_unique<RTXNativeGraphics243::Session>();}return self;}
- (void)dealloc{lease.reset();session.reset();[directory release];++destroyed;[super dealloc];}
- (uint32_t)call:(unsigned)selector scalars:(const uint64_t *)scalars count:(unsigned)count input:(const void *)input bytes:(size_t)bytes output:(void *)output capacity:(size_t)capacity {
 if(closed||!lease->connection())return lastResult=RTX_NATIVE_NOT_OPEN;
 ++calls;lastSelector=selector;size_t actual=capacity;uint32_t scalarOutputs=0;
 uint32_t code=IOConnectCallMethod(lease->connection(),selector,scalars,count,input,bytes,nullptr,&scalarOutputs,output,&actual);
 if(!code&&(actual!=capacity||scalarOutputs))code=RTX_NATIVE_IO_SHAPE;if(code&&!firstError)firstError=code;return lastResult=code;
}
@end
static RTXNativeConnection108 *connection(id device){return (RTXNativeConnection108 *)objc_getAssociatedObject(device,&stateKey);}
static bool identity(io_service_t service,uint64_t &identifier){return RTXReadColdParent108(service,identifier);}
static uint64_t deviceRegistry(id device,SEL){auto *s=connection(device);return s?s->generation:0;}
static io_service_t devicePort(id device,SEL){auto *s=connection(device);return s?s->lease->service():0;}
static NSString *deviceName(id,SEL){return @"RTX 3060 experimental compute";}
static id deviceInitialize(id device,SEL,io_service_t service){
 // _MTLDevice.dealloc expects queues installed by its base initializer, even
 // when the port or caller is rejected. Never release a merely allocated base.
 struct objc_super super={device,deviceBase};
 device=reinterpret_cast<id(*)(struct objc_super *,SEL)>(objc_msgSendSuper)(&super,sel_registerName("init"));
 constructionError=0;if(!device){constructionError=RTX_NATIVE_EXCEPTION;return nil;}++baseInitializations;
 RTXNativeConnection108 *state=nil;
 @try {
  uint64_t identifier=0;
  if(getpid()!=process)constructionError=RTX_NATIVE_PROCESS;
  else if(geteuid()!=0)constructionError=uint32_t(kIOReturnNotPrivileged);
  else if(!identity(service,identifier))constructionError=RTX_NATIVE_EVIDENCE;
  else if(connectionAttempted.exchange(true))constructionError=RTX_NATIVE_STATE;
  else {
   state=[[RTXNativeConnection108 alloc]init];
   if(!state)constructionError=RTX_NATIVE_EXCEPTION;
   else {
    state->generation=identifier;
    if(!state->lease->open(service))constructionError=state->lease->openResult?uint32_t(state->lease->openResult):RTX_NATIVE_NOT_OPEN;
    else {
     std::array<uint8_t,1280> info{};
     uint32_t result=[state call:1 scalars:nullptr count:0 input:nullptr bytes:0 output:info.data()capacity:info.size()];
     if(result)constructionError=result;
     else if(P::get64(info.data())!=UINT64_C(0x525458444d413133)||P::get64(info.data()+8)!=1||P::get64(info.data()+16)!=0||P::get64(info.data()+11*8)!=identifier||P::get64(info.data()+12*8)||P::get64(info.data()+13*8))constructionError=RTX_NATIVE_EVIDENCE;
     else {
      std::array<uint8_t,512> rootInfo{};
      uint32_t rootResult=[state call:85 scalars:nullptr count:0 input:nullptr bytes:0 output:rootInfo.data()capacity:rootInfo.size()];
      if(rootResult||!RTXNativeRoot196::cold(rootInfo.data(),rootInfo.size(),identifier)){constructionError=rootResult?rootResult:RTX_NATIVE_EVIDENCE;[state release];[device release];return nil;}
      std::array<uint8_t,128> dispatchInfo{},expectedDispatch{};
      P::Q::put64(expectedDispatch.data(),RTXNativeDispatch185::Magic);P::Q::put64(expectedDispatch.data()+8,183);P::Q::put64(expectedDispatch.data()+16,identifier);
      uint32_t dispatchResult=[state call:101 scalars:nullptr count:0 input:nullptr bytes:0 output:dispatchInfo.data()capacity:dispatchInfo.size()];
      if(dispatchResult||dispatchInfo!=expectedDispatch){constructionError=dispatchResult?dispatchResult:RTX_NATIVE_EVIDENCE;[state release];[device release];return nil;}
      std::array<uint8_t,64> programInfo{},expectedProgram{};
      const uint64_t programWords[]={RTXProgram205::Magic,205,64,identifier,0,0,0,0};
      for(unsigned i=0;i<8;++i)P::Q::put64(expectedProgram.data()+i*8,programWords[i]);
      uint32_t programResult=[state call:108 scalars:nullptr count:0 input:nullptr bytes:0 output:programInfo.data()capacity:programInfo.size()];
      if(programResult||programInfo!=expectedProgram){constructionError=programResult?programResult:RTX_NATIVE_EVIDENCE;[state release];[device release];return nil;}
      objc_setAssociatedObject(device,&stateKey,state,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
      [state release];++constructionSuccesses;return device;
     }
    }
   }
  }
 }@catch(NSException *exception){(void)exception;constructionError=RTX_NATIVE_EXCEPTION;}
 [state release];[device release];return nil;
}
static void deviceSetAccelerator(id device,SEL,id service){
 auto *state=connection(device);BOOL live=NO;
 if(state){std::lock_guard<std::mutex> lock(state->mutex);live=!state->closed&&state->lease->connection()!=0;}
 if(!live||!RTXAttachAcceleratorService(device,service))
  [NSException raise:NSInternalInconsistencyException format:@"RTX accelerator service attachment rejected"];
}
static void deviceDeallocate(id device,SEL selector){
 // Release framework service ownership while the device is still intact.
 // The measured Metal service holds a weak device reference, not a strong cycle.
 RTXCloseAcceleratorService(device);
 auto *state=connection(device);
 if(state){std::lock_guard<std::mutex> lock(state->mutex);state->closed=YES;state->session->retire();state->lease->close();}
 RTXCloseDeviceFeatures(device);
 ++deviceDeallocations;struct objc_super super={device,deviceBase};
 reinterpret_cast<void(*)(struct objc_super *,SEL)>(objc_msgSendSuper)(&super,selector);
}
static Class makeClass(Class supplied=Nil){
 // A static bundle class has image metadata that NSBundle.classNamed needs.
 // Adopt only a direct _MTLDevice subclass; ordinary factory users retain the
 // dynamic-class path. Once selected, the class cannot be replaced.
 if(supplied&&(class_getSuperclass(supplied)!=objc_getClass("_MTLDevice")||objc_getClass(class_getName(supplied))!=supplied))return Nil;
 static std::once_flag once;static Class result=Nil;
 std::call_once(once,[supplied]{
  deviceBase=objc_getClass("_MTLDevice");if(!deviceBase)return;
  // Preserve the existing installers' unpublished-class contract. Validate
  // all production IMPs on a temporary class before attaching them to the
  // empty image-backed class during its synchronized +initialize.
  if(supplied){
   unsigned directCount=0;Method *direct=class_copyMethodList(supplied,&directCount);free(direct);
   if(directCount||class_getInstanceSize(supplied)!=class_getInstanceSize(deviceBase))return;
  }
  Class cls=objc_allocateClassPair(deviceBase,supplied?"RTXBundleMethodStage108":"RTXNativeCommandDevice108",0);if(!cls)return;
  struct Entry{const char *name;IMP imp;const char *encoding;};const Entry entries[]={
   {"initWithAcceleratorPort:",reinterpret_cast<IMP>(deviceInitialize),"@20@0:8I16"},
   {"_setAcceleratorService:",reinterpret_cast<IMP>(deviceSetAccelerator),"v24@0:8@16"},
   {"dealloc",reinterpret_cast<IMP>(deviceDeallocate),"v16@0:8"},
   {"registryID",reinterpret_cast<IMP>(deviceRegistry),"Q16@0:8"},
   {"acceleratorPort",reinterpret_cast<IMP>(devicePort),"I16@0:8"},
   {"name",reinterpret_cast<IMP>(deviceName),"@16@0:8"}};
  bool ok=RTXInstallLibraryMethods(cls)&&RTXInstallBufferMethods(cls)&&RTXInstallCommandMethods(cls)&&RTXInstallDeviceLimits(cls)&&RTXInstallDeviceFeatures(cls);
  for(const auto &e:entries)ok=ok&&class_addMethod(cls,sel_registerName(e.name),e.imp,e.encoding);
  if(!ok){objc_disposeClassPair(cls);return;}
  if(supplied){
   unsigned count=0;Method *methods=class_copyMethodList(cls,&count);ok=count==15+RTXDeviceLimitMethodCount()+RTXDeviceFeatureMethodCount();
   for(unsigned i=0;i<count&&ok;++i)ok=class_addMethod(supplied,method_getName(methods[i]),method_getImplementation(methods[i]),method_getTypeEncoding(methods[i]));
   free(methods);objc_disposeClassPair(cls);if(!ok)return;result=supplied;
  }else{objc_registerClassPair(cls);result=cls;}
 });return supplied&&result!=supplied?Nil:result;
}
BOOL RTXUseNativeDeviceClass(Class cls){return cls&&makeClass(cls)==cls;}
Class RTXNativeDeviceClass(){return makeClass();}
NSDictionary *RTXCopyNativeConstructionInfo(){
 return [@{@"base_initializations":@(baseInitializations.load()),@"device_deallocations":@(deviceDeallocations.load()),
  @"construction_successes":@(constructionSuccesses.load()),@"connection_attempted":@(connectionAttempted.load()),
  @"thread_error":@(constructionError),@"native_version":@"0.250.0",@"probe_version":[NSString stringWithUTF8String:RTXAccelerator103::ParentVersion]}copy];
}
static io_service_t discover(){
 io_iterator_t iterator=0;if(IOServiceGetMatchingServices(kIOMainPortDefault,IOServiceMatching("RTXProbe"),&iterator)!=KERN_SUCCESS)return 0;
 io_service_t selected=0;unsigned count=0;
 for(unsigned i=0;i<17;++i){io_service_t service=IOIteratorNext(iterator);if(!service)break;++count;if(!selected)selected=service;else IOObjectRelease(service);}
 IOObjectRelease(iterator);if(count!=1){if(selected)IOObjectRelease(selected);return 0;}return selected;
}
static uint64_t nowNs(){struct timespec ts={};if(clock_gettime(CLOCK_MONOTONIC_RAW,&ts))return 0;return uint64_t(ts.tv_sec)*UINT64_C(1000000000)+uint64_t(ts.tv_nsec);}
struct NativeIO {RTXNativeConnection108 *state;uint64_t nowNs(){return ::nowNs();}
 bool call(unsigned selector,const uint64_t *scalars,unsigned count,const uint8_t *input,size_t bytes,uint8_t *output,size_t capacity){return [state call:selector scalars:scalars count:count input:input bytes:bytes output:output capacity:capacity]==0;}
};
struct FileSink {NSString *directory;
 bool save(const char *name,const uint8_t *data,size_t n){
  NSString *leaf=@(name);if([leaf rangeOfString:@"/"].location!=NSNotFound||[leaf isEqual:@"."]||[leaf isEqual:@".."])return false;
  NSData *bytes=[NSData dataWithBytes:data length:n];return [bytes writeToFile:[directory stringByAppendingPathComponent:leaf]options:NSDataWritingWithoutOverwriting error:nil];
 }
};
static bool newDirectory(NSString *path){
 if(!path||![path isAbsolutePath]||![[path stringByStandardizingPath]isEqual:path]||![[[NSURL fileURLWithPath:path]URLByResolvingSymlinksInPath].path isEqual:path])return false;
 if([[NSFileManager defaultManager]fileExistsAtPath:path])return false;
 return [[NSFileManager defaultManager]createDirectoryAtPath:path withIntermediateDirectories:NO attributes:@{NSFilePosixPermissions:@0700}error:nil];
}
static bool saveSession(FileSink &sink,RTXNativeConnection108 *state,bool passed){
 auto &s=*state->session;NSDictionary *r=@{@"passed":@(passed),@"generation":@(state->generation),@"completed":@(s.completed()),@"resident_epoch":@(s.epoch()),@"attempted":@(s.attempted()),@"failure":@(unsigned(s.failure())),@"calls":@(s.calls()),@"elapsed_ns":@(s.elapsed()),@"io_result":@(state->lastResult),@"first_io_error":@(state->firstError),@"native_iokit":@YES,@"probe_version":[NSString stringWithUTF8String:RTXAccelerator103::ParentVersion],@"owned_root_abi":@(RTXGraphicsLayout242::ABI),@"external_setup_abi":@2,@"dispatch_abi":@183,@"program_abi":@205,@"program_input_abi":@206,@"program_revision":@(state->dispatchSession->revision()),@"graphics_owner_abi":@250,@"graphics_active":@(state->graphicsSession->active()),@"graphics_completed":@(state->graphicsSession->completed()),@"graphics_failure":@(unsigned(state->graphicsSession->failure())),@"graphics_calls":@(state->graphicsSession->calls()),@"graphics_elapsed_ns":@(state->graphicsSession->elapsed()),@"owned_dispatch_active":@(state->dispatchSession->active()),@"owned_dispatch_completed":@(state->dispatchSession->completed()),@"owned_dispatch_failure":@(unsigned(state->dispatchSession->failure())),@"owned_dispatch_calls":@(state->dispatchSession->calls()),@"owned_data_verified":@(state->dataSession->ready()),@"owned_data_calls":@(state->dataSession->calls()),@"owned_data_operations":@(state->dataSession->operations()),@"owned_root_verified":@(state->rootEvidence->passed),@"owned_root_failure":@(unsigned(state->rootEvidence->failure)),@"owned_root_calls":@(state->rootEvidence->calls),@"owned_root_elapsed_ns":@(state->rootEvidence->elapsed),@"observation_selector":@77,@"observation_required":@YES,@"shader_arithmetic_checked":@NO,@"metal_registered":@NO};
 NSData *data=[NSJSONSerialization dataWithJSONObject:r options:NSJSONWritingSortedKeys error:nil];return data&&sink.save("native-session.json",static_cast<const uint8_t *>(data.bytes),data.length);
}
// Caller holds the same connection mutex used for dispatch and close. Failed
// preconditions perform no I/O; any attempted transaction retires on failure.
static bool replaceResidentLocked(RTXNativeConnection108 *state,NSData *payload,uint64_t epoch,uint64_t completed,uint64_t &next){
 next=0;if(graphicsClaimed243||ownedClaimed185||ownedStandard187||state->closed||!state->armed||!state->session->ready()||state->session->epoch()!=epoch||state->session->completed()!=completed||payload.length!=4608)return false;
 P::Library checked;if(!P::decode(static_cast<const uint8_t *>(payload.bytes),512,static_cast<const uint8_t *>(payload.bytes)+512,4096,checked)||!RtxLibraryUpload036::profile(checked))return false;
 NSString *folder=[state->directory stringByAppendingPathComponent:[NSString stringWithFormat:@"replacement-%llu",epoch+1]];
 if(!newDirectory(folder))return false;
 state->firstError=0;FileSink sink{folder};NativeIO io{state};
 bool passed=state->session->replace(io,sink,{static_cast<const uint8_t *>(payload.bytes),4608});
 const bool saved=saveSession(sink,state,passed);passed=passed&&saved;
 if(!passed){state->session->retire();return false;}
 std::memcpy(state->payload.data(),payload.bytes,4608);next=state->session->epoch();return true;
}
@interface RTXNativeTransport108 : NSObject<RTXResidentCommandTransport058> {RTXNativeConnection108 *_state;}
- (id)initWithState:(RTXNativeConnection108 *)state;
@end
@implementation RTXNativeTransport108
- (id)initWithState:(RTXNativeConnection108 *)state{if((self=[super init]))_state=[state retain];return self;}
- (void)dealloc{[_state release];[super dealloc];}
- (BOOL)replaceLibraryPayload:(NSData *)payload expectedEpoch:(uint64_t)epoch expectedCompleted:(uint64_t)completed newEpoch:(uint64_t *)next error:(NSError **)error {
 *next=0;*error=nil;if(getpid()!=process||geteuid()!=0)return NO;
 @try {
  NSData *snapshot=[NSData dataWithData:payload];std::lock_guard<std::mutex> lock(_state->mutex);
  if(replaceResidentLocked(_state,snapshot,epoch,completed,*next))return YES;
 }@catch(NSException *exception){(void)exception;std::lock_guard<std::mutex> lock(_state->mutex);_state->session->retire();}
 *next=0;*error=[NSError errorWithDomain:MTLCommandBufferErrorDomain code:MTLCommandBufferErrorInternal userInfo:@{NSLocalizedDescriptionKey:@"Resident shader replacement was not verified"}];return NO;
}
- (BOOL)executeRequest:(NSData *)request libraryPayload:(NSData *)payload result:(NSData **)result completion:(uint64_t *)completion error:(NSError **)error {
 *result=nil;*completion=0;*error=nil;std::lock_guard<std::mutex> lock(_state->mutex);
 _state->firstError=0;bool passed=false;@try {
  if(!graphicsClaimed243&&!ownedClaimed185&&!ownedStandard187&&!_state->closed&&_state->armed&&request.length==2112){
   uint64_t serial=P::get64(static_cast<const uint8_t *>(request.bytes)+24);
   NSString *folder=[_state->directory stringByAppendingPathComponent:[NSString stringWithFormat:@"job-%llu",serial]];
   if(newDirectory(folder)){FileSink sink{folder};NativeIO io{_state};std::array<uint8_t,2048> data{};
    passed=_state->session->execute(io,sink,{static_cast<const uint8_t *>(request.bytes),request.length},{static_cast<const uint8_t *>(payload.bytes),payload.length},data,*completion);
    const bool saved=saveSession(sink,_state,passed);passed=passed&&saved;
    if(passed)*result=[NSData dataWithBytes:data.data()length:data.size()];
   }
  }
 }@catch(NSException *exception){(void)exception;passed=false;}
 if(!passed){_state->session->retire();*completion=0;*error=[NSError errorWithDomain:MTLCommandBufferErrorDomain code:MTLCommandBufferErrorInternal userInfo:@{NSLocalizedDescriptionKey:[NSString stringWithFormat:@"Native RTX capture failed (stage %u, IOKit 0x%08x); no retry",unsigned(_state->session->failure()),_state->lastResult]}];}
 return passed;
}
@end
extern "C" uint32_t rtx_native_info(RTXNativeOwnerInfo *out,size_t bytes){
 if(getpid()!=process)return RTX_NATIVE_PROCESS;if(!out||bytes!=sizeof(*out))return RTX_NATIVE_ARGUMENT;
 std::lock_guard<std::mutex> lock(ownerMutex);*out={RTX_NATIVE_MAGIC,1,sizeof(*out),uint64_t(process),ownerPhase,attempts,opens.load(),closes.load(),releases.load(),destroyed.load(),registry,calls.load(),lastSelector.load(),0,0,0,0,0,0,openResult,closeResult,{0,0,0}};
 auto *s=connection(ownerDevice);if(s){std::lock_guard<std::mutex> stateLock(s->mutex);out->arm_attempted=s->armAttempted;out->armed=s->armed;out->completed=s->graphicsSession->active()?s->graphicsSession->completed():s->dispatchSession->active()?s->dispatchSession->completed():s->session->completed();out->session_failure=unsigned(s->session->failure());out->session_calls=s->session->calls();out->session_elapsed=s->session->elapsed();}return 0;
}
extern "C" uint32_t rtx_native_open(const void *container,size_t bytes){
 if(getpid()!=process)return RTX_NATIVE_PROCESS;
 if(!container||bytes!=RTXLibrary036::Bytes)return RTX_NATIVE_ARGUMENT;
 std::array<uint8_t,RTXLibrary036::Bytes> snapshot{};std::memcpy(snapshot.data(),container,bytes);
 RTXLibrary036::Catalog checked;if(!RTXLibrary036::decode(snapshot.data(),snapshot.size(),checked))return RTX_NATIVE_ARGUMENT;
 std::lock_guard<std::mutex> lock(ownerMutex);if(ownerPhase!=RTXNativeCold)return RTX_NATIVE_STATE;ownerPhase=RTXNativeFailed;++attempts;
 if(geteuid()!=0)return openResult=uint32_t(kIOReturnNotPrivileged);
 @autoreleasepool {
  io_service_t service=0;RTXNativeConnection108 *s=nil;id<MTLDevice> device=nil;id<MTLLibrary> library=nil;
  @try {
   NSArray *all=MTLCopyAllDevices();[all release];Class cls=makeClass();service=discover();
   if(!cls||!service){if(service)IOObjectRelease(service);return openResult=RTX_NATIVE_EVIDENCE;}
   device=reinterpret_cast<id(*)(id,SEL,io_service_t)>(objc_msgSend)([cls alloc],sel_registerName("initWithAcceleratorPort:"),service);
   IOObjectRelease(service);service=0;
   if(!device)return openResult=constructionError?constructionError:RTX_NATIVE_EXCEPTION;
   s=[connection(device)retain];if(!s){[device release];return openResult=RTX_NATIVE_EVIDENCE;}
   const uint64_t identifier=s->generation;
   NSError *error=nil;library=RTXNewCompiledLibrary(device,[NSData dataWithBytes:snapshot.data()length:snapshot.size()],&error);
   NSData *payload=RTXCopyLibraryPayload(library);bool valid=library&&payload.length==4608&&s->session->configure(identifier,{static_cast<const uint8_t *>(payload.bytes),payload.length});
   if(valid)std::memcpy(s->payload.data(),payload.bytes,4608);[payload release];
   if(!valid){[library release];[s release];[device release];return openResult=RTX_NATIVE_EVIDENCE;}
   [s release];s=nil;
   ownerDevice=device;ownerLibrary=library;registry=identifier;ownerPhase=RTXNativeOpen;return openResult=0;
  }@catch(NSException *exception){(void)exception;if(service)IOObjectRelease(service);[library release];[s release];[device release];return openResult=RTX_NATIVE_EXCEPTION;}
 }
}
extern "C" uint32_t rtx_native_resident_replace(const void *container,size_t bytes,uint64_t epoch,uint64_t completed,uint64_t *next){
 if(getpid()!=process)return RTX_NATIVE_PROCESS;if(!next)return RTX_NATIVE_ARGUMENT;*next=0;
 if(!container||bytes!=RTXLibrary036::Bytes||!epoch||epoch==UINT64_MAX)return RTX_NATIVE_ARGUMENT;
 if(geteuid()!=0)return uint32_t(kIOReturnNotPrivileged);
 std::array<uint8_t,RTXLibrary036::Bytes> image{};std::memcpy(image.data(),container,bytes);RTXLibrary036::Catalog checked;
 if(!RTXLibrary036::decode(image.data(),image.size(),checked))return RTX_NATIVE_ARGUMENT;
 std::lock_guard<std::mutex> lock(ownerMutex);auto *state=connection(ownerDevice);
 if(ownerPhase!=RTXNativeArmed||!brokerClaimed||!state)return RTX_NATIVE_STATE;
 @autoreleasepool {@try {
  std::lock_guard<std::mutex> stateLock(state->mutex);NSData *payload=[NSData dataWithBytes:image.data()+640 length:4608];
  return replaceResidentLocked(state,payload,epoch,completed,*next)?0:RTX_NATIVE_EVIDENCE;
 }@catch(NSException *exception){(void)exception;std::lock_guard<std::mutex> stateLock(state->mutex);state->session->retire();return RTX_NATIVE_EXCEPTION;}}
}
static bool shape(uint32_t selector,const uint64_t *scalars,uint32_t count,const void *input,size_t n,void *output,size_t capacity){
 if((selector>=78&&selector<=84)||selector>87||selector==69||selector==65||(selector>=56&&selector<=59)||count>(selector==71?4u:3u)||n>4096||capacity>4096||bool(scalars)!=bool(count)||bool(input)!=bool(n)||bool(output)!=bool(capacity))return false;
 if((selector==68&&(count||n||capacity!=512))||(selector==70&&(count!=1||n||capacity!=1024||!scalars[0])))return false;
 if(selector==71){if(count!=4||n||!capacity||capacity!=scalars[3]||scalars[1]>6)return false;const uint64_t sizes[]={12288,45056,36864,2112,4096,512,4096};const auto part=scalars[1],offset=scalars[2],length=scalars[3];if((part<5?!scalars[0]:scalars[0]!=0)||offset>sizes[part]||length>sizes[part]-offset)return false;}
 if(selector==72&&(count||n||capacity!=256))return false;
 if(selector==73&&(count||n!=128||capacity))return false;
 if(selector==74&&(count!=1||capacity||scalars[0]>4096||scalars[0]%1024||n!=(scalars[0]==4096?512u:1024u)))return false;
 if(selector==75&&(count||n||capacity))return false;
 if(selector==76){if(count!=3||n||!capacity||capacity>1024||scalars[2]!=capacity||scalars[0]>1)return false;const uint64_t size=scalars[0]?4096:512;if(scalars[1]>=size||capacity>size-scalars[1])return false;}
 if(selector==77&&(count!=1||n||capacity!=512))return false;
 if(selector==85&&(count||n||capacity!=512))return false;
 if(selector==86){if(count!=3||n||!capacity||capacity>4096||scalars[2]!=capacity||scalars[0]>7)return false;
  const uint64_t limits[]={4096,131072,262144,256840,12288,45056,RTXGraphicsLayout242::Buffers*64,64};if(scalars[1]>limits[scalars[0]]||capacity>limits[scalars[0]]-scalars[1])return false;}
 if(selector==87&&(count!=1||n||capacity!=24||scalars[0]>=64))return false;
 return true;
}
extern "C" uint32_t rtx_native_call(uint32_t selector,const uint64_t *scalars,uint32_t count,const void *input,size_t n,void *output,size_t capacity,size_t *actual){
 if(getpid()!=process)return RTX_NATIVE_PROCESS;if(!actual)return RTX_NATIVE_ARGUMENT;*actual=0;
 if(!shape(selector,scalars,count,input,n,output,capacity))return RTX_NATIVE_ARGUMENT;
 // Admit and send the same owned snapshot even if another caller thread edits
 // its original storage. Recheck scalar-derived bounds on the copied values.
 std::array<uint64_t,4> scalarSnapshot{};std::array<uint8_t,4096> inputSnapshot{},outputSnapshot{};
 if(count)std::memcpy(scalarSnapshot.data(),scalars,count*sizeof(uint64_t));if(n)std::memcpy(inputSnapshot.data(),input,n);
 scalars=count?scalarSnapshot.data():nullptr;input=n?inputSnapshot.data():nullptr;
 if(!shape(selector,scalars,count,input,n,output,capacity))return RTX_NATIVE_ARGUMENT;
 std::lock_guard<std::mutex> lock(ownerMutex);auto *s=connection(ownerDevice);if(!s||(ownerPhase!=RTXNativeOpen&&ownerPhase!=RTXNativeArmed))return RTX_NATIVE_NOT_OPEN;
 std::lock_guard<std::mutex> stateLock(s->mutex);if(s->closed)return RTX_NATIVE_NOT_OPEN;if(graphicsClaimed243)return RTX_NATIVE_STATE;
 const bool writes=selector==0||selector==3||selector==4||selector==6||selector==7||selector==8||selector==10||selector==11||selector==13||selector==73||selector==74||selector==75;
 if(writes&&(s->writeFailed||s->armAttempted))return RTX_NATIVE_STATE;
 RtxLibraryUpload036::Scope scope{s->generation,1,s->upload.phase()!=RtxLibraryUpload036::Phase::Consumed};
 using Phase=RtxLibraryUpload036::Phase;using Error=RtxLibraryUpload036::Error;
 if(selector==73){
  std::array<uint8_t,128> expected{};auto *b=expected.data();P::Q::put64(b,RtxLibraryUpload036::HeaderMagic);P::Q::put32(b+8,1);P::Q::put32(b+12,128);P::Q::put64(b+16,s->generation);P::Q::put32(b+24,4608);P::Q::put32(b+28,1024);P::Q::put32(b+32,5);P::Q::put32(b+36,0x86);GSPDigest::SHA256 digest;digest.update(s->payload.data(),4608);digest.finish(b+40);
  if(s->upload.phase()!=Phase::Empty||!E::equal(b,static_cast<const uint8_t *>(input),128))return RTX_NATIVE_ARGUMENT;
 }
 if(selector==74&&(s->upload.phase()!=Phase::Uploading||s->upload.written()!=scalars[0]||!E::equal(s->payload.data()+scalars[0],static_cast<const uint8_t *>(input),n)))return RTX_NATIVE_ARGUMENT;
 if(selector==75&&(s->upload.phase()!=Phase::Uploading||s->upload.written()!=4608))return RTX_NATIVE_STATE;
 if(selector==0&&s->upload.phase()!=Phase::Ready)return RTX_NATIVE_STATE;
 const uint32_t code=[s call:selector scalars:scalars count:count input:input bytes:n output:capacity?outputSnapshot.data():nullptr capacity:capacity];
 if(code){if(writes)s->writeFailed=YES;return code;}
 Error change=Error::None;
 if(selector==73)change=s->upload.begin(scope,static_cast<const uint8_t *>(input),n);
 if(selector==74)change=s->upload.append(scope,scalars[0],static_cast<const uint8_t *>(input),n);
 if(selector==75)change=s->upload.seal(scope);
 if(selector==0)change=s->upload.consume(scope);
 if(change!=Error::None){s->writeFailed=YES;return RTX_NATIVE_EVIDENCE;}
 if(capacity)std::memcpy(output,outputSnapshot.data(),capacity);*actual=capacity;return 0;
}
extern "C" uint32_t rtx_native_arm(const char *directory){
 if(getpid()!=process)return RTX_NATIVE_PROCESS;if(!directory)return RTX_NATIVE_ARGUMENT;
 std::lock_guard<std::mutex> lock(ownerMutex);auto *s=connection(ownerDevice);if(!s||ownerPhase!=RTXNativeOpen)return RTX_NATIVE_NOT_OPEN;
 @autoreleasepool {@try {
  {std::lock_guard<std::mutex> stateLock(s->mutex);if(s->armAttempted||s->closed||s->writeFailed)return RTX_NATIVE_STATE;
   NSString *folder=[NSString stringWithUTF8String:directory];if(!newDirectory(folder))return RTX_NATIVE_ARGUMENT;
   s->armAttempted=YES;s->firstError=0;s->directory=[folder copy];NSString *arm=[folder stringByAppendingPathComponent:@"arm"];
   if(!newDirectory(arm))return RTX_NATIVE_EVIDENCE;FileSink sink{arm};NativeIO io{s};bool passed=s->rootEvidence->collect(io,sink,s->generation)&&s->dataSession->collect(io,sink,*s->rootEvidence)&&s->session->arm(io,sink);const bool saved=saveSession(sink,s,passed);
   if(!passed||!saved){s->session->retire();return RTX_NATIVE_EVIDENCE;}
  }
  auto *transport=[[RTXNativeTransport108 alloc]initWithState:s];NSError *error=nil;BOOL configured=RTXConfigureCommandDevice(ownerDevice,ownerLibrary,s->generation,transport,&error);[transport release];
  if(!configured){std::lock_guard<std::mutex> stateLock(s->mutex);s->session->retire();return RTX_NATIVE_EVIDENCE;}
  s->armed=YES;ownerPhase=RTXNativeArmed;return 0;
 }@catch(NSException *exception){(void)exception;s->session->retire();return RTX_NATIVE_EXCEPTION;}}
}
extern "C" uint32_t rtx_native_mapped_transfer182(id<MTLBuffer> buffer,uint32_t slot,uint64_t resourceOffset,uint64_t nativeOffset,uint64_t bytes,BOOL upload){
 if(getpid()!=process||geteuid()!=0)return RTX_NATIVE_PROCESS;
 if(!buffer||!bytes||bytes>NSUIntegerMax||resourceOffset>NSUIntegerMax)return RTX_NATIVE_ARGUMENT;
 std::lock_guard<std::mutex> lock(ownerMutex);auto*s=connection(ownerDevice);
 if(!s||ownerPhase!=RTXNativeArmed||graphicsClaimed243||brokerClaimed||ownedStandard187)return RTX_NATIVE_NOT_OPEN;
 std::lock_guard<std::mutex> stateLock(s->mutex);
 if(s->closed||!s->armed||!s->session->ready()||!s->dataSession->ready())return RTX_NATIVE_STATE;
 const uint64_t capacity=s->dataSession->capacity(slot);
 if(!capacity||nativeOffset>capacity||bytes>capacity-nativeOffset)return RTX_NATIVE_ARGUMENT;
 id transaction=nil;NSData *snapshot=nil;
 try{@autoreleasepool {@try {
  transaction=RTXNewMappedBufferTransfer182(ownerDevice,buffer,NSMakeRange(NSUInteger(resourceOffset),NSUInteger(bytes)),upload);
  if(!transaction)return RTX_NATIVE_ARGUMENT;
  snapshot=RTXCopyMappedUpload182(transaction);
  if(upload&&(!snapshot||snapshot.length!=bytes)){RTXCancelMappedBufferTransfer182(transaction);[snapshot release];[transaction release];return RTX_NATIVE_ARGUMENT;}
  NSString*folder=[s->directory stringByAppendingPathComponent:[NSString stringWithFormat:@"data-transfer-%llu",s->dataSession->operations()+1]];
  if(!newDirectory(folder)){RTXCancelMappedBufferTransfer182(transaction);[snapshot release];[transaction release];return RTX_NATIVE_EVIDENCE;}
  FileSink sink{folder};NativeIO io{s};std::vector<uint8_t>readback;
  bool passed=s->dataSession->transfer(io,sink,slot,nativeOffset,upload?static_cast<const uint8_t*>(snapshot.bytes):nullptr,size_t(bytes),readback,upload);
  const bool saved=saveSession(sink,s,passed);passed=passed&&saved;
  NSData*data=passed&&!upload?[NSData dataWithBytes:readback.data()length:readback.size()]:nil;
  if(passed)passed=RTXFinishMappedBufferTransfer182(transaction,data);
  NSDictionary*publication=@{@"published":@(passed),@"upload":@(upload),@"resource_offset":@(resourceOffset),@"native_offset":@(nativeOffset),@"bytes":@(bytes),@"slot":@(slot),@"gpu_dispatch":@NO};
  NSData*record=[NSJSONSerialization dataWithJSONObject:publication options:NSJSONWritingSortedKeys error:nil];
  if(!record||!sink.save("buffer-publication.json",static_cast<const uint8_t*>(record.bytes),record.length))passed=false;
  if(!passed){RTXCancelMappedBufferTransfer182(transaction);s->dataSession->invalidate();s->session->retire();}
  [snapshot release];[transaction release];return passed?0:RTX_NATIVE_EVIDENCE;
 }@catch(NSException*exception){(void)exception;}}}catch(...){ }
 RTXCancelMappedBufferTransfer182(transaction);[snapshot release];[transaction release];s->dataSession->invalidate();s->session->retire();return RTX_NATIVE_EXCEPTION;
}
#include "native-owned-dispatch185.inc"
#include "native-standard-owned187.inc"
#include "native-owned-broker188.inc"
#include "native-graphics243.inc"
extern "C" uint32_t rtx_native_close(){
 if(getpid()!=process)return RTX_NATIVE_PROCESS;std::lock_guard<std::mutex> lock(ownerMutex);if(ownerPhase==RTXNativeClosed)return closeResult;
 ownerPhase=RTXNativeClosed;auto *s=connection(ownerDevice);
 @autoreleasepool {
  if(ownerDevice){RTXCloseAcceleratorService(ownerDevice);RTXCloseCommandDevice(ownerDevice);RTXCloseBufferArena(ownerDevice);}
  if(s){std::lock_guard<std::mutex> stateLock(s->mutex);s->closed=YES;s->session->retire();s->lease->close();closeResult=uint32_t(s->lease->closeResult);}
  [ownerLibrary release];ownerLibrary=nil;[ownerDevice release];ownerDevice=nil;
 }
 return closeResult;
}
id<MTLDevice> RTXCopyNativeCommandDevice(){if(getpid()!=process)return nil;std::lock_guard<std::mutex> lock(ownerMutex);return ownerPhase==RTXNativeArmed&&!graphicsClaimed243&&!brokerClaimed&&!ownedClaimed185?[ownerDevice retain]:nil;}
id<MTLLibrary> RTXCopyNativeCommandLibrary(){if(getpid()!=process)return nil;std::lock_guard<std::mutex> lock(ownerMutex);return ownerPhase==RTXNativeArmed&&!graphicsClaimed243&&!brokerClaimed&&!ownedClaimed185?[ownerLibrary retain]:nil;}

#include "native-broker-implementation.inc"

#include "native-draw250.inc"
