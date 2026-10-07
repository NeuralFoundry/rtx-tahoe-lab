#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import "RTXDeviceConstruction.h"
#import "RTXMetalCommand.h"
#include "RTXLibraryContainer.hpp"
#include "RTXNativeOwner.h"
#include <atomic>
#include <array>
#include <thread>
#include <unistd.h>

static unsigned checks;
#define CHECK(...) do{++checks;if(!(__VA_ARGS__)){fprintf(stderr,"line %d: %s\n",__LINE__,#__VA_ARGS__);abort();}}while(0)
@interface CPURejectingTransport040 : NSObject<RTXCommandTransport>{@public std::atomic<unsigned> calls;}
@end
@implementation CPURejectingTransport040
- (BOOL)executeRequest:(NSData *)request libraryPayload:(NSData *)payload result:(NSData **)result completion:(uint64_t *)completion error:(NSError **)error {
 (void)request;(void)payload;++calls;*result=nil;*completion=0;*error=[NSError errorWithDomain:@"RTXCPUTransportFixture" code:1 userInfo:nil];return NO;
}
@end
static id<MTLDevice> configure(NSData *data,CPURejectingTransport040 *transport,id<MTLLibrary> *library){
 id<MTLDevice> device=[[RTXNativeDeviceClass() alloc]init];CHECK(device);NSError *error=nil;
 *library=RTXNewCompiledLibrary(device,data,&error);CHECK(*library&&!error&&RTXConfigureCommandDevice(device,*library,37,transport,&error)&&!error);return device;
}
static MTLCommandBufferStatus submit(id<MTLDevice> device,id<MTLCommandQueue> queue,id<MTLLibrary> library){
 @autoreleasepool {
  NSError *error=nil;id<MTLFunction> function=[library newFunctionWithName:library.functionNames[0]];CHECK(function);
  id<MTLComputePipelineState> pipeline=RTXNewComputePipeline(device,function,&error);CHECK(pipeline&&!error);NSDictionary *metadata=RTXCopyPipelineMetadata(pipeline);
  id<MTLCommandBuffer> command=[queue commandBuffer];id<MTLComputeCommandEncoder> encoder=[command computeCommandEncoder];CHECK(command&&encoder);
  [encoder setComputePipelineState:pipeline];NSMutableArray *buffers=[NSMutableArray array];
  for(NSNumber *index in metadata[@"bindings"]){id<MTLBuffer> b=[device newBufferWithLength:256 options:MTLResourceStorageModeShared];CHECK(b);[buffers addObject:b];[encoder setBuffer:b offset:0 atIndex:index.unsignedIntegerValue];[b release];}
  [encoder dispatchThreadgroups:MTLSizeMake(1,1,1)threadsPerThreadgroup:MTLSizeMake(32,1,1)];[encoder endEncoding];[command commit];[command waitUntilCompleted];
  auto status=command.status;CHECK(command.error!=nil);[metadata release];[pipeline release];[function release];return status;
 }
}
int main(int argc,const char **argv){if(argc!=3||geteuid()==0)return 2;@autoreleasepool {
 NSArray *all=MTLCopyAllDevices();CHECK(all.count==1);[all release];NSData *data=[NSData dataWithContentsOfFile:@(argv[1])];CHECK(data.length==5248);
 RTXNativeOwnerInfo before{},after{};CHECK(rtx_native_info(&before,sizeof(before))==0);
 auto *transport=[CPURejectingTransport040 new];id<MTLLibrary> library=nil;
 id<MTLDevice> device=configure(data,transport,&library);CHECK(RTXClaimIdleCommandDeviceForBroker(device));CHECK(!RTXClaimIdleCommandDeviceForBroker(device)&&[device newCommandQueue]==nil);[library release];[device release];
 device=configure(data,transport,&library);id<MTLCommandQueue> queue=[device newCommandQueue];CHECK(queue&&!RTXClaimIdleCommandDeviceForBroker(device));
 NSDictionary *info=RTXCopyCommandDeviceInfo(device);CHECK(![info[@"closed"]boolValue]);[info release];[queue release];CHECK(RTXClaimIdleCommandDeviceForBroker(device));[library release];[device release];
 unsigned gateWins=0,queueWins=0;
 for(unsigned i=0;i<64;++i){@autoreleasepool {
  id<MTLLibrary> l=nil;id<MTLDevice> d=configure(data,transport,&l);std::atomic<unsigned> ready{0};std::atomic<bool> start{false};BOOL claimed=NO;id<MTLCommandQueue> q=nil;
  std::thread a([&]{@autoreleasepool {++ready;while(!start.load())std::this_thread::yield();claimed=RTXClaimIdleCommandDeviceForBroker(d);}});
  std::thread b([&]{@autoreleasepool {++ready;while(!start.load())std::this_thread::yield();q=[d newCommandQueue];}});
  while(ready!=2)std::this_thread::yield();start=true;a.join();b.join();CHECK(bool(claimed)!=bool(q));
  if(claimed)++gateWins;else{++queueWins;[q release];CHECK(RTXClaimIdleCommandDeviceForBroker(d));}
  CHECK([d newCommandQueue]==nil);[l release];[d release];
 }}
 // A structurally valid but different code payload is a CPU rejection fixture.
 // The modified bytes are never executed and are never submitted to IOKit.
 NSMutableData *different=[data mutableCopy];auto *bytes=static_cast<uint8_t *>(different.mutableBytes);bytes[1153]^=1;
 GSPDigest::SHA256 hash;hash.update(bytes+128,5120);hash.finish(bytes+32);RTXLibrary036::Catalog catalog;CHECK(RTXLibrary036::decode(bytes,5248,catalog));
 device=configure(data,transport,&library);NSError *error=nil;id<MTLLibrary> foreign=RTXNewCompiledLibrary(device,different,&error);CHECK(foreign&&!error);
 queue=[device newCommandQueue];CHECK(queue);CHECK(submit(device,queue,foreign)==MTLCommandBufferStatusError&&transport->calls==0);
 info=RTXCopyCommandDeviceInfo(device);CHECK(![info[@"uncertain"]boolValue]&&[info[@"transport_calls"]unsignedIntegerValue]==0&&[info[@"next_serial"]unsignedIntegerValue]==1);
 NSDictionary *rejected=[info copy];[info release];
 CHECK(submit(device,queue,library)==MTLCommandBufferStatusError&&transport->calls==1);
 info=RTXCopyCommandDeviceInfo(device);CHECK([info[@"uncertain"]boolValue]&&[info[@"transport_calls"]unsignedIntegerValue]==1);
 RTXCloseCommandDevice(device);[queue release];[foreign release];[library release];[different release];[device release];
 CHECK(rtx_native_info(&after,sizeof(after))==0&&!memcmp(&before,&after,sizeof(before))&&after.io_opens==0&&after.calls==0);
 NSDictionary *result=@{@"passed":@YES,@"checks":@(checks),@"pid":@(getpid()),@"uid":@(geteuid()),@"race_iterations":@64,@"gate_wins":@(gateWins),@"queue_wins":@(queueWins),@"mismatched_payload":rejected,@"matching_payload_fixture":info,@"cpu_transport_calls":@(transport->calls.load()),@"native_info":[[NSData dataWithBytes:&after length:sizeof(after)]base64EncodedStringWithOptions:0],@"gpu_commands_submitted":@NO};
 CHECK([[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingSortedKeys error:nil]writeToFile:@(argv[2])options:NSDataWritingWithoutOverwriting error:nil]);
 printf("{\"passed\":true,\"checks\":%u,\"race_iterations\":64,\"gpu_commands_submitted\":false}\n",checks);[rejected release];[info release];[transport release];
 }return 0;}
