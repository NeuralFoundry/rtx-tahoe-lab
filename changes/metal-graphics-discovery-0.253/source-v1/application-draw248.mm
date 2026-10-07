#define main inherited_clear231_main
#include "application-clear231.mm"
#undef main
static id<MTLLibrary> loadGraphics247(id<MTLDevice>device,NSData*bytes,NSError**error){
 dispatch_data_t data=dispatch_data_create(bytes.bytes,bytes.length,dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),DISPATCH_DATA_DESTRUCTOR_DEFAULT);
 id<MTLLibrary> result=[device newLibraryWithData:data error:error];dispatch_release(data);return result;
}
static MTLRenderPipelineDescriptor*descriptor247(id<MTLLibrary>library){
 auto*d=[MTLRenderPipelineDescriptor new];d.vertexFunction=[[library newFunctionWithName:@"vertex_varying_triangle"]autorelease];d.fragmentFunction=[[library newFunctionWithName:@"fragment_varying_color"]autorelease];d.label=@"retained pipeline247";
 auto*v=[MTLVertexDescriptor vertexDescriptor];v.attributes[0].format=MTLVertexFormatFloat2;v.attributes[1].format=MTLVertexFormatFloat2;v.attributes[1].offset=8;v.layouts[0].stride=16;v.layouts[0].stepFunction=MTLVertexStepFunctionPerVertex;v.layouts[0].stepRate=1;d.vertexDescriptor=v;d.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA8Unorm;return [d autorelease];
}
#include "app/RTXDrawTransfer248.hpp"
@interface ModelGraphics248:NSObject<RTXGraphicsCommandTransport248>{@public unsigned calls,mode;uint64_t generation;std::function<void()>beforeReply;}
@end
@implementation ModelGraphics248
- (BOOL)executeGraphicsRequest:(NSData*)request libraryContainer:(NSData*)container result:(NSData**)result completion:(uint64_t*)completion error:(NSError**)error{
 namespace D=RTXDrawTransfer248;if(error)*error=nil;*result=nil;*completion=0;D::Digest digest{};CHECK(container.length==4160);std::memcpy(digest.data(),static_cast<const uint8_t*>(container.bytes)+16,32);
 D::Plan p;CHECK(D::decode(request.bytes,request.length,generation,calls+1,digest,p));CHECK(p.firstVertex==2&&p.vertexOffset==16&&p.vertexCount==3&&p.width==64&&p.height==64&&p.pitch==512);++calls;
 D::Bytes reply(static_cast<const uint8_t*>(request.bytes)+D::Header,static_cast<const uint8_t*>(request.bytes)+request.length);
 // Deliberately non-raster CPU sentinel data to test transfer/publication only.
 for(unsigned y=0;y<p.height;++y)for(unsigned x=0;x<p.width*4;++x)reply[size_t(p.vertexBytes+p.colorOffset+uint64_t(y)*p.pitch+x)]^=uint8_t(0x5a+calls);
 if(beforeReply)beforeReply();*completion=p.serial+(mode==1?1:0);
 if(mode==2)reply[size_t(p.vertexBytes+p.colorOffset+256)]^=1;
 if(mode==3)reply[0]^=1;if(mode==4)reply.pop_back();if(mode==7)[NSException raise:NSInvalidArgumentException format:@"Injected graphics transport exception"];
 *result=[NSData dataWithBytes:reply.data()length:reply.size()];return YES;
}
@end
static unsigned cases248=0,modelCalls248=0,negative248=0;
static void drawCase248(Create create,Close close,BOOL(*configure)(id<MTLDevice>,id<RTXGraphicsCommandTransport248>),NSData*initial,NSData*catalog,unsigned fault){
 Backend209 backend;W::Core core;std::mutex mutex;std::memcpy(backend.image.data(),initial.bytes,initial.length);CHECK(RTXCatalog187::decode(backend.image.data(),backend.image.size(),backend.catalog));backend.generation=248+fault;
 ModelChannel209*channel=[ModelChannel209 new];channel->core=&core;channel->backend=&backend;channel->mutex=&mutex;NSError*error=nil;id<MTLLibrary>compute=nil;id<MTLDevice>d=create(channel,initial,backend.generation,&compute,&error);CHECK(d&&compute&&!error);
 ModelGraphics248*transport=[ModelGraphics248 new];transport->generation=backend.generation;transport->mode=fault<8?fault:0;CHECK(configure(d,transport)&&!configure(d,transport));
 id<MTLLibrary>graphics=loadGraphics247(d,catalog,&error);CHECK(graphics&&!error);id<MTLRenderPipelineState>pipeline=[d newRenderPipelineStateWithDescriptor:descriptor247(graphics)error:&error];CHECK(pipeline&&!error);
 id<MTLBuffer>vertex=[d newBufferWithLength:113 options:MTLResourceStorageModeShared],color=[d newBufferWithLength:33041 options:MTLResourceStorageModeShared];CHECK(vertex&&color);fill(vertex,9);fill(color,7);
 const float values[]={-.75f,-.75f,0,0,.75f,-.75f,1,0,0,.75f,.5f,1};std::memcpy(static_cast<uint8_t*>(vertex.contents)+48,values,sizeof(values));
 auto*td=descriptor224(MTLPixelFormatRGBA8Unorm,64,64);td.usage=MTLTextureUsageRenderTarget;id<MTLTexture>texture=[color newTextureWithDescriptor:td offset:256 bytesPerRow:512];CHECK(texture);
 auto*pass=[MTLRenderPassDescriptor renderPassDescriptor];pass.colorAttachments[0].texture=texture;pass.colorAttachments[0].loadAction=MTLLoadActionLoad;pass.colorAttachments[0].storeAction=MTLStoreActionStore;
 auto before=snapshot(color);auto initialVertex=snapshot(vertex);id<MTLCommandQueue>queue=[d newCommandQueue];CHECK(queue&&!configure(d,transport));id<MTLCommandBuffer>command=[[queue commandBuffer]retain];CHECK(command);
 __block unsigned scheduled=0,completed=0;[command addScheduledHandler:^(id<MTLCommandBuffer>){++scheduled;}];[command addCompletedHandler:^(id<MTLCommandBuffer>){++completed;}];
 id<MTLRenderCommandEncoder>e=[command renderCommandEncoderWithDescriptor:pass];CHECK(e&&[e conformsToProtocol:@protocol(MTLRenderCommandEncoder)]&&e.device==d);
 if(fault!=8)[e setRenderPipelineState:pipeline];[e setVertexBuffer:fault==9?nil:vertex offset:16 atIndex:0];[e setViewport:MTLViewport{0,0,fault==10?63.0:64.0,64,0,1}];[e setScissorRect:MTLScissorRect{0,0,64,64}];[e setCullMode:MTLCullModeNone];[e setTriangleFillMode:MTLTriangleFillModeFill];
 [e drawPrimitives:fault==11?MTLPrimitiveTypeLine:MTLPrimitiveTypeTriangle vertexStart:fault==12?NSUIntegerMax:2 vertexCount:fault==13?4:3];
 if(!fault)[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:2 vertexCount:3];
 [e setVertexBuffer:nil offset:0 atIndex:0]; // Encoded operations retain their own bindings.
 [e endEncoding];pass.colorAttachments[0].texture=nil;
 transport->beforeReply=[&]{uint32_t pixel=0;CHECK(textureThrows224([&]{[texture replaceRegion:MTLRegionMake2D(0,0,1,1)mipmapLevel:0 withBytes:&pixel bytesPerRow:0];}));if(fault==5)static_cast<uint8_t*>(color.contents)[0]^=0x31;if(fault==6)close(d);};
 [command commit];[command waitUntilCompleted];transport->beforeReply={};CHECK(scheduled==1&&completed==1&&backend.jobs==0);
 auto after=snapshot(color);CHECK(snapshot(vertex)==initialVertex);
 if(!fault){CHECK(command.status==MTLCommandBufferStatusCompleted&&!command.error&&transport->calls==2);for(unsigned y=0;y<64;++y)for(unsigned x=0;x<256;++x)before[256+y*512+x]^=uint8_t(0x5b^0x5c);CHECK(after==before);}
 else{++negative248;CHECK(command.status==MTLCommandBufferStatusError&&command.error);if(fault==5)before[0]^=0x31;CHECK(after==before&&transport->calls==(fault<8?1u:0u));if(fault<8)CHECK(![queue commandBuffer]);}
 if(fault!=6){uint32_t pixel=0;[texture replaceRegion:MTLRegionMake2D(0,0,1,1)mipmapLevel:0 withBytes:&pixel bytesPerRow:0];}
 modelCalls248+=transport->calls;++cases248;[command release];[queue release];[texture release];[vertex release];[color release];[pipeline release];[graphics release];close(d);[compute release];[d release];[channel release];[transport release];core.stop(backend);CHECK(backend.retirements==1);
}
int main(int argc,char**argv){@autoreleasepool{
 if(argc!=4||geteuid()!=501)return 2;int code=1;
 @try{try{NSBundle*bundle=[NSBundle bundleWithPath:@(argv[1])];NSError*error=nil;CHECK([bundle loadAndReturnError:&error]&&!error);void*handle=dlopen(bundle.executablePath.fileSystemRepresentation,RTLD_NOW|RTLD_LOCAL);CHECK(handle);
 auto create=reinterpret_cast<Create>(dlsym(handle,"RTXCreateOwnedApplication209"));auto close=reinterpret_cast<Close>(dlsym(handle,"RTXCloseOwnedApplication209"));auto configure=reinterpret_cast<BOOL(*)(id<MTLDevice>,id<RTXGraphicsCommandTransport248>)>(dlsym(handle,"RTXConfigureGraphicsCommandTransport248"));CHECK(create&&close&&configure);
 NSData*initial=[NSData dataWithContentsOfFile:@(argv[2])],*catalog=[NSData dataWithContentsOfFile:@(argv[3])];CHECK(initial.length==5248&&catalog.length==4160);
 for(unsigned fault=0;fault<14;++fault){@autoreleasepool{drawCase248(create,close,configure,initial,catalog,fault);}}
 code=0;}catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());}}@catch(NSException*e){std::fprintf(stderr,"%s\n",e.description.UTF8String);}
 std::printf("{\"passed\":%s,\"checks\":%u,\"cases\":%u,\"negative_cases\":%u,\"cpu_transport_calls\":%u,\"standard_draw_primitives_api\":true,\"actual_xpc\":false,\"gpu_jobs\":0,\"pixel_arithmetic_emulated\":false}\n",code?"false":"true",checks,cases248,negative248,modelCalls248);return code;
}}
