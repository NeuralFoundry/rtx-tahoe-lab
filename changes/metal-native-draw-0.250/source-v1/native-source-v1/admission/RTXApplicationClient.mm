#import "RTXApplicationClientInternal.h"
#import "RTXDeviceLimits.h"
#import "RTXDeviceFeatures.h"
#import "RTXDeviceContract.h"
#import "RTXDeviceCompiler.h"
#import "RTXDeviceIdentity.h"
#import "RTXDeviceWrapper.h"
#import "RTXResourceFactories.h"
#import "RTXDeviceRegistration.h"
#import <objc/message.h>
#include "ResidentBrokerClient.hpp"
#include "RTXApplicationConstruction.h"
#include "RTXApplicationPort.h"
#include <cstdlib>
#include <memory>
#include <mutex>
#include <unistd.h>
using namespace RTXResidentClient058;
static NSError *clientError(NSString *text,NSInteger code=1){return [NSError errorWithDomain:@"RTXApplicationBroker041" code:code userInfo:@{NSLocalizedDescriptionKey:text}];}
@interface RTXApplicationTransport059:NSObject<RTXCommandTransport>{
 id<RTXBrokerChannel041> _channel;std::unique_ptr<State> _state;std::mutex _mutex;pid_t _process;uid_t _uid;
 uint64_t _exchanges;
}
- (id)initWithChannel:(id<RTXBrokerChannel041>)channel container:(NSData *)container generation:(uint64_t)generation;
- (BOOL)hello:(NSError **)error;
- (void)close;
- (NSDictionary *)copyInfo;
@end
@implementation RTXApplicationTransport059
- (id)initWithChannel:(id<RTXBrokerChannel041>)channel container:(NSData *)container generation:(uint64_t)generation {
 if((self=[super init])){_process=getpid();_uid=geteuid();_channel=[channel retain];_state=std::make_unique<State>(static_cast<const uint8_t *>(container.bytes),container.length,generation);}return self;
}
- (BOOL)exchange:(const Frame &)frame output:(std::array<uint8_t,2048> &)output completion:(uint64_t &)completion error:(NSError **)error {
 NSData *reply=nil;uint32_t uid=UINT32_MAX;int64_t pid=0;BOOL exchanged=NO;
 if(getpid()==_process&&geteuid()==_uid){++_exchanges;@try{exchanged=[_channel exchange:[NSData dataWithBytes:frame.bytes.data()length:frame.size]reply:&reply serverUID:&uid serverPID:&pid];}@catch(NSException *e){(void)e;}}
 BOOL accepted=exchanged&&[reply isKindOfClass:[NSData class]]&&_state->accept(static_cast<const uint8_t *>(reply.bytes),reply.length,uid,pid,output,completion);
 if(!accepted){_state->fail();[_channel cancel];if(error)*error=clientError(@"Broker reply was unavailable or did not match the pinned session; this connection is retired",100+NSInteger(_state->status()));}
 return accepted;
}
- (BOOL)hello:(NSError **)error {
 if(getpid()!=_process||geteuid()!=_uid)return NO;
 std::lock_guard<std::mutex> lock(_mutex);Frame frame;std::array<uint8_t,2048> result{};uint64_t completion=0;
 if(!_state->beginHello(frame)){if(error)*error=clientError(@"Invalid reviewed library or generation");return NO;}
 return [self exchange:frame output:result completion:completion error:error];
}
- (BOOL)executeRequest:(NSData *)request libraryPayload:(NSData *)payload result:(NSData **)result completion:(uint64_t *)completion error:(NSError **)error {
 if(!result||!completion||!error)return NO;*result=nil;*completion=0;*error=nil;
 if(getpid()!=_process||geteuid()!=_uid){*error=clientError(@"Broker transport belongs to another process or user");return NO;}
 std::lock_guard<std::mutex> lock(_mutex);Frame frame;
 if(!_state->beginExecute(static_cast<const uint8_t *>(request.bytes),request.length,static_cast<const uint8_t *>(payload.bytes),payload.length,frame)){[_channel cancel];*error=clientError(@"Invalid or retired broker request");return NO;}
 std::array<uint8_t,2048> data{};uint64_t completed=0;
 if(![self exchange:frame output:data completion:completed error:error])return NO;
 *result=[NSData dataWithBytes:data.data()length:data.size()];*completion=completed;return YES;
}
- (void)close {
 if(getpid()!=_process||geteuid()!=_uid)return;
 std::lock_guard<std::mutex> lock(_mutex);Frame frame;
 if(_state&&_state->beginClose(frame)){std::array<uint8_t,2048> result{};uint64_t completion=0;[self exchange:frame output:result completion:completion error:nullptr];}
 [_channel cancel];
}
- (NSDictionary *)copyInfo {
 std::lock_guard<std::mutex> lock(_mutex);
 return [@{@"generation":@(_state->generation()),@"session":@(_state->session()),@"completed":@(_state->completed()),@"native_serial":@(_state->nativeSerial()),@"resident_epoch":@(_state->epoch()),@"phase":@(unsigned(_state->phase())),@"server_pid":@(_state->serverPID()),@"channel_exchanges":@(_exchanges),@"process_id":@(_process),@"uid":@(_uid)}copy];
}
- (void)dealloc {[self close];[_channel release];[super dealloc];}
@end
static char transportKey;static Class applicationBase;
static RTXApplicationTransport059 *transportFor(id device){return objc_getAssociatedObject(device,&transportKey);}
static uint64_t registry(id device,SEL){uint64_t child=RTXApplicationRegistry049(device);if(child)return child;NSDictionary *info=[transportFor(device)copyInfo];uint64_t generation=[info[@"generation"]unsignedLongLongValue];[info release];return generation;}
static NSString *name(id,SEL){return @"RTX 3060 experimental broker compute";}
static unsigned port(id device,SEL){return RTXApplicationPort044(device);}
static void deallocate(id device,SEL selector){
 RTXCloseDeviceWrapper047(device);RTXApplicationDeviceWillDeallocate044(device);[transportFor(device)close];RTXCloseDeviceFeatures(device);RTXCloseBufferArena(device);
 struct objc_super super={device,applicationBase};reinterpret_cast<void(*)(struct objc_super *,SEL)>(objc_msgSendSuper)(&super,selector);
}
static Class applicationClass(Class supplied=Nil){
 // Static class selection is immutable for the lifetime of this image. The
 // normal dynamic factory remains available when no bundle class is supplied.
 if(supplied&&(class_getSuperclass(supplied)!=objc_getClass("_MTLDevice")||objc_getClass(class_getName(supplied))!=supplied))return Nil;
 static Class result;static std::once_flag once;
 std::call_once(once,[supplied]{
  applicationBase=objc_getClass("_MTLDevice");if(!applicationBase)return;
  if(supplied){unsigned directCount=0;Method *direct=class_copyMethodList(supplied,&directCount);free(direct);
   if(directCount||class_getInstanceSize(supplied)!=class_getInstanceSize(applicationBase))return;}
  Class cls=objc_allocateClassPair(applicationBase,supplied?"RTXApplicationBundleMethodStage059":"RTXApplicationDevice041",0);if(!cls)return;
  BOOL ok=RTXInstallLibraryMethods(cls)&&RTXInstallBufferMethods(cls)&&RTXInstallCommandMethods(cls)&&RTXInstallDeviceLimits(cls)&&RTXInstallDeviceFeatures(cls)&&RTXInstallDeviceContract045(cls)&&RTXInstallDeviceCompiler055(cls)&&RTXInstallDeviceIdentity046(cls)&&RTXInstallDeviceWrapper047(cls)&&RTXInstallResourceFactories047(cls)&&RTXInstallDeviceRegistration051(cls);
  ok=ok&&class_addMethod(cls,sel_registerName("initWithAcceleratorPort:"),reinterpret_cast<IMP>(RTXApplicationInitializeWithPort044),"@20@0:8I16")&&
   class_addMethod(cls,sel_registerName("_setAcceleratorService:"),reinterpret_cast<IMP>(RTXApplicationSetService044),"v24@0:8@16")&&
   class_addMethod(cls,sel_registerName("dealloc"),reinterpret_cast<IMP>(deallocate),"v16@0:8")&&
   class_addMethod(cls,sel_registerName("registryID"),reinterpret_cast<IMP>(registry),"Q16@0:8")&&
   class_addMethod(cls,sel_registerName("name"),reinterpret_cast<IMP>(name),"@16@0:8")&&
   class_addMethod(cls,sel_registerName("acceleratorPort"),reinterpret_cast<IMP>(port),"I16@0:8");
  if(!ok){objc_disposeClassPair(cls);return;}
  if(supplied){
   unsigned count=0;Method *methods=class_copyMethodList(cls,&count);ok=count==15+RTXDeviceLimitMethodCount()+RTXDeviceFeatureMethodCount()+RTXDeviceContractMethodCount045()+RTXDeviceCompilerMethodCount055()+RTXDeviceIdentityMethodCount046()+RTXDeviceWrapperMethodCount047()+RTXResourceFactoryMethodCount047()+RTXDeviceRegistrationMethodCount051();
   for(unsigned i=0;i<count&&ok;++i)ok=class_addMethod(supplied,method_getName(methods[i]),method_getImplementation(methods[i]),method_getTypeEncoding(methods[i]));
   free(methods);objc_disposeClassPair(cls);if(!ok)return;result=supplied;
  }else{objc_registerClassPair(cls);result=cls;}
 });return supplied&&result!=supplied?Nil:result;
}
BOOL RTXUseApplicationDeviceClass(Class cls){return cls&&applicationClass(cls)==cls;}
Class RTXApplicationDeviceClass(){return applicationClass();}
static id<MTLDevice> createWithChannel(id<MTLDevice> initialized,id<RTXBrokerChannel041> channel,NSData *container,uint64_t generation,id<MTLLibrary> *library,NSError **error){
 if(error)*error=nil;if(!library){[initialized release];return nil;}*library=nil;
 if(geteuid()==0||!channel||![container isKindOfClass:[NSData class]]||!generation||(initialized&&object_getClass(initialized)!=applicationClass())){if(error)*error=clientError(@"Invalid application factory arguments");[initialized release];return nil;}
 // Snapshot mutable caller data once for both the handshake and local library.
 NSData *image=[container copy];auto *transport=[[RTXApplicationTransport059 alloc]initWithChannel:channel container:image generation:generation];
 id<MTLDevice> device=initialized;id<MTLLibrary> selected=nil;BOOL configured=NO;
 @try {
  if([transport hello:error]){
   if(!device){Class cls=applicationClass();if(cls)device=[[cls alloc]init];}
   if(device&&RTXInitializeDeviceLimits(device)&&RTXInitializeDeviceFeatures(device)){
    reinterpret_cast<void(*)(id,SEL)>(objc_msgSend)(device,sel_registerName("initWorkarounds"));
    selected=RTXNewCompiledLibrary(device,image,error);
    configured=selected&&RTXConfigureResidentCommandDevice059(device,selected,generation,transport,error);
    if(configured)objc_setAssociatedObject(device,&transportKey,transport,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
   }
  }
 }@catch(NSException *e){configured=NO;if(error)*error=clientError(e.reason?:@"Application device initialization exception");}
 if(!configured){[transport close];[selected release];[device release];device=nil;if(error&&!*error)*error=clientError(@"Application device initialization failed");}
 else *library=selected;
 [transport release];[image release];return device;
}
id<MTLDevice> RTXCreateApplicationDeviceWithChannel041(id<RTXBrokerChannel041> channel,NSData *container,uint64_t generation,id<MTLLibrary> *library,NSError **error){
 return createWithChannel(nil,channel,container,generation,library,error);
}
id<MTLDevice> RTXConsumeInitializedApplicationDevice044(id<MTLDevice> device,NSString *service,NSData *container,uint64_t generation,id<MTLLibrary> *library,NSError **error){
 id<RTXBrokerChannel041> channel=RTXNewXPCChannel041(service,2000);
 id<MTLDevice> result=createWithChannel(device,channel,container,generation,library,error);[channel release];return result;
}
id<MTLDevice> RTXCreateApplicationDevice(NSString *service,NSData *container,uint64_t generation,uint32_t timeout,id<MTLLibrary> *library,NSError **error){
 if(error)*error=nil;if(library)*library=nil;
 id<RTXBrokerChannel041> channel=RTXNewXPCChannel041(service,timeout);
 if(!channel){if(error)*error=clientError(@"Invalid broker service or timeout");return nil;}
 id<MTLDevice> device=RTXCreateApplicationDeviceWithChannel041(channel,container,generation,library,error);[channel release];return device;
}
void RTXCloseApplicationDevice(id<MTLDevice> device){RTXCloseDeviceWrapper047(device);RTXCloseApplicationPort044(device);if(!transportFor(device))return;RTXCloseCommandDevice(device);[transportFor(device)close];RTXCloseBufferArena(device);}
NSDictionary *RTXCopyApplicationDeviceInfo(id<MTLDevice> device){return [transportFor(device)copyInfo];}
