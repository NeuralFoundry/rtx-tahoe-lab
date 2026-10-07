#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "app/RTXAcceleratorIdentity103.hpp"
#include <dlfcn.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <cstdio>
using Decode=NSDictionary*(*)(NSData*,NSDictionary*,const RTXAccelerator103::Binding&,uint64_t,NSError**);
static unsigned checks=0,rejections=0;
#define CHECK(...) do{++checks;if(!(__VA_ARGS__)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#__VA_ARGS__);return 2;}}while(0)
static NSData*encoded(id v){return [NSJSONSerialization dataWithJSONObject:v options:0 error:nil];}
int main(int argc,char**argv){@autoreleasepool{
 CHECK(argc==4&&geteuid()==501);NSString*path=@(argv[1]);NSBundle*bundle=[NSBundle bundleWithPath:path];NSError*error=nil;
 CHECK(bundle&&[bundle loadAndReturnError:&error]&&!error);void*handle=dlopen(bundle.executablePath.fileSystemRepresentation,RTLD_NOW|RTLD_LOCAL);CHECK(handle);
 auto decode=reinterpret_cast<Decode>(dlsym(handle,"RTXDecodeApplicationRuntime115"));CHECK(decode);
 NSData*memory=[NSData dataWithContentsOfFile:@(argv[2])];RTXAccelerator103::Binding b;CHECK(RTXMemory107::decode(static_cast<const unsigned char*>(memory.bytes),unsigned(memory.length),b.memory));
 b.parentGeneration=b.memory.generation;b.childRegistry=101;b.epoch=7;b.session=19;
 NSString*dir=@(argv[3]);CHECK([[NSFileManager defaultManager]createDirectoryAtPath:dir withIntermediateDirectories:NO attributes:nil error:&error]&&!error);
 // Synthetic records are passed directly to the actual Foundation decoder.
 // No protected readiness is published and no IOKit connection is opened.
 NSMutableDictionary*spec=[[NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:[bundle pathForResource:@"runtime-config209" ofType:@"json"]]options:NSJSONReadingMutableContainers error:nil]mutableCopy];CHECK(spec);
 for(NSString*key in @[@"ready_path",@"compiler_helper",@"compiler_configuration",@"diagnostics_root"]){NSString*p=[dir stringByAppendingPathComponent:key];CHECK([[@"fixture" dataUsingEncoding:NSUTF8StringEncoding]writeToFile:p atomically:NO]);spec[key]=p;}
 char boot[96]={};size_t bytes=sizeof(boot);CHECK(!sysctlbyname("kern.bootsessionuuid",boot,&bytes,nullptr,0));
 NSDictionary*port=@{@"abi":@(RTXRootReady200::PortBindingABI),@"child_registry":@(b.childRegistry),@"program_epoch":@(b.epoch),@"publication_session":@(b.session),@"root_abi":@195,@"host_buffer_abi":@2,@"owned_data_abi":@181,@"owned_dispatch_abi":@183,@"publication_abi":@2,@"owned_root_verified":@YES,@"probe_version":@"0.81.0",@"accelerator_version":@"0.235.0",@"owner_version":@"0.235.0",@"application_version":@"0.209.1",@"memory_base64":[memory base64EncodedStringWithOptions:0]};
 NSDictionary*ready=@{@"allowed_uid":@501,@"generation":@(b.parentGeneration),@"root_pid":@1000001,@"boot_uuid":@(boot),@"service":spec[@"service"],@"compiler_helper":spec[@"compiler_helper"],@"compiler_configuration":spec[@"compiler_configuration"],@"container_sha256":spec[@"container_sha256"],@"port_binding":port};
 error=nil;CHECK(decode(encoded(ready),spec,b,getpid(),&error)&&!error);
 for(NSString*key in @[@"probe_version",@"accelerator_version",@"owner_version"]){
  for(id value in @[@"0.80.0",@"0.209.0",@"0.208.0",@"",@YES,@235,[NSNull null]]){
   NSMutableDictionary*p=[port mutableCopy];p[key]=value;NSMutableDictionary*r=[ready mutableCopy];r[@"port_binding"]=p;error=nil;
   CHECK(!decode(encoded(r),spec,b,getpid(),&error)&&error);++rejections;[p release];[r release];
  }
 }
 for(NSString*key in @[@"root_abi",@"owned_dispatch_abi",@"publication_abi",@"child_registry",@"program_epoch",@"publication_session"]){
  NSMutableDictionary*p=[port mutableCopy];p[key]=@0;NSMutableDictionary*r=[ready mutableCopy];r[@"port_binding"]=p;error=nil;CHECK(!decode(encoded(r),spec,b,getpid(),&error)&&error);++rejections;[p release];[r release];
 }
 [spec release];std::printf("{\"passed\":true,\"checks\":%u,\"rejections\":%u,\"actual_foundation_decoder\":true,\"synthetic_readiness\":true,\"gpu_jobs\":0,\"actual_iokit\":false}\n",checks,rejections);return 0;
}}
