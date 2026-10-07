#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#import <objc/message.h>
#import <IOKit/IOKitLib.h>
#include <dlfcn.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
static unsigned checks;
#define CHECK(...) do{++checks;if(!(__VA_ARGS__)){fprintf(stderr,"line %d: %s\n",__LINE__,#__VA_ARGS__);abort();}}while(0)
static NSArray *devices(){NSArray *all=MTLCopyAllDevices();NSMutableArray *out=[NSMutableArray array];for(id<MTLDevice> d in all)[out addObject:@{@"name":d.name,@"registry":@(d.registryID)}];[all release];return out;}
int main(int argc,const char **argv){if(argc!=5||geteuid()==0)return 2;@autoreleasepool {
 NSString *path=@(argv[1]),*mode=@(argv[2]);CHECK([path isAbsolutePath]);CHECK([mode isEqual:@"explicit"]||[mode isEqual:@"named"]||[mode isEqual:@"principal"]);
 NSArray *before=[devices()copy];CHECK(!objc_getClass("RTXMetalApplicationDevice056")&&!objc_getClass("RTXApplicationDevice041"));
 NSBundle *bundle=[NSBundle bundleWithPath:path];CHECK(bundle&&!bundle.loaded);Class cls=Nil;
 if([mode isEqual:@"explicit"]){NSError *e=nil;CHECK([bundle loadAndReturnError:&e]&&!e);cls=[bundle classNamed:@"RTXMetalApplicationDevice056"];}
 else if([mode isEqual:@"named"])cls=[bundle classNamed:@"RTXMetalApplicationDevice056"];
 else cls=bundle.principalClass;
 CHECK(cls&&bundle.loaded&&cls==objc_getClass("RTXMetalApplicationDevice056")&&bundle.principalClass==cls);
 CHECK([bundle classNamed:@"RTXMetalApplicationDevice056"]==cls&&[bundle classNamed:@"RTXMissing044"]==Nil);
 CHECK(class_getSuperclass(cls)==objc_getClass("_MTLDevice")&&class_getInstanceSize(cls)==class_getInstanceSize(class_getSuperclass(cls)));
 CHECK(class_getImageName(cls)&&[@(class_getImageName(cls))isEqual:bundle.executablePath]);
 void *handle=dlopen(bundle.executablePath.UTF8String,RTLD_NOW|RTLD_NOLOAD);CHECK(handle);
 using Factory=id<MTLDevice>(*)(NSString *,NSData *,uint64_t,uint32_t,id<MTLLibrary> *,NSError **);
 auto factory=reinterpret_cast<Factory>(dlsym(handle,"RTXApplicationBundleCreate"));auto getClass=reinterpret_cast<Class(*)()>(dlsym(handle,"RTXApplicationBundleClass"));auto count=reinterpret_cast<uint64_t(*)()>(dlsym(handle,"RTXApplicationBundleEntryCount"));CHECK(factory&&getClass&&count&&getClass()==cls&&count()==1);
 CHECK(!dlsym(handle,"rtx_native_open")&&!dlsym(handle,"RTXClaimNativeBroker"));
 unsigned methodCount=0;Method *methods=class_copyMethodList(cls,&methodCount);CHECK(methodCount>13);NSMutableArray *surface=[NSMutableArray array];
 for(unsigned i=0;i<methodCount;++i){Dl_info info{};CHECK(dladdr(reinterpret_cast<const void *>(method_getImplementation(methods[i])),&info));CHECK([@(info.dli_fname)isEqual:bundle.executablePath]);[surface addObject:NSStringFromSelector(method_getName(methods[i]))];}free(methods);
 CHECK(!class_conformsToProtocol(cls,objc_getProtocol("MTLDevice"))&&!class_conformsToProtocol(cls,objc_getProtocol("MTLDeviceSPI")));
 NSData *image=[NSData dataWithContentsOfFile:@(argv[3])];CHECK(image.length==5248);
 id<MTLLibrary> library=nil;NSError *error=nil;id<MTLDevice> device=factory(@"local.emre.RTXMetalBroker040.missing043bundle",image,37,100,&library,&error);
 CHECK(!device&&!library&&error);CHECK([before isEqual:devices()]&&count()==1&&!objc_getClass("RTXApplicationDevice041"));
 auto portInfo=reinterpret_cast<NSDictionary *(*)(void)>(dlsym(handle,"RTXApplicationPortInfo044"));CHECK(portInfo);
 for(unsigned i=0;i<32;++i){id rejected=reinterpret_cast<id(*)(id,SEL,io_service_t)>(objc_msgSend)([cls alloc],sel_registerName("initWithAcceleratorPort:"),i%2?UINT32_MAX:0);CHECK(!rejected);}
 io_service_t other=IOServiceGetMatchingService(kIOMainPortDefault,IOServiceMatching("IOAccelerator"));CHECK(other);
 CHECK(!reinterpret_cast<id(*)(id,SEL,io_service_t)>(objc_msgSend)([cls alloc],sel_registerName("initWithAcceleratorPort:"),other));IOObjectRelease(other);
 NSDictionary *construction=portInfo();CHECK([construction[@"base_initializations"]unsignedIntValue]==33&&[construction[@"device_deallocations"]unsignedIntValue]==33&&[construction[@"thread_error"]unsignedIntValue]==3);
 for(NSString *key in @[@"successes",@"port_retains",@"port_releases",@"identities",@"handshakes"])CHECK([construction[key]unsignedIntValue]==0);
 CHECK([before isEqual:devices()]);
 NSDictionary *r=@{@"passed":@YES,@"checks":@(checks),@"pid":@(getpid()),@"uid":@(geteuid()),@"mode":mode,@"bundle":bundle.bundlePath,@"executable":bundle.executablePath,@"class":NSStringFromClass(cls),@"class_bytes":@(class_getInstanceSize(cls)),@"entry_count":@(count()),@"methods":surface,@"missing_broker_rejected":@YES,@"native_owner_linked":@NO,@"system_metal_registered":@NO,@"gpu_commands_submitted":@NO,@"devices":before,@"port_construction":construction};
 CHECK([[NSJSONSerialization dataWithJSONObject:r options:NSJSONWritingSortedKeys error:nil]writeToFile:@(argv[4]) options:NSDataWritingWithoutOverwriting error:nil]);
 printf("{\"passed\":true,\"checks\":%u,\"mode\":\"%s\"}\n",checks,mode.UTF8String);[construction release];[before release];
 }return 0;}
