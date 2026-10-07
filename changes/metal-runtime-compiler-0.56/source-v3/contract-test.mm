#import "RTXApplicationClientInternal.h"
#include "CPUFixture041.h"
#include <atomic>
#include <thread>
#import <IOSurface/IOSurface.h>
#include <dlfcn.h>
#import <objc/runtime.h>
#import <objc/message.h>
using namespace RTXBroker040;
static unsigned checks;static std::atomic<unsigned> channelDestructions{0};
#define CHECK(...) do{++checks;if(!(__VA_ARGS__)){fprintf(stderr,"line %d: %s\n",__LINE__,#__VA_ARGS__);abort();}}while(0)

static std::atomic<unsigned> wrapperBirths{0},wrapperDeaths{0};
static id getWrapper(id device){return reinterpret_cast<id(*)(id,SEL)>(objc_msgSend)(device,sel_registerName("_deviceWrapper"));}
static void setWrapper(id device,id wrapper){reinterpret_cast<void(*)(id,SEL,id)>(objc_msgSend)(device,sel_registerName("_setDeviceWrapper:"),wrapper);}
// Test-only wrapper retains the underlying device, as a framework wrapper can.
// If the production device retains this wrapper, its lifetime test will fail.
@interface WrapperFixture047:NSObject {@public id underlying;id next;unsigned behavior;}
- (id)initWithDevice:(id)device;
- (id)_deviceWrapper;
@end
@implementation WrapperFixture047
- (id)initWithDevice:(id)device {if((self=[super init])){underlying=[device retain];++wrapperBirths;}return self;}
- (id)_deviceWrapper {if(behavior==2)return getWrapper(underlying);if(behavior==3)return nil;return next?getWrapper(next):self;}
- (void)dealloc {[next release];[underlying release];++wrapperDeaths;[super dealloc];}
@end

static NSDictionary *wrapperTests(id device){
 NSMutableDictionary *r=[NSMutableDictionary dictionary];CHECK(getWrapper(device)==device);r[@"initial_self"]=@YES;
 @autoreleasepool {
  WrapperFixture047 *a=[[WrapperFixture047 alloc]initWithDevice:device];setWrapper(device,a);CHECK(getWrapper(device)==a);r[@"attached"]=@YES;
  WrapperFixture047 *b=[[WrapperFixture047 alloc]initWithDevice:device];a->next=[b retain];CHECK(getWrapper(device)==b);r[@"chain_resolved"]=@YES;
  setWrapper(device,nil);CHECK(getWrapper(device)==device);setWrapper(device,a);setWrapper(device,device);CHECK(getWrapper(device)==device);r[@"cleared_and_self_normalized"]=@YES;
  setWrapper(device,a);[a release];[b release];
 }
 CHECK(wrapperBirths==2&&wrapperDeaths==2&&getWrapper(device)==device);r[@"weak_expiry"]=@YES;
 NSObject *invalid=[NSObject new];BOOL rejected=NO;
 @try{setWrapper(device,invalid);}@catch(NSException *e){rejected=[e.name isEqual:NSInvalidArgumentException];}
 [invalid release];CHECK(rejected&&getWrapper(device)==device);r[@"invalid_rejected"]=@YES;
 @autoreleasepool {
  WrapperFixture047 *cycle=[[WrapperFixture047 alloc]initWithDevice:device];cycle->behavior=2;setWrapper(device,cycle);BOOL cycleRejected=NO;
  @try{(void)getWrapper(device);}@catch(NSException *e){cycleRejected=[e.name isEqual:NSInternalInconsistencyException];}
  CHECK(cycleRejected);setWrapper(device,nil);CHECK(getWrapper(device)==device);r[@"cycle_rejected_and_recovered"]=@YES;
  cycle->behavior=3;setWrapper(device,cycle);BOOL nilRejected=NO;
  @try{(void)getWrapper(device);}@catch(NSException *e){nilRejected=[e.name isEqual:NSInternalInconsistencyException];}
  CHECK(nilRejected);setWrapper(device,nil);CHECK(getWrapper(device)==device);r[@"nil_result_rejected_and_recovered"]=@YES;[cycle release];
 }
 std::atomic<unsigned> failures{0},queries{0};std::thread threads[4];
 for(unsigned i=0;i<4;++i)threads[i]=std::thread([&]{for(unsigned j=0;j<16;++j){@autoreleasepool {
  WrapperFixture047 *w=[[WrapperFixture047 alloc]initWithDevice:device];
  @try{setWrapper(device,w);id found=getWrapper(device);if(found!=device&&![found isKindOfClass:[WrapperFixture047 class]])++failures;else ++queries;setWrapper(device,nil);}@catch(NSException *e){(void)e;++failures;}
  [w release];
 }}});
 for(auto &thread:threads)thread.join();
 CHECK(failures==0&&queries==64&&wrapperBirths==67&&wrapperDeaths==67&&getWrapper(device)==device);
 r[@"concurrent_queries"]=@(queries.load());r[@"concurrent_failures"]=@(failures.load());r[@"created"]=@(wrapperBirths.load());r[@"destroyed"]=@(wrapperDeaths.load());
 NSMutableDictionary *types=[NSMutableDictionary dictionary];for(NSString *name in @[@"_deviceWrapper",@"_setDeviceWrapper:"]){Method m=class_getInstanceMethod(object_getClass(device),NSSelectorFromString(name));CHECK(m);types[name]=@(method_getTypeEncoding(m));}r[@"encodings"]=types;return r;
}

