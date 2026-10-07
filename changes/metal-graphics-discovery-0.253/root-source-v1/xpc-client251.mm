#include <utility>
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
 if(argc!=7||geteuid()!=501)return 2;int result=1;
 @try{try{
  NSBundle*bundle=[NSBundle bundleWithPath:@(argv[1])];NSError*error=nil;CHECK([bundle loadAndReturnError:&error]&&!error);void*h=dlopen(bundle.executablePath.fileSystemRepresentation,RTLD_NOW|RTLD_LOCAL);CHECK(h);
  using Create=id<MTLDevice>(*)(NSString*,NSData*,uint64_t,uint32_t,id<MTLLibrary>*,NSError**);auto create=reinterpret_cast<Create>(dlsym(h,"RTXCreateGraphicsXPCApplication251"));auto close=reinterpret_cast<void(*)(id<MTLDevice>)>(dlsym(h,"RTXCloseOwnedApplication209"));CHECK(create&&close);
  uint64_t generation=strtoull(argv[4],nullptr,10);CHECK(generation);bool cpu=std::strcmp(argv[6],"cpu")==0;CHECK(cpu||std::strcmp(argv[6],"native")==0);
  NSData*catalog=[NSData dataWithContentsOfFile:@(argv[2])];id<MTLLibrary>library=nil;id<MTLDevice>d=create(@(argv[3]),catalog,generation,5000,&library,&error);CHECK(d&&library&&!error&&d.registryID==generation);
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
   for(size_t i=0;i<color.length;++i){bool pixel=i>=256&&i<256+512*hgt&&(i-256)%512<w*4;if(!pixel)CHECK(bytes[i]==seed[i]);else if(cpu)CHECK(bytes[i]==uint8_t(seed[i]^frame));}
   NSString*dir=@(argv[5]);for(auto pair:{std::pair<NSString*,NSData*>(@"vertex",vb),{@"before",before},{@"after",[NSData dataWithBytes:color.contents length:color.length]}}){NSString*path=[dir stringByAppendingPathComponent:[NSString stringWithFormat:@"frame-%u-%@.bin",frame,pair.first]];CHECK([pair.second writeToFile:path options:NSDataWritingWithoutOverwriting error:&error]&&!error);}
   ++completed;[command release];[texture release];
  }}
  close(d);[queue release];[vertex release];[color release];[pipeline release];[library release];[d release];
  NSDictionary*report=@{@"passed":@YES,@"checks":@(checks),@"completed_commands":@(completed),@"pid":@(getpid()),@"uid":@(geteuid()),@"generation":@(generation),@"actual_xpc":@YES,@"standard_draw_primitives":@YES,@"cpu_model":@(cpu),@"gpu_execution_inferred_from_command_status":@NO};NSData*raw=[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingSortedKeys error:&error];CHECK(raw&&!error&&[raw writeToFile:[@(argv[5])stringByAppendingPathComponent:@"client-result.json"]options:NSDataWritingWithoutOverwriting error:&error]&&!error);result=0;
 }catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());}}@catch(NSException*e){std::fprintf(stderr,"%s\n",e.description.UTF8String);}return result;
}}
