#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>
static unsigned checks;
#define CHECK(...) do{++checks;if(!(__VA_ARGS__)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#__VA_ARGS__);return 2;}}while(0)
int main(int argc,char**argv){@autoreleasepool{
 CHECK(argc==4&&geteuid()==501);
 NSString*path=@(argv[1]);NSData*expected=[NSData dataWithContentsOfFile:@(argv[2])];
 CHECK(expected.length==5248);bool accept=!std::strcmp(argv[3],"accept");CHECK(accept||!std::strcmp(argv[3],"reject"));
 NSBundle*bundle=[NSBundle bundleWithPath:path];NSError*error=nil;CHECK(bundle&&[bundle loadAndReturnError:&error]&&!error);
 void*handle=dlopen(bundle.executablePath.fileSystemRepresentation,RTLD_NOW|RTLD_LOCAL);CHECK(handle);
 using Copy=NSData*(*)(id);using Select=Class(*)();using Info=NSDictionary*(*)();
 auto copy=reinterpret_cast<Copy>(dlsym(handle,"RTXCopyApplicationBundleCatalog214"));
 auto select=reinterpret_cast<Select>(dlsym(handle,"RTXApplicationBundleClass"));
 auto info=reinterpret_cast<Info>(dlsym(handle,"RTXApplicationPortInfo044"));CHECK(copy&&select&&info);
 Class cls=select();CHECK(cls&&cls==objc_getClass("RTXMetalApplicationDevice209"));
 id device=[[cls alloc]init];CHECK(device&&object_getClass(device)==cls);
 NSString*actualPath=[NSBundle bundleForClass:cls].bundlePath;CHECK([actualPath.stringByStandardizingPath isEqualToString:path.stringByStandardizingPath]);
 NSData*actual=copy(device);CHECK(bool(actual)==accept);if(accept)CHECK([actual isEqualToData:expected]);CHECK(!copy(nil));
 NSDictionary*ports=info();CHECK([ports[@"base_initializations"]unsignedIntValue]==0&&[ports[@"handshakes"]unsignedIntValue]==0&&[ports[@"port_retains"]unsignedIntValue]==0);
 [ports release];[actual release];[device release];
 NSDictionary*result=@{@"passed":@YES,@"checks":@(checks),@"pid":@(getpid()),@"accepted":@(accept),@"actual_bundle_resource_reader":@YES,@"automatic_port_constructor":@NO,@"native_opened":@NO,@"gpu_executed":@NO};
 NSData*data=[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingSortedKeys error:nil];CHECK(data);std::fwrite(data.bytes,1,data.length,stdout);std::putchar('\n');
 // Objective-C bundle classes remain registered for the lifetime of this process.
 return 0;
}}