static NSArray *resourceTests(id<MTLDevice> device){
 NSMutableArray *rows=[NSMutableArray array];NSUInteger before=device.currentAllocatedSize;
 @autoreleasepool { // The descriptor dictionary also owns the real test buffer.

 NSDictionary *properties=@{(id)kIOSurfaceWidth:@4,(id)kIOSurfaceHeight:@4,(id)kIOSurfaceBytesPerElement:@4,(id)kIOSurfaceBytesPerRow:@16,(id)kIOSurfaceAllocSize:@64,(id)kIOSurfacePixelFormat:@0x52474241};
 IOSurfaceRef surface=IOSurfaceCreate((CFDictionaryRef)properties);CHECK(surface&&IOSurfaceGetWidth(surface)==4&&IOSurfaceGetHeight(surface)==4);
 CHECK(IOSurfaceLock(surface,0,nullptr)==kIOReturnSuccess);auto *surfaceBytes=static_cast<uint8_t *>(IOSurfaceGetBaseAddress(surface));CHECK(surfaceBytes);std::memset(surfaceBytes,0xa5,64);

 MTLTextureDescriptor *texture=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:4 height:4 mipmapped:NO];
 MTLHeapDescriptor *heap=[MTLHeapDescriptor new];heap.size=4096;heap.storageMode=MTLStorageModeShared;
 MTLDepthStencilDescriptor *depth=[MTLDepthStencilDescriptor new];MTLSamplerDescriptor *sampler=[MTLSamplerDescriptor new];
 MTLIndirectCommandBufferDescriptor *indirect=[MTLIndirectCommandBufferDescriptor new];indirect.commandTypes=MTLIndirectCommandTypeConcurrentDispatch;
 MTLVisibleFunctionTableDescriptor *visible=[MTLVisibleFunctionTableDescriptor new];visible.functionCount=1;
 MTLIntersectionFunctionTableDescriptor *intersection=[MTLIntersectionFunctionTableDescriptor new];intersection.functionCount=1;
 id<MTLBuffer> buffer=[device newBufferWithLength:64 options:MTLResourceStorageModeShared];CHECK(buffer);
 NSDictionary *descriptors=@{@"newDepthStencilStateWithDescriptor:":depth,@"newHeapWithDescriptor:":heap,@"newSamplerStateWithDescriptor:":sampler,@"newTextureWithDescriptor:":texture,@"newVisibleFunctionTableWithDescriptor:":visible,@"newIntersectionFunctionTableWithDescriptor:":intersection,@"newIndirectComputeCommandEncoderWithBuffer:":buffer,@"newIndirectRenderCommandEncoderWithBuffer:":buffer};
 auto record=[&](NSString *name,unsigned sample,id value){CHECK(!value);Method m=class_getInstanceMethod(object_getClass(device),NSSelectorFromString(name));CHECK(m);[rows addObject:@{@"selector":name,@"sample":@(sample),@"encoding":@(method_getTypeEncoding(m)),@"returned_nil":@(value==nil)}];};
 for(NSString *name in [[descriptors allKeys]sortedArrayUsingSelector:@selector(compare:)])for(unsigned i=0;i<2;++i)record(name,i,reinterpret_cast<id(*)(id,SEL,id)>(objc_msgSend)(device,NSSelectorFromString(name),i?descriptors[name]:nil));
 for(unsigned i=0;i<2;++i){
  NSString *name=@"newIndirectArgumentBufferLayoutWithStructType:";record(name,i,reinterpret_cast<id(*)(id,SEL,id)>(objc_msgSend)(device,NSSelectorFromString(name),nil));
  name=@"newFence";record(name,i,[device newFence]);
  for(NSString *n in @[@"newIndirectCommandBufferWithDescriptor:maxCommandCount:options:",@"newIndirectCommandBufferWithDescriptor:maxCount:options:"])record(n,i,reinterpret_cast<id(*)(id,SEL,id,NSUInteger,NSUInteger)>(objc_msgSend)(device,NSSelectorFromString(n),i?indirect:nil,i?1:0,i?MTLResourceStorageModeShared:NSUIntegerMax));
  name=@"newTextureWithDescriptor:iosurface:plane:";record(name,i,[device newTextureWithDescriptor:texture iosurface:surface plane:i?0:NSUIntegerMax]);
  std::array<uint8_t,128> storage;storage.fill(0xa5);auto original=storage;__block unsigned callbacks=0;
  name=@"newTiledTextureWithBytesNoCopy:length:descriptor:offset:bytesPerRow:";
  record(name,i,reinterpret_cast<id(*)(id,SEL,void*,NSUInteger,id,NSUInteger,NSUInteger)>(objc_msgSend)(device,NSSelectorFromString(name),storage.data()+16,64,i?texture:nil,i?0:NSUIntegerMax,16));
  name=@"newTiledTextureWithBytesNoCopy:length:deallocator:descriptor:offset:bytesPerRow:";
  void(^deallocator)(void*,NSUInteger)=^(void*,NSUInteger){++callbacks;};
  record(name,i,reinterpret_cast<id(*)(id,SEL,void*,NSUInteger,void(^)(void*,NSUInteger),id,NSUInteger,NSUInteger)>(objc_msgSend)(device,NSSelectorFromString(name),storage.data()+16,64,deallocator,i?texture:nil,i?0:NSUIntegerMax,16));
  CHECK(callbacks==0&&storage==original);NSMutableDictionary *last=[rows.lastObject mutableCopy];last[@"deallocator_calls"]=@(callbacks);last[@"borrowed_bytes_unchanged"]=@(storage==original);[rows replaceObjectAtIndex:rows.count-1 withObject:last];[last release];
 }
 std::array<uint8_t,64> expectedSurface;expectedSurface.fill(0xa5);CHECK(std::memcmp(surfaceBytes,expectedSurface.data(),64)==0);CHECK(IOSurfaceUnlock(surface,0,nullptr)==kIOReturnSuccess);CFRelease(surface);
 CHECK(rows.count==30&&device.currentAllocatedSize==before+4096);[buffer release];
 [heap release];[depth release];[sampler release];[indirect release];[visible release];[intersection release];
 }
 CHECK(device.currentAllocatedSize==before);return rows;
}

