#import "RTXApplicationClientInternal.h"
#include "CPUFixture041.h"
#import <objc/runtime.h>
#import <objc/message.h>
#include <dlfcn.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <cstdio>
#include <cstdlib>
using namespace RTXBroker040;
using CopyAIR=NSData *(*)(id<MTLDevice>,id<MTLFunction>,NSError **);static CopyAIR copyAIR;
static std::atomic<unsigned> checks{0},channelDeaths{0};
#define CHECK(...) do{++checks;if(!(__VA_ARGS__)){fprintf(stderr,"line %d: %s\n",__LINE__,#__VA_ARGS__);abort();}}while(0)
@interface SourceChannel055:NSObject<RTXBrokerChannel041>{@public CPUBackend041 backend;Core core;Peer peer;NSMutableArray *frames;}
@end
@implementation SourceChannel055
- (id)init{if((self=[super init]))frames=[NSMutableArray new];return self;}
- (BOOL)exchange:(NSData *)request reply:(NSData **)reply serverUID:(uint32_t *)uid serverPID:(int64_t *)pid{
 HeaderView h;CHECK(read(static_cast<const uint8_t *>(request.bytes),request.length,h)&&h.op!=Op::Execute);
 Frame f=core.receive(peer,static_cast<const uint8_t *>(request.bytes),request.length,true,backend);*reply=[NSData dataWithBytes:f.bytes.data()length:f.size];*uid=0;*pid=700;
 [frames addObject:@{@"request":[request base64EncodedStringWithOptions:0],@"reply":[*reply base64EncodedStringWithOptions:0]}];return YES;
}
- (void)cancel{}
- (void)dealloc{++channelDeaths;[frames release];[super dealloc];}
@end
static NSArray *devices(){NSArray *all=MTLCopyAllDevices();NSMutableArray *r=[NSMutableArray array];for(id<MTLDevice> d in all)[r addObject:@{@"name":d.name,@"registry":@(d.registryID)}];[all release];return r;}
static void save(NSString *out,NSString *name,id object){NSError *e=nil;NSData *b=[NSJSONSerialization dataWithJSONObject:object options:NSJSONWritingSortedKeys error:&e];CHECK(b&&!e&&[b writeToFile:[out stringByAppendingPathComponent:name]options:NSDataWritingWithoutOverwriting error:&e]&&!e);}
static NSString *source(NSString *name,unsigned value){return [NSString stringWithFormat:@"#include <metal_stdlib>\nusing namespace metal;\nkernel void %@(device const float *a [[buffer(0)]], device const float *b [[buffer(1)]], device float *out [[buffer(2)]], uint tid [[thread_position_in_grid]]) { out[tid]=(a[tid]+b[tid])+%u.0f; }\n",name,value];}
static NSDictionary *inspect(id<MTLDevice> device,id<MTLLibrary> library,NSString *name,NSString *out,NSString *file){
 CHECK(library&&library.device==device&&[library.functionNames isEqual:@[name]]);id<MTLFunction> f=[library newFunctionWithName:name];CHECK(f&&f.device==device&&f.functionType==MTLFunctionTypeKernel);
 NSError *e=nil;NSData *air=copyAIR(device,f,&e);CHECK(air&&!e&&air.length>20&&air.length<1048576);const size_t n=air.length;
 CHECK([air writeToFile:[out stringByAppendingPathComponent:[file stringByAppendingString:@".air"]]options:NSDataWritingWithoutOverwriting error:&e]&&!e);
 // The returned bytes have independent ownership beyond native function life.
 id<MTLFunction> second=[library newFunctionWithName:name];NSData *again=copyAIR(device,second,&e);CHECK(again&&!e&&[again isEqualToData:air]);[second release];[again release];[air release];
 NSDictionary *r=@{@"name":name,@"file":[file stringByAppendingString:@".air"],@"bytes":@(n),@"library_class":NSStringFromClass(object_getClass(library)),@"function_class":NSStringFromClass(object_getClass(f)),@"device_address":@((uintptr_t)device),@"library_address":@((uintptr_t)library),@"registry":@(device.registryID),@"owned":@YES};[f release];return r;
}
int main(int argc,const char **argv){if(argc!=4||geteuid()!=501)return 2;@autoreleasepool{
 NSString *out=@(argv[3]);NSArray *before=[devices()copy];CHECK(before.count==1&&[before[0][@"name"]containsString:@"AMD"]);NSBundle *bundle=[NSBundle bundleWithPath:@(argv[1])];NSError *e=nil;CHECK(bundle&&[bundle loadAndReturnError:&e]&&!e);Class cls=bundle.principalClass;CHECK(cls==objc_getClass("RTXMetalApplicationDevice056")&&class_getSuperclass(cls)==objc_getClass("_MTLDevice")&&class_getInstanceSize(cls)==712);
 CHECK(!class_conformsToProtocol(cls,@protocol(MTLDevice))&&!class_conformsToProtocol(cls,objc_getProtocol("MTLDeviceSPI")));void *handle=dlopen(bundle.executablePath.fileSystemRepresentation,RTLD_NOW|RTLD_NOLOAD);CHECK(handle&&!dlsym(handle,"rtx_native_open"));
 using Create=id<MTLDevice>(*)(id<RTXBrokerChannel041>,NSData *,uint64_t,id<MTLLibrary> *,NSError **);
 auto create=reinterpret_cast<Create>(dlsym(handle,"_Z40RTXCreateApplicationDeviceWithChannel041PU30objcproto19RTXBrokerChannel04111objc_objectP6NSDatayPPU21objcproto10MTLLibrary11objc_objectPP7NSError"));
 auto close=reinterpret_cast<void(*)(id<MTLDevice>)>(dlsym(handle,"_Z25RTXCloseApplicationDevicePU19objcproto9MTLDevice11objc_object"));
 auto featureInfo=reinterpret_cast<NSDictionary *(*)()>(dlsym(handle,"RTXCopyDeviceFeatureInfo"));auto portInfo=reinterpret_cast<NSDictionary *(*)()>(dlsym(handle,"RTXApplicationPortInfo044"));auto compilerInfo=reinterpret_cast<NSDictionary *(*)()>(dlsym(handle,"RTXCopyDeviceCompilerInfo055"));copyAIR=reinterpret_cast<CopyAIR>(dlsym(handle,"RTXCopyNativeFunctionAIR055"));CHECK(create&&close&&featureInfo&&portInfo&&compilerInfo&&copyAIR);
 NSMutableArray *records=[NSMutableArray array],*wire=[NSMutableArray array];NSString *prefix=[NSString stringWithFormat:@"rtx055_%d_",getpid()];std::mutex recordMutex;std::atomic<unsigned> asyncDone{0};unsigned retainedFunctionDeaths=0;
 @autoreleasepool{
  NSData *image=[NSData dataWithContentsOfFile:@(argv[2])];CHECK(image.length==5248);SourceChannel055 *channels[2]={};id<MTLDevice> ds[2]={};id<MTLLibrary> selected[2]={};
  for(unsigned d=0;d<2;++d){channels[d]=[SourceChannel055 new];std::memcpy(channels[d]->backend.image.data(),image.bytes,image.length);ds[d]=create(channels[d],image,37,&selected[d],&e);CHECK(ds[d]&&selected[d]&&!e&&object_getClass(ds[d])==cls&&ds[d].registryID==37);}
  CHECK(ds[0]!=ds[1]);MTLCompileOptions *opts=[MTLCompileOptions new];opts.languageVersion=MTLLanguageVersion2_4;opts.fastMathEnabled=NO;
  NSString *sharedName=[prefix stringByAppendingString:@"shared"],*sharedSource=source(sharedName,5);id<MTLLibrary> shared[2]={};
  for(unsigned d=0;d<2;++d){shared[d]=[ds[d]newLibraryWithSource:sharedSource options:opts error:&e];CHECK(shared[d]&&!e);[records addObject:inspect(ds[d],shared[d],sharedName,out,[NSString stringWithFormat:@"shared-%u",d])];}
  CHECK(shared[0]!=shared[1]);save(out,@"shared-source.json",@{@"source":sharedSource});
  id<MTLFunction> foreign=[shared[0]newFunctionWithName:sharedName];NSError *foreignError=nil;CHECK(!copyAIR(ds[1],foreign,&foreignError)&&foreignError.code==2);save(out,@"foreign-function.json",@{@"error_domain":foreignError.domain,@"error_code":@(foreignError.code)});
  foreignError=nil;CHECK(!copyAIR(nil,foreign,&foreignError)&&foreignError.code==1);foreignError=nil;CHECK(!copyAIR(ds[0],nil,&foreignError)&&foreignError.code==1);
  NSObject *invalid=[NSObject new];foreignError=nil;CHECK(!copyAIR(ds[0],(id<MTLFunction>)invalid,&foreignError)&&foreignError.code==1);[invalid release];[foreign release];
  for(auto lib:shared)[lib release];
  std::thread threads[4];
  for(unsigned t=0;t<4;++t)threads[t]=std::thread([&,t]{for(unsigned i=0;i<8;++i){@autoreleasepool{
   unsigned d=(t+i)%2;NSString *name=[prefix stringByAppendingFormat:@"t%u_%u",t,i],*text=source(name,1+t*8+i);MTLCompileOptions *options=[MTLCompileOptions new];options.languageVersion=MTLLanguageVersion2_4;options.fastMathEnabled=NO;NSError *error=nil;id<MTLLibrary> library=[ds[d]newLibraryWithSource:text options:options error:&error];CHECK(library&&!error);NSString *file=[NSString stringWithFormat:@"sync-%u-%u",t,i];NSDictionary *r=inspect(ds[d],library,name,out,file);save(out,[file stringByAppendingString:@".source.json"],@{@"source":text});{std::lock_guard<std::mutex> lock(recordMutex);[records addObject:r];}[library release];[options release];
  }}});
  for(auto &thread:threads)thread.join();
  for(unsigned d=0;d<2;++d){
   NSError *error=nil;id<MTLLibrary> bad=[ds[d]newLibraryWithSource:@"kernel void invalid( {{" options:opts error:&error];CHECK(!bad&&error.code==MTLLibraryErrorCompileFailure&&[error.domain isEqual:MTLLibraryErrorDomain]);save(out,[NSString stringWithFormat:@"invalid-%u.json",d],@{@"domain":error.domain,@"code":@(error.code),@"description":error.localizedDescription});
  }
  dispatch_group_t group=dispatch_group_create();auto *recordLock=&recordMutex;auto *completionCount=&asyncDone;
  for(unsigned i=0;i<8;++i){unsigned d=i%2;id<MTLDevice> device=ds[d];NSString *name=[prefix stringByAppendingFormat:@"async%u",i],*text=source(name,40+i),*file=[NSString stringWithFormat:@"async-%u",i];save(out,[file stringByAppendingString:@".source.json"],@{@"source":text});dispatch_group_enter(group);
   [device newLibraryWithSource:text options:opts completionHandler:^(id<MTLLibrary> library,NSError *error){@autoreleasepool{CHECK(library&&!error);NSDictionary *r=inspect(device,library,name,out,file);{std::lock_guard<std::mutex> lock(*recordLock);[records addObject:r];}++(*completionCount);}dispatch_group_leave(group);}];
  }
  CHECK(dispatch_group_wait(group,dispatch_time(DISPATCH_TIME_NOW,30*NSEC_PER_SEC))==0&&asyncDone==8);dispatch_release(group);[opts release];
  // A surviving native function must keep its original device alive even after
  // releasing its library and the application's explicit device reference.
  NSString *holdName=[prefix stringByAppendingString:@"hold"];NSError *error=nil;MTLCompileOptions *holdOpts=[MTLCompileOptions new];holdOpts.languageVersion=MTLLanguageVersion2_4;holdOpts.fastMathEnabled=NO;id<MTLLibrary> hold=[ds[0]newLibraryWithSource:source(holdName,99) options:holdOpts error:&error];CHECK(hold&&!error);id<MTLFunction> held=[hold newFunctionWithName:holdName];CHECK(held&&held.device==ds[0]);[hold release];[holdOpts release];
  for(unsigned d=0;d<2;++d){close(ds[d]);[selected[d]release];[ds[d]release];CHECK(channels[d]->backend.calls==0&&channels[d]->frames.count==2);NSArray *copied=[channels[d]->frames copy];[wire addObject:copied];[copied release];[channels[d]release];}
  NSDictionary *p=portInfo();CHECK([p[@"device_deallocations"]unsignedIntValue]<2&&held.device);retainedFunctionDeaths=[p[@"device_deallocations"]unsignedIntValue];[p release];[held release];
 }
 for(unsigned i=0;i<200;++i){NSDictionary *p=portInfo();BOOL done=[p[@"device_deallocations"]unsignedIntValue]==2;[p release];if(done&&channelDeaths==2)break;usleep(5000);}
 NSDictionary *features=featureInfo(),*ports=portInfo(),*compiler=compilerInfo();
 save(out,@"lifetime-observation.json",@{@"features":features,@"ports":ports,@"compiler":compiler,@"channel_deaths":@(channelDeaths.load()),@"retained_function_device_deaths":@(retainedFunctionDeaths),@"completed_records":@(records.count)});
 CHECK([features[@"created"]unsignedIntValue]==2&&[features[@"destroyed"]unsignedIntValue]==2&&[ports[@"device_deallocations"]unsignedIntValue]==2&&channelDeaths==2&&records.count==42);CHECK([before isEqual:devices()]);
 save(out,@"result.json",@{@"passed":@YES,@"uid":@(geteuid()),@"pid":@(getpid()),@"class":NSStringFromClass(cls),@"class_bytes":@(class_getInstanceSize(cls)),@"records":records,@"wire":wire,@"features":features,@"ports":ports,@"compiler":compiler,@"channel_deaths":@(channelDeaths.load()),@"async_callbacks":@(asyncDone.load()),@"sync_thread_builds":@32,@"cache_identity_builds":@2,@"invalid_errors":@2,@"held_function_builds":@1,@"cpu_backend_executions":@0,@"gpu_commands_submitted":@NO,@"system_metal_registered":@NO,@"devices_before":before,@"devices_after":devices(),@"checks":@(checks.load())});
 [features release];[ports release];[compiler release];[before release];printf("{\"passed\":true,\"checks\":%u}\n",checks.load());
 }return 0;}
