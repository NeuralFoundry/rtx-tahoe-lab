#include "fixture-draw248.inc"
#include "app/OwnedGraphicsBroker251.hpp"
namespace G251=RTXOwnedGraphicsBroker251;
struct GraphicsBackend251{
 uint64_t done=0;unsigned calls=0,claims=0,retired=0;bool corrupt=false;
 uint64_t generation()const{return 251;}
 bool claim(uint64_t&gen,uint64_t&complete){++claims;gen=251;complete=0;return true;}
 bool execute(const G251::Bytes&r,G251::Bytes&out,uint64_t&complete){
  namespace D=RTXDrawTransfer248;D::Plan p;CHECK(G251::decode(r,251,done+1,p));++calls;complete=++done;out.assign(r.begin()+D::Header,r.end());
  for(unsigned y=0;y<p.height;++y)for(unsigned x=0;x<p.width*4;++x)out[size_t(p.vertexBytes+p.colorOffset+y*p.pitch+x)]^=uint8_t(done);
  if(corrupt)out[0]^=1;return true;
 }
 void retire(){++retired;}
};
@interface GraphicsChannel251:NSObject<RTXOwnedChannel208>{@public GraphicsBackend251*backend;G251::Core*core;G251::Peer peer;std::mutex*mutex;bool cancelled;unsigned mode;}
@end
@implementation GraphicsChannel251
- (BOOL)exchange:(NSData*)request reply:(NSData**)reply serverUID:(uint32_t*)uid serverPID:(int64_t*)pid{
 std::lock_guard<std::mutex>lock(*mutex);if(cancelled)return NO;
 auto out=core->receive(peer,request.bytes,request.length,true,*backend);
 if(backend->calls&&mode==1)return NO;
 if(backend->calls&&mode==2)[NSException raise:NSInvalidArgumentException format:@"Lost graphics reply"];
 *reply=[NSData dataWithBytes:out.bytes.data()length:out.size()];*uid=0;*pid=123;return YES;
}
- (void)cancel{cancelled=true;}
@end
using Create251=id<MTLDevice>(*)(id<RTXOwnedChannel208>,NSData*,uint64_t,id<MTLLibrary>*,NSError**);
static unsigned publications251=0;
using Consume253=id<MTLDevice>(*)(id<MTLDevice>,id<RTXOwnedChannel208>,NSData*,uint64_t,id<MTLLibrary>*,NSError**);
static Consume253 consume253=nullptr;static Class deviceClass253=Nil;static bool initialized253=false;
static void case251(Create251 create,Close close,NSData*catalog,unsigned mode){
 GraphicsBackend251 backend;backend.corrupt=mode==3;G251::Core core;std::mutex mutex;GraphicsChannel251*channel=[GraphicsChannel251 new];channel->backend=&backend;channel->core=&core;channel->mutex=&mutex;channel->mode=mode;
 NSError*error=nil;id<MTLLibrary>library=nil;id<MTLDevice>d=initialized253?consume253([[deviceClass253 alloc]init],channel,catalog,251,&library,&error):create(channel,catalog,251,&library,&error);CHECK(d&&library&&!error&&d.registryID==251&&backend.claims==1&&backend.calls==0);
 id<MTLRenderPipelineState>pipeline=[d newRenderPipelineStateWithDescriptor:descriptor247(library)error:&error];CHECK(pipeline&&!error);
 id<MTLBuffer>vertex=[d newBufferWithLength:113 options:MTLResourceStorageModeShared],color=[d newBufferWithLength:33041 options:MTLResourceStorageModeShared];CHECK(vertex&&color);fill(vertex,9);fill(color,7);
 float values[]={-.75f,-.75f,0,0,.75f,-.75f,1,0,0,.75f,.5f,1};std::memcpy(static_cast<uint8_t*>(vertex.contents)+48,values,sizeof(values));auto before=snapshot(color);auto initialVertex=snapshot(vertex);
 auto*td=descriptor224(MTLPixelFormatRGBA8Unorm,64,64);td.usage=MTLTextureUsageRenderTarget;id<MTLTexture>texture=[color newTextureWithDescriptor:td offset:256 bytesPerRow:512];CHECK(texture);
 auto*pass=[MTLRenderPassDescriptor renderPassDescriptor];pass.colorAttachments[0].texture=texture;pass.colorAttachments[0].loadAction=mode==5?MTLLoadActionClear:MTLLoadActionLoad;pass.colorAttachments[0].storeAction=MTLStoreActionStore;
 id<MTLCommandQueue>queue=[d newCommandQueue];CHECK(queue);id<MTLCommandBuffer>command=[[queue commandBuffer]retain];CHECK(command);
 __block unsigned complete=0;[command addCompletedHandler:^(id<MTLCommandBuffer>){++complete;}];
 if(mode==4){CHECK(![command computeCommandEncoder]);}
 else{auto e=[command renderCommandEncoderWithDescriptor:pass];if(mode==5)CHECK(!e);else{
  CHECK(e);[e setRenderPipelineState:pipeline];[e setVertexBuffer:vertex offset:16 atIndex:0];[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:2 vertexCount:3];
  if(!mode)[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:2 vertexCount:3];[e endEncoding];
 }}
 [command commit];[command waitUntilCompleted];CHECK(complete==1&&snapshot(vertex)==initialVertex);
 if(!mode){CHECK(command.status==MTLCommandBufferStatusCompleted&&!command.error&&backend.calls==2);for(unsigned y=0;y<64;++y)for(unsigned x=0;x<256;++x)before[256+y*512+x]^=3;CHECK(snapshot(color)==before);publications251+=2;}
 else{CHECK(command.status==MTLCommandBufferStatusError&&command.error&&snapshot(color)==before);CHECK(backend.calls==(mode<=3?1u:0u));if(mode<=3)CHECK(![queue commandBuffer]);}
 close(d);[command release];[queue release];[texture release];[vertex release];[color release];[pipeline release];[library release];[d release];[channel release];core.stop(backend);CHECK(backend.retired==1);
}
int main(int argc,char**argv){@autoreleasepool{
 if(argc!=3||geteuid()!=501)return 2;int code=1;
 @try{try{NSBundle*b=[NSBundle bundleWithPath:@(argv[1])];NSError*error=nil;CHECK([b loadAndReturnError:&error]&&!error);void*h=dlopen(b.executablePath.fileSystemRepresentation,RTLD_NOW|RTLD_LOCAL);CHECK(h);
  auto create=reinterpret_cast<Create251>(dlsym(h,"RTXCreateGraphicsApplication251"));auto close=reinterpret_cast<Close>(dlsym(h,"RTXCloseOwnedApplication209"));CHECK(create&&close);NSData*catalog=[NSData dataWithContentsOfFile:@(argv[2])];
  consume253=reinterpret_cast<Consume253>(dlsym(h,"RTXConsumeInitializedGraphicsChannel253"));deviceClass253=b.principalClass;CHECK(consume253&&deviceClass253);
  for(unsigned path=0;path<2;++path){initialized253=path;for(unsigned mode=0;mode<6;++mode){@autoreleasepool{case251(create,close,catalog,mode);}}}
  code=0;
 }catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());}}@catch(NSException*e){std::fprintf(stderr,"%s\n",e.description.UTF8String);}
 std::printf("{\"passed\":%s,\"checks\":%u,\"cases\":12,\"publications\":%u,\"standard_draw_api\":true,\"broker_core_and_client\":true,\"actual_xpc\":false,\"gpu_jobs\":0}\n",code?"false":"true",checks,publications251);return code;
}}