static NSDictionary *identitySnapshot(id<MTLDevice> device,NSString *phase){
 Class cls=object_getClass(device);NSMutableDictionary *encodings=[NSMutableDictionary dictionary];
 for(NSString *name in @[@"isHeadless",@"hasUnifiedMemory",@"isLowPower",@"isRemovable",@"location",@"locationNumber",@"peerGroupID",@"peerCount",@"peerIndex",@"recommendedMaxWorkingSetSize",@"isDepth24Stencil8PixelFormatSupported",@"supportPriorityBand",@"supportsSampleCount:",@"supportsVertexAmplificationCount:"]){
  Method m=class_getInstanceMethod(cls,NSSelectorFromString(name));CHECK(m);encodings[name]=@(method_getTypeEncoding(m));
 }
 BOOL priority=reinterpret_cast<BOOL(*)(id,SEL)>(objc_msgSend)(device,sel_registerName("supportPriorityBand"));
 NSDictionary *values=@{@"isHeadless":@([device isHeadless]),@"hasUnifiedMemory":@(device.hasUnifiedMemory),@"isLowPower":@([device isLowPower]),@"isRemovable":@([device isRemovable]),@"location":@(device.location),@"locationNumber":@(device.locationNumber),@"peerGroupID":@(device.peerGroupID),@"peerCount":@(device.peerCount),@"peerIndex":@(device.peerIndex),@"recommendedMaxWorkingSetSize":@(device.recommendedMaxWorkingSetSize),@"isDepth24Stencil8PixelFormatSupported":@([device isDepth24Stencil8PixelFormatSupported]),@"supportPriorityBand":@(priority)};
 CHECK([values[@"isHeadless"]boolValue]&&[values[@"location"]unsignedLongLongValue]==NSUIntegerMax&&[values[@"recommendedMaxWorkingSetSize"]unsignedLongLongValue]==67108864);
 for(NSString *name in @[@"hasUnifiedMemory",@"isLowPower",@"isRemovable",@"locationNumber",@"peerGroupID",@"peerCount",@"peerIndex",@"isDepth24Stencil8PixelFormatSupported",@"supportPriorityBand"])CHECK([values[name]unsignedLongLongValue]==0);
 NSMutableArray *samples=[NSMutableArray array];const NSUInteger counts[]={0,1,2,3,4,8,16,32,64,NSUIntegerMax};
 for(NSUInteger count:counts){BOOL texture=[device supportsTextureSampleCount:count];BOOL sample=reinterpret_cast<BOOL(*)(id,SEL,NSUInteger)>(objc_msgSend)(device,sel_registerName("supportsSampleCount:"),count);BOOL vertex=[device supportsVertexAmplificationCount:count];CHECK(!texture&&!sample&&!vertex);[samples addObject:@{@"count":@(count),@"texture":@(texture),@"sample":@(sample),@"vertex":@(vertex)}];}
 return @{@"phase":phase,@"values":values,@"encodings":encodings,@"samples":samples};
}

