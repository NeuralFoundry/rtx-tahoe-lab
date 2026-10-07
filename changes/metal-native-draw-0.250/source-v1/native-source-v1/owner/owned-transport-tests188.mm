#import "RTXOwnedBrokerTransport188.h"
#import "RTXNativeOwnedBroker188.h"
#import "RTXNativeOwner.h"
#import <objc/runtime.h>
#include "OwnedBroker188.hpp"
#include <memory>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
namespace OT=RTXOwnedBroker188;
static unsigned checks=0,jobs=0;
#define CHECK(...) do{++checks;if(!(__VA_ARGS__)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#__VA_ARGS__);std::exit(2);}}while(0)
struct Model188 {
 std::array<uint8_t,RTXCatalog187::Bytes>image{};RTXCatalog187::Catalog catalog;uint64_t completed=0;unsigned claims=0,retirements=0;
 bool claim(decltype(image)&out,uint64_t&gen,uint64_t&serial){++claims;out=image;gen=0x179;serial=0;return true;}
 bool execute(const OT::Bytes&wire,OT::Bytes&out,uint64_t&serial){RTXBatch187::Plan plan;CHECK(RTXBatch187::decode(wire.data(),wire.size(),catalog.library,0x179,completed+1,plan));out.assign(wire.begin()+512,wire.end());for(unsigned i=0;i<plan.resources;++i){const auto&r=plan.resource[i];if(r.access&2)for(size_t j=0;j<r.bytes;++j)out[r.payloadOffset+j]^=0x5a;}serial=++completed;return true;}
 void retire(){++retirements;}
};
struct Shared188 {std::mutex mutex;OT::Core core;Model188 model;explicit Shared188(NSData*data){CHECK(data.length==imageSize());std::memcpy(model.image.data(),data.bytes,data.length);CHECK(RTXCatalog187::decode(model.image.data(),model.image.size(),model.catalog));}static size_t imageSize(){return RTXCatalog187::Bytes;}};
@interface ModelChannel188:NSObject<RTXOwnedChannel188>{@public std::shared_ptr<Shared188>root;OT::Peer peer;unsigned mode,calls;BOOL cancelled;}
@end
@implementation ModelChannel188
- (BOOL)exchange:(NSData*)request reply:(NSData**)reply serverUID:(uint32_t*)uid serverPID:(int64_t*)pid{
 std::lock_guard<std::mutex>lock(root->mutex);if(cancelled)return NO;++calls;auto result=root->core.receive(peer,static_cast<const uint8_t*>(request.bytes),request.length,true,root->model);
 *reply=[NSData dataWithBytes:result.bytes.data()length:result.size()];*uid=0;*pid=mode==1?808:807;return YES;
}
- (void)cancel{if(!root)return;std::lock_guard<std::mutex>lock(root->mutex);cancelled=YES;peer.closed=true;}
@end
static id<MTLCommandBuffer> submit(id<MTLCommandQueue>queue,id<MTLComputePipelineState>pipeline,const id<MTLBuffer>*buffers){
 id<MTLCommandBuffer>cmd=[queue commandBuffer];CHECK(cmd);id<MTLComputeCommandEncoder>encoder=[cmd computeCommandEncoder];CHECK(encoder);[encoder setComputePipelineState:pipeline];for(unsigned i=0;i<3;++i)[encoder setBuffer:buffers[i]offset:0 atIndex:i];
 [encoder dispatchThreadgroups:MTLSizeMake(2,3,4)threadsPerThreadgroup:pipeline.requiredThreadsPerThreadgroup];[encoder endEncoding];[cmd commit];[cmd waitUntilCompleted];return cmd;
}
int main(int argc,char**argv){if(argc!=3||geteuid()!=501)return 1;@autoreleasepool{
 RTXNativeOwnerInfo before{},after{};CHECK(rtx_native_info(&before,sizeof(before))==0&&!before.io_opens&&!before.calls);
 id<RTXCommandTransport>denied=nil;NSData*deniedImage=nil;uint64_t generation=99,completed=99;CHECK(!RTXClaimNativeOwnedBroker188(&denied,&deniedImage,&generation,&completed)&&!denied&&!deniedImage&&!generation&&!completed);RTXRetireNativeOwnedBroker188();
 CHECK(!RTXNewOwnedXPC188(@"invalid.service",1000));
 Class cls=objc_allocateClassPair([NSObject class],"RTXOwnedTransportFixtureDevice188",0);CHECK(cls&&RTXInstallBufferMethods(cls)&&RTXInstallLibraryMethods(cls)&&RTXInstallCommandMethods(cls));objc_registerClassPair(cls);
 for(NSString*name in @[@"1x1x1",@"1x1x64",@"16x8x1",@"32x32x1",@"64x1x1",@"8x4x4"]){@autoreleasepool{
  NSData*image=[NSData dataWithContentsOfFile:[[@(argv[1])stringByAppendingPathComponent:name]stringByAppendingPathComponent:@"compiled.rtxlib"]];auto root=std::make_shared<Shared188>(image);
  id<MTLDevice>devices[2]={};id<MTLLibrary>libs[2]={};id<MTLComputePipelineState>pipelines[2]={};id<MTLCommandQueue>queues[2]={};id<MTLBuffer>buffers[2][3]={};id<RTXCommandTransport>transports[2]={};ModelChannel188*channels[2]={};
  for(unsigned i=0;i<2;++i){NSError*error=nil;devices[i]=(id<MTLDevice>)[[cls alloc]init];libs[i]=RTXNewCompiledLibrary(devices[i],image,&error);CHECK(libs[i]&&!error);channels[i]=[ModelChannel188 new];channels[i]->root=root;transports[i]=RTXNewOwnedTransport188(channels[i],image,0x179,&error);CHECK(transports[i]&&!error&&RTXConfigureCommandDevice(devices[i],libs[i],0x179,transports[i],&error));
   id<MTLFunction>function=[libs[i]newFunctionWithName:libs[i].functionNames[0]];pipelines[i]=[devices[i]newComputePipelineStateWithFunction:function error:&error];[function release];CHECK(pipelines[i]&&!error);queues[i]=[devices[i]newCommandQueue];CHECK(queues[i]);
   for(unsigned j=0;j<3;++j){buffers[i][j]=(i==1&&j==2)?[buffers[i][0]retain]:[devices[i]newBufferWithLength:4097 options:MTLResourceStorageModeShared];CHECK(buffers[i][j]);std::memset(buffers[i][j].contents,0x31,4097);}
  }
  for(unsigned round=0;round<2;++round)for(unsigned i=0;i<2;++i){auto cmd=submit(queues[i],pipelines[i],buffers[i]);CHECK(cmd.status==MTLCommandBufferStatusCompleted&&!cmd.error);NSDictionary*info=RTXCopyOwnedTransportInfo188(transports[i]);CHECK([info[@"completed"]unsignedIntValue]==round+1&&[info[@"native_serial"]unsignedIntValue]==round*2+i+1);[info release];
   for(unsigned j=0;j<3;++j){const auto*p=static_cast<const uint8_t*>(buffers[i][j].contents);uint8_t want=(j==2||(i==1&&j==0))?uint8_t(0x31^((round==0)?0x5a:0)):0x31;for(unsigned k=0;k<4097;++k)if(p[k]!=want)CHECK(false);}++jobs;
  }
  channels[0]->mode=1;auto command=submit(queues[0],pipelines[0],buffers[0]);CHECK(command.status==MTLCommandBufferStatusError&&command.error&&channels[0]->cancelled);const auto*output=static_cast<const uint8_t*>(buffers[0][2].contents);for(unsigned j=0;j<4097;++j)if(output[j]!=0x31)CHECK(false);
  CHECK(root->model.claims==1&&root->model.completed==5&&!root->model.retirements);
  for(unsigned i=0;i<2;++i){RTXCloseCommandDevice(devices[i]);RTXCloseOwnedTransport188(transports[i]);[queues[i]release];for(auto b:buffers[i])[b release];[pipelines[i]release];[libs[i]release];[transports[i]release];[channels[i]release];[devices[i]release];}
 }}
 CHECK(rtx_native_info(&after,sizeof(after))==0&&!std::memcmp(&before,&after,sizeof(after)));
 NSDictionary*report=@{@"passed":@YES,@"checks":@(checks),@"successful_model_commands":@(jobs),@"changed_server_rejections":@6,@"actual_metal_objects":@YES,@"synthetic_channel":@YES,@"actual_xpc":@NO,@"native_hardware_opens":@0,@"gpu_executed":@NO};CHECK([[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingSortedKeys error:nil]writeToFile:@(argv[2])options:NSDataWritingWithoutOverwriting error:nil]);std::printf("checks=%u commands=%u\n",checks,jobs);
 }return 0;}
