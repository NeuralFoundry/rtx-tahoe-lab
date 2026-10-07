#import "RTXMetalCommand.h"
#import "RTXNativeOwner.h"
#import <objc/runtime.h>
#include "OwnedBatch187.hpp"
#include <atomic>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
namespace B=RTXBatch187;
static unsigned checks=0,jobs=0,rejected=0;
#define CHECK(...) do{++checks;if(!(__VA_ARGS__)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#__VA_ARGS__);std::exit(2);}}while(0)
@interface OwnedFixtureTransport187:NSObject<RTXCommandTransport>{
@public RTXCatalog187::Catalog catalog;unsigned calls,mode,resources;uint64_t completed;id<MTLBuffer>borrowed;
}
@end
@implementation OwnedFixtureTransport187
- (BOOL)executeRequest:(NSData*)request libraryPayload:(NSData*)payload result:(NSData**)result completion:(uint64_t*)completion error:(NSError**)error{
 ++calls;*result=nil;*completion=0;*error=nil;CHECK(payload.length==4608);B::Plan plan;
 CHECK(B::decode(static_cast<const uint8_t*>(request.bytes),request.length,catalog.library,0x179,completed+1,plan));resources=plan.resources;
 if(mode==4)throw std::runtime_error("CPU-only transport failure");
 B::Bytes output(plan.payloadBytes);std::memcpy(output.data(),static_cast<const uint8_t*>(request.bytes)+B::Header,output.size());
 for(unsigned i=0;i<plan.resources;++i){const auto&r=plan.resource[i];if(r.access&2)for(size_t j=0;j<r.bytes;++j)output[r.payloadOffset+j]^=0x5a;}
 if(mode==1){for(unsigned i=0;i<plan.resources;++i)if(!(plan.resource[i].access&2)){output[plan.resource[i].payloadOffset]^=1;break;}}
 if(mode==2)static_cast<uint8_t*>(borrowed.contents)[0]^=1;
 if(mode==3)[borrowed setPurgeableState:MTLPurgeableStateEmpty];
 *result=[NSData dataWithBytes:output.data()length:output.size()];*completion=++completed;return YES;
}
@end
static id<MTLCommandBuffer> encode(id<MTLCommandQueue>queue,id<MTLComputePipelineState>pipeline,const id<MTLBuffer>*buffers,BOOL byThreads){
 id<MTLCommandBuffer>cmd=[queue commandBuffer];CHECK(cmd);id<MTLComputeCommandEncoder>encoder=[cmd computeCommandEncoder];CHECK(encoder);[encoder setComputePipelineState:pipeline];
 for(unsigned i=0;i<3;++i)[encoder setBuffer:buffers[i]offset:i==1?4:0 atIndex:i];
 const auto local=pipeline.requiredThreadsPerThreadgroup;
 if(byThreads)[encoder dispatchThreads:MTLSizeMake(local.width*2,local.height*3,local.depth*4)threadsPerThreadgroup:local];
 else [encoder dispatchThreadgroups:MTLSizeMake(2,3,4)threadsPerThreadgroup:local];
 [encoder endEncoding];return cmd;
}
int main(int argc,char**argv){if(argc!=3||geteuid()!=501)return 1;@autoreleasepool{
 Class cls=objc_allocateClassPair([NSObject class],"RTXStandardOwnedFixture187",0);CHECK(cls&&RTXInstallBufferMethods(cls)&&RTXInstallLibraryMethods(cls)&&RTXInstallCommandMethods(cls));objc_registerClassPair(cls);
 RTXNativeOwnerInfo initial{},final{};CHECK(rtx_native_info(&initial,sizeof(initial))==0&&initial.io_opens==0&&initial.calls==0);
 for(NSString*name in @[@"1x1x1",@"1x1x64",@"16x8x1",@"32x32x1",@"64x1x1",@"8x4x4"]){
  NSData*container=[NSData dataWithContentsOfFile:[[@(argv[1])stringByAppendingPathComponent:name]stringByAppendingPathComponent:@"compiled.rtxlib"]];CHECK(container.length==5248);
  for(unsigned mode=0;mode<=4;++mode)for(bool alias:{false,true}){@autoreleasepool{
   id<MTLDevice>device=(id<MTLDevice>)[[cls alloc]init];NSError*error=nil;id<MTLLibrary>library=RTXNewCompiledLibrary(device,container,&error);CHECK(library&&!error);
   auto*transport=[OwnedFixtureTransport187 new];CHECK(RTXCatalog187::decode(static_cast<const uint8_t*>(container.bytes),container.length,transport->catalog));transport->mode=mode;
   CHECK(RTXConfigureCommandDevice(device,library,0x179,transport,&error));id<MTLFunction>fn=[library newFunctionWithName:library.functionNames[0]];CHECK(fn);
   id<MTLComputePipelineState>pipeline=[device newComputePipelineStateWithFunction:fn error:&error];CHECK(pipeline&&!error);
   const auto local=pipeline.requiredThreadsPerThreadgroup;CHECK(pipeline.maxTotalThreadsPerThreadgroup==local.width*local.height*local.depth);
   id<MTLCommandQueue>queue=[device newCommandQueue];CHECK(queue);id<MTLBuffer>buffers[3]={};
   buffers[0]=[device newBufferWithLength:4097 options:MTLResourceStorageModeShared];buffers[1]=[device newBufferWithLength:8193 options:MTLResourceStorageModeShared];buffers[2]=alias?[buffers[0]retain]:[device newBufferWithLength:16385 options:MTLResourceStorageModeShared];
   for(unsigned i=0;i<3;++i){CHECK(buffers[i]);std::memset(buffers[i].contents,0x31,buffers[i].length);}transport->borrowed=buffers[2];
   const unsigned count=mode?1:2;
   for(unsigned serial=1;serial<=count;++serial){
    id<MTLCommandBuffer>cmd=encode(queue,pipeline,buffers,serial==2);[cmd commit];[cmd waitUntilCompleted];CHECK(transport->calls==serial&&transport->resources==(alias?2:3));
    if(mode==0){CHECK(cmd.status==MTLCommandBufferStatusCompleted&&!cmd.error);for(unsigned i=0;i<3;++i){const uint8_t want=(i==2||(i==0&&alias))?uint8_t(0x31^((serial&1)?0x5a:0)):0x31;const auto*bytes=static_cast<const uint8_t*>(buffers[i].contents);for(NSUInteger j=0;j<buffers[i].length;++j)if(bytes[j]!=want)CHECK(false);}++jobs;}
    else{CHECK(cmd.status==MTLCommandBufferStatusError&&cmd.error&&[queue commandBuffer]==nil);NSDictionary*info=RTXCopyCommandDeviceInfo(device);CHECK([info[@"uncertain"]boolValue]&&[info[@"publications"]unsignedIntValue]==0);[info release];
     const auto*out=static_cast<const uint8_t*>(buffers[2].contents);for(NSUInteger j=0;j<buffers[2].length;++j){uint8_t want=mode==3?0:uint8_t(0x31^((mode==2&&j==0)?1:0));if(out[j]!=want)CHECK(false);}++rejected;
    }
   }
   transport->borrowed=nil;RTXCloseCommandDevice(device);[queue release];for(auto b:buffers)[b release];[pipeline release];[fn release];[library release];[transport release];[device release];
  }}
 }
 CHECK(rtx_native_info(&final,sizeof(final))==0&&!std::memcmp(&initial,&final,sizeof(final)));
 NSDictionary*report=@{@"passed":@YES,@"checks":@(checks),@"synthetic_jobs":@(jobs),@"rejected":@(rejected),@"actual_metal_objects":@YES,@"synthetic_device":@YES,@"fixture_data_mutation":@YES,@"native_hardware_opens":@0,@"gpu_executed":@NO};
 CHECK([[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingSortedKeys error:nil]writeToFile:@(argv[2])options:NSDataWritingWithoutOverwriting error:nil]);std::printf("checks=%u jobs=%u rejects=%u\n",checks,jobs,rejected);
 }return 0;}
