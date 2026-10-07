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
using Configure=BOOL(*)(id<MTLDevice>,NSString *,NSString *,NSString *,NSError **);
using CopyContainer=NSData *(*)(id<MTLComputePipelineState>);
static CopyContainer copyContainer;
static NSDictionary *capture(id<MTLDevice> device,id<MTLComputePipelineState> pipeline,NSString *name,NSString *out,NSString *file){
 CHECK(pipeline&&pipeline.device==device&&pipeline.allocatedSize==0&&pipeline.threadExecutionWidth==32&&pipeline.maxTotalThreadsPerThreadgroup==64);
 NSData *container=copyContainer(pipeline);CHECK(container&&container.length==5248);NSError *e=nil;CHECK([container writeToFile:[out stringByAppendingPathComponent:[file stringByAppendingString:@".rtxlib"]]options:NSDataWritingWithoutOverwriting error:&e]&&!e);[container release];
 return @{@"entry":name,@"file":[file stringByAppendingString:@".rtxlib"],@"device_address":@((uintptr_t)device),@"pipeline_class":NSStringFromClass(object_getClass(pipeline)),@"allocated_bytes":@0};
}
static id<MTLLibrary> build(id<MTLDevice> device,NSString *name,unsigned value,NSString *out,NSString *file){
 NSString *text=source(name,value);save(out,[file stringByAppendingString:@".source.json"],@{@"entry":name,@"constant":@(value),@"source":text});MTLCompileOptions *opts=[MTLCompileOptions new];opts.languageVersion=MTLLanguageVersion2_4;opts.fastMathEnabled=NO;NSError *e=nil;id<MTLLibrary> lib=[device newLibraryWithSource:text options:opts error:&e];[opts release];CHECK(lib&&!e);return lib;
}
int main(int argc,const char **argv){if(argc!=7||geteuid()!=501)return 2;@autoreleasepool{
 NSString *out=@(argv[3]);NSArray *before=[devices()copy];NSBundle *bundle=[NSBundle bundleWithPath:@(argv[1])];NSError *e=nil;CHECK([bundle loadAndReturnError:&e]&&!e);Class cls=bundle.principalClass;CHECK(cls==objc_getClass("RTXMetalApplicationDevice056"));
 void *handle=dlopen(bundle.executablePath.fileSystemRepresentation,RTLD_NOW|RTLD_NOLOAD);CHECK(handle);
 using Create=id<MTLDevice>(*)(id<RTXBrokerChannel041>,NSData *,uint64_t,id<MTLLibrary> *,NSError **);
 auto create=reinterpret_cast<Create>(dlsym(handle,"_Z40RTXCreateApplicationDeviceWithChannel041PU30objcproto19RTXBrokerChannel04111objc_objectP6NSDatayPPU21objcproto10MTLLibrary11objc_objectPP7NSError"));
 auto close=reinterpret_cast<void(*)(id<MTLDevice>)>(dlsym(handle,"_Z25RTXCloseApplicationDevicePU19objcproto9MTLDevice11objc_object"));
 auto configure=reinterpret_cast<Configure>(dlsym(handle,"RTXConfigureBackendCompiler056"));copyContainer=reinterpret_cast<CopyContainer>(dlsym(handle,"RTXCopyNativePipelineContainer056"));
 auto info=reinterpret_cast<NSDictionary *(*)()>(dlsym(handle,"RTXCopyBackendCompilerInfo056"));auto portInfo=reinterpret_cast<NSDictionary *(*)()>(dlsym(handle,"RTXApplicationPortInfo044"));CHECK(create&&close&&configure&&copyContainer&&info&&portInfo);
 NSMutableArray *records=[NSMutableArray array],*wire=[NSMutableArray array];id<MTLComputePipelineState> held=nil;
 @autoreleasepool{
  NSData *image=[NSData dataWithContentsOfFile:@(argv[2])];SourceChannel055 *channels[2]={};id<MTLDevice> ds[2]={};id<MTLLibrary> selected[2]={};
  for(unsigned d=0;d<2;++d){channels[d]=[SourceChannel055 new];std::memcpy(channels[d]->backend.image.data(),image.bytes,image.length);ds[d]=create(channels[d],image,37,&selected[d],&e);CHECK(ds[d]&&selected[d]&&!e);}
  NSString *prefix=[NSString stringWithFormat:@"runtime_%d_",getpid()],*same=[prefix stringByAppendingString:@"same"];
  id<MTLLibrary> first=build(ds[0],same,5,out,@"first");id<MTLFunction> function=[first newFunctionWithName:same];CHECK(function);
  CHECK(![ds[0]newComputePipelineStateWithFunction:function error:&e]&&[e.domain isEqual:@"RTXBackendCompiler056"]&&e.code==3);e=nil;
  CHECK(![ds[1]newComputePipelineStateWithFunction:function error:&e]&&e.code==2&&[e.domain isEqual:@"RTXNativeFunction055"]);e=nil;
  for(unsigned d=0;d<2;++d){CHECK(configure(ds[d],@(argv[4]),@(argv[5]),@(argv[6]),&e)&&!e);CHECK(!configure(ds[d],@(argv[4]),@(argv[5]),@(argv[6]),&e)&&e.code==2);e=nil;}
  held=[ds[0]newComputePipelineStateWithFunction:function error:&e];CHECK(held&&!e);[records addObject:capture(ds[0],held,same,out,@"first")];
  id<MTLComputePipelineState> repeat=[ds[0]newComputePipelineStateWithFunction:function error:&e];CHECK(repeat&&!e&&repeat!=held);[records addObject:capture(ds[0],repeat,same,out,@"repeat")];[repeat release];[function release];[first release];
  for(unsigned i=0;i<2;++i){NSString *name=i?[prefix stringByAppendingString:@"rename"]:same,*file=i?@"renamed":@"changed";id<MTLLibrary> lib=build(ds[0],name,i?5:9,out,file);id<MTLFunction> f=[lib newFunctionWithName:name];id<MTLComputePipelineState> p=[ds[0]newComputePipelineStateWithFunction:f error:&e];CHECK(p&&!e);[records addObject:capture(ds[0],p,name,out,file)];[p release];[f release];[lib release];}
  NSString *asyncName=[prefix stringByAppendingString:@"async"];id<MTLLibrary> al=build(ds[0],asyncName,13,out,@"async");id<MTLFunction> af=[al newFunctionWithName:asyncName];dispatch_semaphore_t sem=dispatch_semaphore_create(0);
  id<MTLDevice> asyncDevice=ds[0];[asyncDevice newComputePipelineStateWithFunction:af completionHandler:^(id<MTLComputePipelineState> p,NSError *error){@autoreleasepool{CHECK(p&&!error);[records addObject:capture(asyncDevice,p,asyncName,out,@"async")];}dispatch_semaphore_signal(sem);}];CHECK(dispatch_semaphore_wait(sem,dispatch_time(DISPATCH_TIME_NOW,60*NSEC_PER_SEC))==0);dispatch_release(sem);[af release];[al release];
  std::mutex mutex;std::thread threads[2];for(unsigned i=0;i<2;++i){threads[i]=std::thread([&,i]{@autoreleasepool{
   NSString *name=[prefix stringByAppendingFormat:@"thread%u",i],*file=[NSString stringWithFormat:@"thread%u",i];id<MTLLibrary> lib=build(ds[i],name,17+4*i,out,file);id<MTLFunction> f=[lib newFunctionWithName:name];NSError *error=nil;id<MTLComputePipelineState> p=[ds[i]newComputePipelineStateWithFunction:f error:&error];CHECK(p&&!error);NSDictionary *row=capture(ds[i],p,name,out,file);{std::lock_guard<std::mutex> lock(mutex);[records addObject:row];}[p release];[f release];[lib release];
  }});}
  for(auto &thread:threads)thread.join();
  NSString *badName=[prefix stringByAppendingString:@"division"],*badSource=[source(badName,5)stringByReplacingOccurrencesOfString:@"(a[tid]+b[tid])+5.0f" withString:@"a[tid]/b[tid]"];MTLCompileOptions *opts=[MTLCompileOptions new];opts.languageVersion=MTLLanguageVersion2_4;opts.fastMathEnabled=NO;
  id<MTLLibrary> bad=[ds[0]newLibraryWithSource:badSource options:opts error:&e];CHECK(bad&&!e);id<MTLFunction> bf=[bad newFunctionWithName:badName];CHECK(![ds[0]newComputePipelineStateWithFunction:bf error:&e]&&[e.domain isEqual:@"RTXBackendCompiler056"]&&e.code==4);save(out,@"unsupported.json",@{@"source":badSource,@"error_domain":e.domain,@"error_code":@(e.code),@"message":e.localizedDescription});[bf release];[bad release];[opts release];e=nil;
  NSString *lastName=[prefix stringByAppendingString:@"recovery"];id<MTLLibrary> last=build(ds[0],lastName,25,out,@"recovery");id<MTLFunction> lf=[last newFunctionWithName:lastName];id<MTLComputePipelineState> lp=[ds[0]newComputePipelineStateWithFunction:lf error:&e];CHECK(lp&&!e);[records addObject:capture(ds[0],lp,lastName,out,@"recovery")];[lp release];[lf release];[last release];
  for(unsigned d=0;d<2;++d){close(ds[d]);[selected[d]release];[ds[d]release];CHECK(channels[d]->backend.calls==0&&channels[d]->frames.count==2);NSArray *frames=[channels[d]->frames copy];[wire addObject:frames];[frames release];[channels[d]release];}
 }
 NSDictionary *retained=portInfo(),*retainedCompiler=info();save(out,@"retained-pipeline.json",@{@"ports":retained,@"compiler":retainedCompiler});CHECK([retained[@"device_deallocations"]unsignedIntValue]==1&&[retainedCompiler[@"destroyed"]unsignedIntValue]==1&&held.device);[retained release];[retainedCompiler release];[held release];
 NSDictionary *ports=portInfo(),*compiler=info();CHECK([ports[@"device_deallocations"]unsignedIntValue]==2&&[compiler[@"destroyed"]unsignedIntValue]==2&&[compiler[@"requests"]unsignedIntValue]==9&&[compiler[@"successes"]unsignedIntValue]==8&&channelDeaths==2&&records.count==8);CHECK([before isEqual:devices()]);
 save(out,@"result.json",@{@"passed":@YES,@"pid":@(getpid()),@"uid":@(geteuid()),@"records":records,@"wire":wire,@"ports":ports,@"compiler":compiler,@"channel_deaths":@(channelDeaths.load()),@"checks":@(checks.load()),@"gpu_commands_submitted":@NO,@"system_metal_registered":@NO});[ports release];[compiler release];[before release];printf("pipeline passed %u checks\n",checks.load());
 }return 0;}
