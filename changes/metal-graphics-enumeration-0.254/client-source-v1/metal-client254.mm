#include <utility>
#import <objc/runtime.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <dlfcn.h>
#include <unistd.h>
#include <vector>
#include <cstring>
#include <stdexcept>
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(#x);}while(0)
int main(int argc,char**argv){@autoreleasepool{
 if(argc!=6||geteuid()!=501)return 2;int result=1;
 @try{try{
  CHECK(!getenv("DYLD_INSERT_LIBRARIES"));NSError*error=nil;
  const bool absent=std::strcmp(argv[5],"absent")==0;CHECK(absent||std::strcmp(argv[5],"gpu")==0);
  uint64_t generation=strtoull(argv[2],nullptr,10),child=strtoull(argv[3],nullptr,10);
  // Do not load a bundle or call a private device factory before enumeration.
  NSArray<id<MTLDevice>>*devices=MTLCopyAllDevices();CHECK(devices);
  NSMutableArray*inventory=[NSMutableArray array];id<MTLDevice>d=nil;unsigned rtx=0,matches=0;
  for(id<MTLDevice>device in devices){NSString*cls=NSStringFromClass(object_getClass(device));bool isRTX=[cls isEqualToString:@"RTXMetalApplicationDevice209"]||[device.name rangeOfString:@"RTX"].location!=NSNotFound;
   [inventory addObject:@{@"name":device.name?:@"",@"registry_id":@(device.registryID),@"class":cls?:@"",@"bundle":[NSBundle bundleForClass:object_getClass(device)].bundlePath?:@""}];
   if(isRTX)++rtx;if(device.registryID==child&&isRTX){++matches;d=device;}
  }
  NSDictionary*enumeration=@{@"pid":@(getpid()),@"uid":@(geteuid()),@"devices":inventory,@"rtx_devices":@(rtx),@"matching_devices":@(matches),@"standard_mtlcopyalldevices":@YES,@"private_factory_called":@NO};
  NSData*listing=[NSJSONSerialization dataWithJSONObject:enumeration options:NSJSONWritingSortedKeys error:&error];CHECK(listing&&!error&&[listing writeToFile:[@(argv[1])stringByAppendingPathComponent:@"enumeration.json"]options:NSDataWritingWithoutOverwriting error:&error]&&!error);
  if(absent){CHECK(!rtx&&!matches);[devices release];std::printf("{\"passed\":true,\"standard_metal_enumeration\":true,\"rtx_absent\":true,\"gpu_executed\":false}\n");return 0;}
  CHECK(generation&&child&&generation!=child&&rtx==1&&matches==1&&d);[d retain];[devices release];
  CHECK([NSStringFromClass(object_getClass(d))isEqualToString:@"RTXMetalApplicationDevice209"]);
  NSBundle*bundle=[NSBundle bundleForClass:object_getClass(d)];CHECK([bundle.bundlePath isEqualToString:@"/Library/GPUBundles/RTXMetalGraphics253-normal.bundle"]);
  void*h=dlopen(bundle.executablePath.fileSystemRepresentation,RTLD_NOLOAD|RTLD_LAZY|RTLD_LOCAL);CHECK(h);
  auto close=reinterpret_cast<void(*)(id<MTLDevice>)>(dlsym(h,"RTXCloseOwnedApplication209"));auto info=reinterpret_cast<NSDictionary*(*)(id<MTLDevice>)>(dlsym(h,"RTXCopyOwnedApplicationInfo209"));auto runtime=reinterpret_cast<NSDictionary*(*)(id<MTLDevice>)>(dlsym(h,"RTXCopyApplicationRuntimeInfo115"));CHECK(close&&info&&runtime);
  NSDictionary*peer=info(d),*configured=runtime(d);CHECK([peer[@"phase"]unsignedIntValue]==3&&[peer[@"generation"]unsignedLongLongValue]==generation&&[configured[@"abi"]unsignedIntValue]==253&&configured[@"graphics_only"]==(id)kCFBooleanTrue&&configured[@"automatic_configuration"]==(id)kCFBooleanTrue&&[configured[@"child_registry"]unsignedLongLongValue]==child);
  NSData*admission=[NSJSONSerialization dataWithJSONObject:@{@"peer":peer,@"runtime":configured}options:NSJSONWritingSortedKeys error:&error];CHECK(admission&&!error&&[admission writeToFile:[@(argv[1])stringByAppendingPathComponent:@"admission.json"]options:NSDataWritingWithoutOverwriting error:&error]&&!error);[peer release];[configured release];
  NSData*catalog=[NSData dataWithContentsOfFile:@(argv[4])];CHECK(catalog.length==4160);dispatch_data_t data=dispatch_data_create(catalog.bytes,catalog.length,dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),DISPATCH_DATA_DESTRUCTOR_DEFAULT);
  CHECK(data);id<MTLLibrary>library=[d newLibraryWithData:data error:&error];dispatch_release(data);CHECK(library&&!error);
  auto*descriptor=[MTLRenderPipelineDescriptor new];descriptor.vertexFunction=[[library newFunctionWithName:@"vertex_varying_triangle"]autorelease];descriptor.fragmentFunction=[[library newFunctionWithName:@"fragment_varying_color"]autorelease];
  auto*vd=[MTLVertexDescriptor vertexDescriptor];vd.attributes[0].format=MTLVertexFormatFloat2;vd.attributes[1].format=MTLVertexFormatFloat2;vd.attributes[1].offset=8;vd.layouts[0].stride=16;vd.layouts[0].stepFunction=MTLVertexStepFunctionPerVertex;vd.layouts[0].stepRate=1;descriptor.vertexDescriptor=vd;descriptor.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA8Unorm;
  id<MTLRenderPipelineState>pipeline=[d newRenderPipelineStateWithDescriptor:descriptor error:&error];CHECK(pipeline&&!error);[descriptor release];
  auto vertex=[d newBufferWithLength:113 options:MTLResourceStorageModeShared];auto color=[d newBufferWithLength:33041 options:MTLResourceStorageModeShared];CHECK(vertex&&color);
  auto queue=[d newCommandQueue];CHECK(queue);unsigned completed=0;
  for(unsigned frame=1;frame<=3;++frame){@autoreleasepool{
   unsigned w=frame==3?32:64,hgt=w;std::memset(vertex.contents,7,vertex.length);std::memset(color.contents,9,color.length);
   float vertices[]={-.75f,-.75f,0,0,.75f,-.75f,1,0,0,.75f,.5f,1};if(frame==2)for(unsigned i=0;i<3;++i){vertices[i*4]+=.03125f;vertices[i*4+1]+=.0625f;}std::memcpy(static_cast<uint8_t*>(vertex.contents)+48,vertices,sizeof(vertices));
   NSData*before=[NSData dataWithBytes:color.contents length:color.length];NSData*vb=[NSData dataWithBytes:vertex.contents length:vertex.length];
   auto*td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:w height:hgt mipmapped:NO];td.storageMode=MTLStorageModeShared;td.usage=MTLTextureUsageRenderTarget;
   auto texture=[color newTextureWithDescriptor:td offset:256 bytesPerRow:512];CHECK(texture);auto*pass=[MTLRenderPassDescriptor renderPassDescriptor];pass.colorAttachments[0].texture=texture;pass.colorAttachments[0].loadAction=MTLLoadActionLoad;pass.colorAttachments[0].storeAction=MTLStoreActionStore;
   auto command=[[queue commandBuffer]retain];CHECK(command);__block unsigned callbacks=0;[command addCompletedHandler:^(id<MTLCommandBuffer>){++callbacks;}];auto e=[command renderCommandEncoderWithDescriptor:pass];CHECK(e);[e setRenderPipelineState:pipeline];[e setVertexBuffer:vertex offset:16 atIndex:0];[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:2 vertexCount:3];[e endEncoding];[command commit];[command waitUntilCompleted];
   CHECK(command.status==MTLCommandBufferStatusCompleted&&!command.error&&callbacks==1);CHECK(!std::memcmp(vertex.contents,vb.bytes,vb.length));
   auto*bytes=static_cast<uint8_t*>(color.contents);auto*seed=static_cast<const uint8_t*>(before.bytes);
   for(size_t i=0;i<color.length;++i){bool pixel=i>=256&&i<256+512*hgt&&(i-256)%512<w*4;if(!pixel)CHECK(bytes[i]==seed[i]);}
   NSString*dir=@(argv[1]);for(auto pair:{std::pair<NSString*,NSData*>(@"vertex",vb),{@"before",before},{@"after",[NSData dataWithBytes:color.contents length:color.length]}}){NSString*path=[dir stringByAppendingPathComponent:[NSString stringWithFormat:@"frame-%u-%@.bin",frame,pair.first]];CHECK([pair.second writeToFile:path options:NSDataWritingWithoutOverwriting error:&error]&&!error);}
   ++completed;[command release];[texture release];
  }}
  close(d);[queue release];[vertex release];[color release];[pipeline release];[library release];[d release];
  NSDictionary*report=@{@"passed":@YES,@"checks":@(checks),@"completed_commands":@(completed),@"pid":@(getpid()),@"uid":@(geteuid()),@"generation":@(generation),@"actual_xpc":@YES,@"standard_draw_primitives":@YES,@"cpu_model":@NO,@"child_registry":@(child),@"standard_metal_enumeration":@YES,@"private_factory_called":@NO,@"gpu_execution_inferred_from_command_status":@NO};NSData*raw=[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingSortedKeys error:&error];CHECK(raw&&!error&&[raw writeToFile:[@(argv[1])stringByAppendingPathComponent:@"client-result.json"]options:NSDataWritingWithoutOverwriting error:&error]&&!error);result=0;
 }catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());}}@catch(NSException*e){std::fprintf(stderr,"%s\n",e.description.UTF8String);}return result;
}}
