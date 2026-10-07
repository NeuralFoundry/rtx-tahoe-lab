#import <Foundation/Foundation.h>
#include "app/RTXAcceleratorIdentity103.hpp"
#include <dlfcn.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
static unsigned checks=0,rejected=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::abort();}}while(0)
using Decode=NSDictionary*(*)(NSData*,NSDictionary*,const RTXAccelerator103::Binding&,uint64_t,NSError**);
using Read=NSData*(*)(NSString*,NSUInteger,NSError**);
static NSData*json(id v){return [NSJSONSerialization dataWithJSONObject:v options:0 error:nil];}
int main(int argc,char**argv){@autoreleasepool{
 CHECK(argc==3&&geteuid()==501);NSBundle*b=[NSBundle bundleWithPath:@(argv[1])];NSError*error=nil;CHECK([b loadAndReturnError:&error]&&!error);
 void*h=dlopen(b.executablePath.fileSystemRepresentation,RTLD_NOW|RTLD_LOCAL);CHECK(h);
 auto decode=reinterpret_cast<Decode>(dlsym(h,"RTXDecodeApplicationRuntime115"));auto read=reinterpret_cast<Read>(dlsym(h,"RTXReadApplicationRuntimeFile115"));CHECK(decode&&read);
 NSData*m=[NSData dataWithContentsOfFile:@(argv[2])];RTXAccelerator103::Binding binding;CHECK(RTXMemory107::decode(static_cast<const unsigned char*>(m.bytes),m.length,binding.memory));binding.parentGeneration=binding.memory.generation;binding.childRegistry=101;binding.epoch=3;binding.session=9;
 char boot[96]={};size_t n=sizeof(boot);CHECK(!sysctlbyname("kern.bootsessionuuid",boot,&n,nullptr,0));
 NSString*sha=@"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
 NSDictionary*spec=@{@"abi":@253,@"service":@(RTXAccelerator103::BrokerService),@"application_version":@"0.253.0",@"ready_path":@(argv[2]),@"container_sha256":sha};
 NSDictionary*port=@{@"abi":@4,@"child_registry":@101,@"program_epoch":@3,@"publication_session":@9,@"root_abi":@242,@"host_buffer_abi":@2,@"owned_data_abi":@181,@"owned_dispatch_abi":@183,@"owned_graphics_abi":@242,@"publication_abi":@3,@"owned_root_verified":@YES,@"probe_version":@"0.83.1",@"accelerator_version":@"0.253.0",@"application_version":@"0.253.0",@"memory_base64":[m base64EncodedStringWithOptions:0]};
 NSDictionary*record=@{@"abi":@253,@"protocol":@251,@"service":@(RTXAccelerator103::BrokerService),@"allowed_uid":@501,@"generation":@(binding.parentGeneration),@"root_pid":@1,@"boot_uuid":@(boot),@"container_sha256":sha,@"port_binding":port};
 NSDictionary*plan=decode(json(record),spec,binding,getpid(),&error);CHECK(plan&&!error&&plan.count==11);
 // Mutate every serialized field. Exact types matter even when NSNumber values
 // compare equal; JSON boolean false must never become integer zero.
 for(unsigned which=0;which<3;++which){NSDictionary*original=which==0?record:(which==1?port:spec);
  for(NSString*key in original){for(id bad in @[@NO,@YES,@0,@(-1),@"",[NSNull null]]){
   if(which==1&&[key isEqualToString:@"owned_root_verified"]&&bad==(id)kCFBooleanTrue)continue;
   NSMutableDictionary*changed=[original mutableCopy];changed[key]=bad;NSMutableDictionary*r=[record mutableCopy];if(which==1)r[@"port_binding"]=changed;
   error=nil;CHECK(!decode(json(which==0?changed:r),which==2?changed:spec,binding,getpid(),&error)&&error);++rejected;[r release];[changed release];
  }
  NSMutableDictionary*changed=[original mutableCopy];[changed removeObjectForKey:key];NSMutableDictionary*r=[record mutableCopy];if(which==1)r[@"port_binding"]=changed;
  error=nil;CHECK(!decode(json(which==0?changed:r),which==2?changed:spec,binding,getpid(),&error)&&error);++rejected;[r release];[changed release];
 }}
 for(auto member:{&RTXAccelerator103::Binding::childRegistry,&RTXAccelerator103::Binding::parentGeneration,&RTXAccelerator103::Binding::epoch,&RTXAccelerator103::Binding::session}){auto drift=binding;++(drift.*member);CHECK(!decode(json(record),spec,drift,getpid(),&error));++rejected;}
 // The decoder accepts a labelled fixture; the actual file reader must reject
 // its uid501 directory. No test input may stand in for root readiness.
 CHECK(!read(@(argv[2]),65536,&error)&&error);CHECK(!read(@(argv[2]),0,&error));
 std::printf("{\"passed\":true,\"checks\":%u,\"rejections\":%u,\"actual_framework_types\":true,\"actual_iokit\":false,\"actual_xpc\":false,\"gpu_jobs\":0}\n",checks,rejected);
}}
