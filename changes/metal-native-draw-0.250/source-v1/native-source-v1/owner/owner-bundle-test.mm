#import <IOKit/IOKitLib.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/message.h>
#import <objc/runtime.h>
#include "RTXNativeOwner.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(2);}}while(0)
int main(int argc,char **argv){if(argc!=4||geteuid()!=501)return 1;@autoreleasepool {
 NSArray *devices=MTLCopyAllDevices();CHECK(devices.count==1&&[[devices[0]name]containsString:@"AMD"]);[devices release];
 NSError *error=nil;NSBundle *bundle=[NSBundle bundleWithPath:@(argv[1])];CHECK([bundle loadAndReturnError:&error]&&!error);
 Class cls=nil;NSString *kind=@(argv[2]);
 if([kind isEqual:@"principal"])cls=bundle.principalClass;else if([kind isEqual:@"named"])cls=[bundle classNamed:@"RTXMetalNativeOwner108"];else return 3;
 CHECK(cls==objc_getClass("RTXMetalNativeOwner108")&&class_getSuperclass(cls)==objc_getClass("_MTLDevice")&&class_getInstanceSize(cls)==712);
 void *h=dlopen(bundle.executablePath.fileSystemRepresentation,RTLD_NOW|RTLD_NOLOAD);CHECK(h);
 auto info=reinterpret_cast<uint32_t(*)(RTXNativeOwnerInfo*,size_t)>(dlsym(h,"rtx_native_info"));
 auto construction=reinterpret_cast<NSDictionary*(*)()>(dlsym(h,"RTXMetalBundleCopyConstructionInfo"));CHECK(info&&construction);
 auto graphicsBegin=reinterpret_cast<uint32_t(*)()>(dlsym(h,"rtx_native_graphics_begin243"));
 auto graphicsDraw=reinterpret_cast<uint32_t(*)(void*,size_t,uint64_t*)>(dlsym(h,"rtx_native_graphics_draw243"));
 CHECK(graphicsBegin&&graphicsDraw&&graphicsBegin()==RTX_NATIVE_PROCESS&&graphicsDraw(nullptr,0,nullptr)==RTX_NATIVE_PROCESS);
 RTXNativeOwnerInfo state{};CHECK(info(&state,sizeof(state))==0&&state.io_opens==0&&state.calls==0);
 for(unsigned n=0;n<24;++n){id value=reinterpret_cast<id(*)(id,SEL,io_service_t)>(objc_msgSend)([cls alloc],sel_registerName("initWithAcceleratorPort:"),n%2?UINT32_MAX:0U);CHECK(!value);}
 NSDictionary *meta=construction();CHECK([meta[@"native_version"]isEqual:@"0.243.0"]&&[meta[@"probe_version"]isEqual:@"0.82.0"]);
 CHECK([meta[@"base_initializations"]unsignedLongLongValue]==24&&[meta[@"device_deallocations"]unsignedLongLongValue]==24&&[meta[@"construction_successes"]unsignedLongLongValue]==0&&![meta[@"connection_attempted"]boolValue]);
 CHECK(info(&state,sizeof(state))==0&&state.io_opens==0&&state.calls==0&&state.open_attempts==0);
 NSDictionary *report=@{@"passed":@YES,@"pid":@(getpid()),@"checks":@(checks),@"class":NSStringFromClass(cls),@"class_bytes":@(class_getInstanceSize(cls)),@"mode":kind,@"metadata":meta,@"native_opens":@0,@"native_calls":@0,@"gpu_submissions":@0};
 CHECK([[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingSortedKeys error:nil]writeToFile:@(argv[3])options:NSDataWritingWithoutOverwriting error:nil]);[meta release];std::printf("{\"passed\":true,\"checks\":%u}\n",checks);
}return 0;}