@interface CPUChannel041:NSObject<RTXBrokerChannel041>{@public CPUBackend041 backend;Core core;Peer peer;NSMutableArray *records;unsigned cancellations;}
@end
@implementation CPUChannel041
- (id)init {if((self=[super init]))records=[NSMutableArray new];return self;}
- (BOOL)exchange:(NSData *)request reply:(NSData **)reply serverUID:(uint32_t *)uid serverPID:(int64_t *)pid {
 Frame f=core.receive(peer,static_cast<const uint8_t *>(request.bytes),request.length,true,backend);
 *reply=[NSData dataWithBytes:f.bytes.data()length:f.size];*uid=0;*pid=700;
 [records addObject:@{@"request":[request base64EncodedStringWithOptions:0],@"reply":[*reply base64EncodedStringWithOptions:0]}];return YES;
}
- (void)cancel {++cancellations;}
- (void)dealloc {++channelDestructions;[records release];[super dealloc];}
@end
int main(int argc,const char **argv){if(argc!=5||geteuid()==0)return 2;@autoreleasepool {
 NSArray *system=MTLCopyAllDevices();CHECK(system.count==1);[system release];NSBundle *bundle=[NSBundle bundleWithPath:@(argv[1])];NSError *error=nil;CHECK(bundle&&[bundle loadAndReturnError:&error]&&!error);
 Class cls=bundle.principalClass;CHECK(cls==objc_getClass("RTXMetalApplicationDevice056"));void *handle=dlopen(bundle.executablePath.fileSystemRepresentation,RTLD_NOW|RTLD_NOLOAD);CHECK(handle&&!dlsym(handle,"rtx_native_open"));
 using Create=id<MTLDevice>(*)(id<RTXBrokerChannel041>,NSData *,uint64_t,id<MTLLibrary> *,NSError **);
 auto create=reinterpret_cast<Create>(dlsym(handle,"_Z40RTXCreateApplicationDeviceWithChannel041PU30objcproto19RTXBrokerChannel04111objc_objectP6NSDatayPPU21objcproto10MTLLibrary11objc_objectPP7NSError"));
 auto close=reinterpret_cast<void(*)(id<MTLDevice>)>(dlsym(handle,"_Z25RTXCloseApplicationDevicePU19objcproto9MTLDevice11objc_object"));
 auto featureInfo=reinterpret_cast<NSDictionary *(*)(void)>(dlsym(handle,"RTXCopyDeviceFeatureInfo"));CHECK(create&&close&&featureInfo);
 NSData *image=[NSData dataWithContentsOfFile:@(argv[2])];CHECK(image.length==5248);NSArray *selection=[NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:@(argv[3])]options:0 error:nil];CHECK(selection.count==311);
 NSMutableArray *observations=[NSMutableArray array];NSArray *frames=nil;unsigned fixtureCalls=0;NSArray *otherFrames=nil;NSMutableArray *allocations=[NSMutableArray array];NSMutableArray *identities=[NSMutableArray array];NSDictionary *wrappers=nil;NSArray *resources=nil;NSMutableDictionary *closedWrapper=[NSMutableDictionary dictionary];
 @autoreleasepool {
 CPUChannel041 *channel=[CPUChannel041 new];std::memcpy(channel->backend.image.data(),image.bytes,image.length);id<MTLLibrary> library=nil;
 id<MTLDevice> device=create(channel,image,37,&library,&error);CHECK(device&&library&&!error&&object_getClass(device)==cls);
 for(NSDictionary *row in selection){@autoreleasepool {
  NSString *name=row[@"selector"];SEL selector=NSSelectorFromString(name);Method method=class_getInstanceMethod(cls,selector);CHECK(method&&[@(method_getTypeEncoding(method))isEqual:row[@"implementation"][@"encoding"]]);
  printf("query %s\n",name.UTF8String);fflush(stdout);NSMutableDictionary *observation=[NSMutableDictionary dictionaryWithDictionary:@{@"selector":name,@"encoding":@(method_getTypeEncoding(method))}];
  @try{
   NSMethodSignature *signature=[NSMethodSignature signatureWithObjCTypes:method_getTypeEncoding(method)];CHECK(signature&&signature.numberOfArguments==2&&signature.methodReturnLength<=32&&signature.methodReturnLength>0);
   NSInvocation *invocation=[NSInvocation invocationWithMethodSignature:signature];invocation.target=device;invocation.selector=selector;
   [invocation invoke];std::array<uint8_t,32> bytes{};[invocation getReturnValue:bytes.data()];observation[@"returned"]=@YES;observation[@"return_type"]=@(signature.methodReturnType);observation[@"bytes"]=[[NSData dataWithBytes:bytes.data()length:signature.methodReturnLength]base64EncodedStringWithOptions:0];
  }@catch(NSException *exception){observation[@"returned"]=@NO;observation[@"exception_name"]=exception.name;observation[@"exception_reason"]=exception.reason?:@"";}
  CHECK([observation[@"returned"]boolValue]);[observations addObject:observation];
 }}

 auto arenaInfo=reinterpret_cast<NSDictionary *(*)(id<MTLDevice>)>(dlsym(handle,"_Z22RTXCopyBufferArenaInfoPU19objcproto9MTLDevice11objc_object"));CHECK(arenaInfo);
 auto record=[&](NSString *phase,id<MTLDevice> d,NSUInteger expected){NSUInteger actual=d.currentAllocatedSize;CHECK(actual==expected);[allocations addObject:@{@"phase":phase,@"bytes":@(actual)}];};
 [identities addObject:identitySnapshot(device,@"created")];record(@"initial",device,0);NSDictionary *empty=arenaInfo(device);CHECK(![empty[@"exists"]boolValue]);[empty release];
 wrappers=[wrapperTests(device)retain];resources=[resourceTests(device)retain];CHECK(channel->backend.calls==0);
 id<MTLBuffer> a=[device newBufferWithLength:1 options:MTLResourceStorageModeShared];CHECK(a&&a.allocatedSize==4096);record(@"one_byte",device,4096);
 id<MTLBuffer> b=[device newBufferWithLength:4097 options:MTLResourceStorageModeShared];CHECK(b&&b.allocatedSize==8192);record(@"two_buffers",device,12288);
 CHECK(![device newBufferWithLength:16777217 options:MTLResourceStorageModeShared]);CHECK(![device newBufferWithLength:4096 options:MTLResourceStorageModePrivate]);CHECK(![device newBufferWithLength:0 options:MTLResourceStorageModeShared]);record(@"failed_allocations",device,12288);
 CPUChannel041 *otherChannel=[CPUChannel041 new];std::memcpy(otherChannel->backend.image.data(),image.bytes,image.length);id<MTLLibrary> otherLibrary=nil;
 id<MTLDevice> other=create(otherChannel,image,37,&otherLibrary,&error);CHECK(other&&otherLibrary&&!error&&other!=device);
 [identities addObject:identitySnapshot(other,@"second_device")];record(@"other_initial",other,0);id<MTLBuffer> c=[other newBufferWithLength:8193 options:MTLResourceStorageModeShared];CHECK(c&&c.allocatedSize==12288);record(@"other_allocation",other,12288);record(@"first_device_isolated",device,12288);
 [a release];record(@"first_release",device,8192);[b release];record(@"all_released",device,0);
 id<MTLBuffer> full[4]{};for(unsigned i=0;i<4;++i){full[i]=[device newBufferWithLength:16777216 options:MTLResourceStorageModeShared];CHECK(full[i]);}record(@"budget_full",device,67108864);
 CHECK(![device newBufferWithLength:1 options:MTLResourceStorageModeShared]);record(@"budget_rejection",device,67108864);
 [full[0]release];record(@"budget_slot_released",device,50331648);full[0]=[device newBufferWithLength:1 options:MTLResourceStorageModeShared];CHECK(full[0]);record(@"budget_slot_reused",device,50335744);
 for(auto buffer:full)[buffer release];record(@"budget_all_released",device,0);
 @autoreleasepool {
 WrapperFixture047 *w=[[WrapperFixture047 alloc]initWithDevice:other];setWrapper(other,w);CHECK(getWrapper(other)==w);close(other);CHECK(getWrapper(other)==other);BOOL denied=NO;
 @try{setWrapper(other,w);}@catch(NSException *e){denied=[e.name isEqual:NSInternalInconsistencyException];}
 CHECK(denied);setWrapper(other,nil);CHECK(getWrapper(other)==other);closedWrapper[@"cleared_on_close"]=@YES;closedWrapper[@"attachment_rejected"]=@(denied);closedWrapper[@"nil_clear_allowed"]=@YES;[w release];
 }
 CHECK(wrapperBirths==68&&wrapperDeaths==68);closedWrapper[@"total_created"]=@(wrapperBirths.load());closedWrapper[@"total_destroyed"]=@(wrapperDeaths.load());
 [identities addObject:identitySnapshot(other,@"closed")];record(@"closed_with_live_buffer",other,12288);CHECK(![other newBufferWithLength:1 options:MTLResourceStorageModeShared]);[c release];record(@"closed_after_release",other,0);
 [otherLibrary release];[other release];otherFrames=[otherChannel->records copy];CHECK(otherChannel->backend.calls==0&&otherFrames.count==2);[otherChannel release];
 close(device);[library release];[device release];frames=[channel->records copy];fixtureCalls=channel->backend.calls;CHECK(fixtureCalls==0&&frames.count==2);[channel release];
 }
 for(unsigned i=0;i<200;++i){NSDictionary *info=featureInfo();BOOL drained=[info[@"created"]unsignedIntValue]==[info[@"destroyed"]unsignedIntValue];[info release];if(drained&&channelDestructions==2)break;usleep(5000);}
 NSDictionary *features=featureInfo();CHECK([features[@"created"]unsignedIntValue]==2&&[features[@"destroyed"]unsignedIntValue]==2&&channelDestructions==2);
 NSDictionary *result=@{@"passed":@YES,@"checks":@(checks),@"pid":@(getpid()),@"uid":@(geteuid()),@"class":NSStringFromClass(cls),@"observations":observations,@"features":features,@"frames":frames,@"other_frames":otherFrames,@"allocations":allocations,@"identities":identities,@"wrappers":wrappers,@"closed_wrapper":closedWrapper,@"resources":resources,@"fixture_calls":@(fixtureCalls),@"channel_destructions":@(channelDestructions.load()),@"device_instantiated":@YES,@"cpu_fixture_only":@YES,@"gpu_commands_submitted":@NO,@"metal_registered":@NO};
 CHECK([[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingSortedKeys error:nil]writeToFile:@(argv[4])options:NSDataWritingWithoutOverwriting error:nil]);[features release];[frames release];[otherFrames release];[wrappers release];[resources release];
 }return 0;}
