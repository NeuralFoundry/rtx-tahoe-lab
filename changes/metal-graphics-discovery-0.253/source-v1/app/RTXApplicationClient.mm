#import "RTXGraphicsBrokerTransport251.h"
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
#import "RTXOwnedBrokerTransport208.h"
#include "RTXApplicationConstruction.h"
#include "RTXApplicationPort.h"
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <memory>
#include <mutex>
#include <unistd.h>

static NSError *clientError(NSString *text,NSInteger code=1){return [NSError errorWithDomain:@"RTXApplicationBroker041" code:code userInfo:@{NSLocalizedDescriptionKey:text}];}
static char graphicsKey251;
static id<RTXGraphicsCommandTransport248> graphicsFor251(id device){return objc_getAssociatedObject(device,&graphicsKey251);}
static char transportKey;static Class applicationBase;
static id<RTXCommandTransport> transportFor(id device){return objc_getAssociatedObject(device,&transportKey);}
static uint64_t registry(id device,SEL){uint64_t child=RTXApplicationRegistry049(device);if(child)return child;NSDictionary *info=graphicsFor251(device)?RTXCopyOwnedGraphicsTransportInfo251(graphicsFor251(device)):RTXCopyOwnedTransportInfo208(transportFor(device));uint64_t generation=[info[@"generation"]unsignedLongLongValue];[info release];return generation;}
static NSString *name(id device,SEL){return graphicsFor251(device)?@"RTX 3060 experimental graphics":@"RTX 3060 experimental broker compute";}
static unsigned port(id device,SEL){return RTXApplicationPort044(device);}
static void deallocate(id device,SEL selector){
 RTXCloseDeviceWrapper047(device);RTXApplicationDeviceWillDeallocate044(device);RTXCloseCommandDevice(device);RTXCloseOwnedGraphicsTransport251(graphicsFor251(device));RTXCloseOwnedTransport208(transportFor(device));RTXCloseDeviceFeatures(device);RTXCloseBufferArena(device);
 struct objc_super super={device,applicationBase};reinterpret_cast<void(*)(struct objc_super *,SEL)>(objc_msgSendSuper)(&super,selector);
}
// Protocol declaration requires every required method in the loaded runtime,
// including inherited protocols and class methods. Method presence is an ABI
// contract, not a GPU-family claim; feature flags and nullable factories retain
// their actual supported/unsupported behavior.
static bool protocolCompatible124(Class cls,Protocol *p,std::set<std::string> &seen){
 if(!cls||!p)return false;
 const char *name=protocol_getName(p);if(!name)return false;
 if(!seen.insert(name).second)return true;
 for(BOOL instance:{YES,NO}){
  unsigned count=0;auto descriptions=protocol_copyMethodDescriptionList(p,YES,instance,&count);bool ok=count<=4096;
  for(unsigned i=0;i<count&&ok;++i){auto &d=descriptions[i];Method method=d.name?(instance?class_getInstanceMethod(cls,d.name):class_getClassMethod(cls,d.name)):nullptr;
   const char *actual=method?method_getTypeEncoding(method):nullptr;ok=d.types&&actual;
   if(ok&&std::strcmp(d.types,actual)){
    // The measured Apple base itself uses void* for this opaque private C++
    // return type. Accept only that exact inherited method/encoding pair.
    ok=instance&&!std::strcmp(sel_getName(d.name),"getCompilerConnectionManager:")&&
      !std::strcmp(d.types,"^{MTLCompilerConnectionManager=}20@0:8i16")&&!std::strcmp(actual,"^v20@0:8i16")&&
      method==class_getInstanceMethod(objc_getClass("_MTLDevice"),d.name);
   }
  }
  free(descriptions);if(!ok)return false;
 }
 unsigned count=0;Protocol *__unsafe_unretained *parents=protocol_copyProtocolList(p,&count);bool ok=count<=128;
 for(unsigned i=0;i<count&&ok;++i)ok=protocolCompatible124(cls,parents[i],seen);
 free(parents);return ok;
}
static bool installProtocols124(Class cls){
 Protocol *protocols[]={objc_getProtocol("MTLDevice"),objc_getProtocol("MTLDeviceSPI")};std::set<std::string> seen;
 for(Protocol *p:protocols)if(!protocolCompatible124(cls,p,seen))return false;
 for(Protocol *p:protocols)if(!class_addProtocol(cls,p)&&!class_conformsToProtocol(cls,p))return false;
 return true;
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
  Class cls=objc_allocateClassPair(applicationBase,supplied?"RTXApplication209_RTXApplicationBundleMethodStage059":"RTXApplication209_RTXApplicationDevice041",0);if(!cls)return;
  BOOL ok=RTXInstallLibraryMethods(cls)&&RTXInstallBufferMethods(cls)&&RTXInstallCommandMethods(cls)&&RTXInstallDeviceLimits(cls)&&RTXInstallDeviceFeatures(cls)&&RTXInstallDeviceContract045(cls)&&RTXInstallDeviceCompiler055(cls)&&RTXInstallDeviceIdentity046(cls)&&RTXInstallDeviceWrapper047(cls)&&RTXInstallResourceFactories047(cls)&&RTXInstallDeviceRegistration051(cls);
  ok=ok&&class_addMethod(cls,sel_registerName("initWithAcceleratorPort:"),reinterpret_cast<IMP>(RTXApplicationInitializeWithPort044),"@20@0:8I16")&&
   class_addMethod(cls,sel_registerName("_setAcceleratorService:"),reinterpret_cast<IMP>(RTXApplicationSetService044),"v24@0:8@16")&&
   class_addMethod(cls,sel_registerName("dealloc"),reinterpret_cast<IMP>(deallocate),"v16@0:8")&&
   class_addMethod(cls,sel_registerName("registryID"),reinterpret_cast<IMP>(registry),"Q16@0:8")&&
   class_addMethod(cls,sel_registerName("name"),reinterpret_cast<IMP>(name),"@16@0:8")&&
   class_addMethod(cls,sel_registerName("acceleratorPort"),reinterpret_cast<IMP>(port),"I16@0:8");
  if(!ok){objc_disposeClassPair(cls);return;}
  if(supplied){
   unsigned count=0;Method *methods=class_copyMethodList(cls,&count);ok=count==9+RTXLibraryMethodCount247()+RTXBufferMethodCount117()+RTXDeviceLimitMethodCount()+RTXDeviceFeatureMethodCount()+RTXDeviceContractMethodCount045()+RTXDeviceCompilerMethodCount055()+RTXDeviceIdentityMethodCount046()+RTXDeviceWrapperMethodCount047()+RTXResourceFactoryMethodCount047()+RTXDeviceRegistrationMethodCount051();
   for(unsigned i=0;i<count&&ok;++i)ok=class_addMethod(supplied,method_getName(methods[i]),method_getImplementation(methods[i]),method_getTypeEncoding(methods[i]));
   free(methods);objc_disposeClassPair(cls);if(!ok||!installProtocols124(supplied))return;result=supplied;
  }else{if(!installProtocols124(cls)){objc_disposeClassPair(cls);return;}objc_registerClassPair(cls);result=cls;}
 });return supplied&&result!=supplied?Nil:result;
}
BOOL RTXUseApplicationDeviceClass(Class cls){return cls&&applicationClass(cls)==cls;}
Class RTXApplicationDeviceClass(){return applicationClass();}
static id<MTLDevice> createWithChannel(id<MTLDevice> initialized,id<RTXOwnedChannel208> channel,NSData *container,uint64_t generation,id<MTLLibrary> *library,NSError **error){
 if(error)*error=nil;if(!library){[initialized release];return nil;}*library=nil;
 if(geteuid()==0||!channel||![container isKindOfClass:[NSData class]]||!generation||(initialized&&object_getClass(initialized)!=applicationClass())){if(error)*error=clientError(@"Invalid application factory arguments");[initialized release];return nil;}
 // Snapshot mutable caller data once for both the handshake and local library.
 NSData *image=[container copy];id<RTXCommandTransport> transport=RTXNewOwnedTransport208(channel,image,generation,error);
 id<MTLDevice> device=initialized;id<MTLLibrary> selected=nil;BOOL configured=NO;
 @try {
  if(transport){
   if(!device){Class cls=applicationClass();if(cls)device=[[cls alloc]init];}
   if(device&&RTXInitializeDeviceLimits(device)&&RTXInitializeDeviceFeatures(device)){
    reinterpret_cast<void(*)(id,SEL)>(objc_msgSend)(device,sel_registerName("initWorkarounds"));
    selected=RTXNewCompiledLibrary(device,image,error);
    configured=selected&&RTXConfigureResidentCommandDevice059(device,selected,generation,transport,error);
    if(configured)objc_setAssociatedObject(device,&transportKey,transport,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
   }
  }
 }@catch(NSException *e){configured=NO;if(error)*error=clientError(e.reason?:@"Application device initialization exception");}
 if(!configured){RTXCloseOwnedTransport208(transport);[selected release];[device release];device=nil;if(error&&!*error)*error=clientError(@"Application device initialization failed");}
 else *library=selected;
 [transport release];[image release];return device;
}
id<MTLDevice> RTXCreateApplicationDeviceWithChannel041(id<RTXOwnedChannel208> channel,NSData *container,uint64_t generation,id<MTLLibrary> *library,NSError **error){
 return createWithChannel(nil,channel,container,generation,library,error);
}
id<MTLDevice> RTXConsumeInitializedApplicationDevice044(id<MTLDevice> device,NSString *service,NSData *container,uint64_t generation,id<MTLLibrary> *library,NSError **error){
 id<RTXOwnedChannel208> channel=RTXNewOwnedXPC208(service,2000);
 id<MTLDevice> result=createWithChannel(device,channel,container,generation,library,error);[channel release];return result;
}
id<MTLDevice> RTXCreateApplicationDevice(NSString *service,NSData *container,uint64_t generation,uint32_t timeout,id<MTLLibrary> *library,NSError **error){
 if(error)*error=nil;if(library)*library=nil;
 id<RTXOwnedChannel208> channel=RTXNewOwnedXPC208(service,timeout);
 if(!channel){if(error)*error=clientError(@"Invalid broker service or timeout");return nil;}
 id<MTLDevice> device=RTXCreateApplicationDeviceWithChannel041(channel,container,generation,library,error);[channel release];return device;
}
void RTXCloseApplicationDevice(id<MTLDevice> device){RTXCloseDeviceWrapper047(device);RTXCloseApplicationPort044(device);if(!transportFor(device)&&!graphicsFor251(device))return;RTXCloseCommandDevice(device);RTXCloseOwnedGraphicsTransport251(graphicsFor251(device));RTXCloseOwnedTransport208(transportFor(device));RTXCloseBufferArena(device);}
NSDictionary *RTXCopyApplicationDeviceInfo(id<MTLDevice> device){return graphicsFor251(device)?RTXCopyOwnedGraphicsTransportInfo251(graphicsFor251(device)):RTXCopyOwnedTransportInfo208(transportFor(device));}

static id<MTLDevice> createGraphics253(id<MTLDevice>initialized,id<RTXOwnedChannel208>channel,NSData*catalog,uint64_t generation,id<MTLLibrary>*library,NSError**error){
 if(error)*error=nil;if(!library){[initialized release];return nil;}*library=nil;
 if(geteuid()==0||!channel||![catalog isKindOfClass:[NSData class]]||!generation||(initialized&&object_getClass(initialized)!=applicationClass())){if(error)*error=clientError(@"Invalid graphics application arguments");[initialized release];return nil;}
 NSData*image=[catalog copy];id<RTXGraphicsCommandTransport248>transport=RTXNewOwnedGraphicsTransport251(channel,image,generation,error);
 id<MTLDevice>device=initialized;id<MTLLibrary>selected=nil;BOOL configured=NO;
 @try{if(transport){if(!device){Class cls=applicationClass();if(cls)device=[[cls alloc]init];}
  if(device&&RTXInitializeDeviceLimits(device)&&RTXInitializeDeviceFeatures(device)){
   reinterpret_cast<void(*)(id,SEL)>(objc_msgSend)(device,sel_registerName("initWorkarounds"));
   selected=RTXNewCompiledLibrary(device,image,error);configured=selected&&RTXConfigureGraphicsDevice251(device,generation,transport);
   if(configured)objc_setAssociatedObject(device,&graphicsKey251,transport,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  }
 }}@catch(NSException*e){configured=NO;if(error)*error=clientError(e.reason?:@"Graphics application initialization failed");}
 if(!configured){RTXCloseOwnedGraphicsTransport251(transport);[selected release];[device release];device=nil;if(error&&!*error)*error=clientError(@"Graphics application initialization failed");}else *library=selected;
 [transport release];[image release];return device;
}
extern "C" id<MTLDevice> RTXCreateGraphicsApplication251(id<RTXOwnedChannel208>channel,NSData*catalog,uint64_t generation,id<MTLLibrary>*library,NSError**error){return createGraphics253(nil,channel,catalog,generation,library,error);}
extern "C" id<MTLDevice> RTXConsumeInitializedGraphicsChannel253(id<MTLDevice>device,id<RTXOwnedChannel208>channel,NSData*catalog,uint64_t generation,id<MTLLibrary>*library,NSError**error){return createGraphics253(device,channel,catalog,generation,library,error);}
extern "C" id<MTLDevice> RTXConsumeInitializedGraphicsDevice253(id<MTLDevice>device,NSString*service,NSData*catalog,uint64_t generation,id<MTLLibrary>*library,NSError**error){
 id<RTXOwnedChannel208>channel=RTXNewGraphicsXPC251(service,2000);auto result=createGraphics253(device,channel,catalog,generation,library,error);[channel release];return result;
}
extern "C" id<MTLDevice> RTXCreateGraphicsXPCApplication251(NSString*service,NSData*catalog,uint64_t generation,uint32_t timeout,id<MTLLibrary>*library,NSError**error){
 if(error)*error=nil;if(library)*library=nil;id<RTXOwnedChannel208>channel=RTXNewGraphicsXPC251(service,timeout);
 if(!channel){if(error)*error=clientError(@"Invalid graphics service or timeout");return nil;}
 id<MTLDevice>device=RTXCreateGraphicsApplication251(channel,catalog,generation,library,error);[channel release];return device;
}
