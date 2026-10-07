#import "app/RTXApplicationClientInternal.h"
#include "app/OwnedBroker208.hpp"
#include <dlfcn.h>
#include <functional>
#include <mutex>
#include <vector>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <cstring>
#include <unistd.h>
#include <filesystem>
#include <fstream>
namespace W=RTXOwnedBroker208;namespace B=RTXBatch187;
static unsigned checks;
#define CHECK(...) do{++checks;if(!(__VA_ARGS__))throw std::runtime_error("line "+std::to_string(__LINE__)+": "+#__VA_ARGS__);}while(0)
static void write209(const std::filesystem::path&p,const void*data,size_t n){CHECK(!std::filesystem::exists(p));std::ofstream f(p,std::ios::binary);f.write(static_cast<const char*>(data),std::streamsize(n));CHECK(bool(f));}
struct Backend209 {
 std::array<uint8_t,RTXCatalog187::Bytes>image{};RTXCatalog187::Catalog catalog;uint64_t generation=0;unsigned claims=0,jobs=0,retirements=0;
 std::function<void()>beforeReply;
 std::vector<std::vector<uint8_t>>approved;std::filesystem::path evidence;unsigned admissions=0,frames=0;bool programPattern=false;
 bool claim(decltype(image)&out,uint64_t&gen,uint64_t&completed){++claims;out=image;gen=generation;completed=0;return true;}
 bool admit(const uint8_t*payload,size_t n){++admissions;if(n!=4608)return false;if(approved.empty())return !std::memcmp(payload,image.data()+640,n);for(const auto&v:approved)if(v.size()==5248&&!std::memcmp(payload,v.data()+640,n))return true;return false;}
 bool execute(const B::Bytes&request,const uint8_t*payload,size_t n,B::Bytes&result,uint64_t&completion){
  RTXProgram205::Library selected;if(n!=4608||!RTXProgram205::decode(payload,512,payload+512,4096,selected))return false;
  B::Plan p;const auto serial=W::P::get64(request.data()+24);if(!B::decode(request.data(),request.size(),selected.programs,generation,serial,p))return false;
  ++jobs;result.assign(request.begin()+B::Header,request.end());
  // Deliberately a CPU transport oracle, not the shader's GPU arithmetic.
  const uint8_t salt=programPattern?W::programHash(payload,n)[0]:0x5a;
  for(unsigned i=0;i<p.resources;++i)if(p.resource[i].access&2)for(size_t j=0;j<p.resource[i].bytes;++j)result[p.resource[i].payloadOffset+j]^=uint8_t(salt+j%31);
  if(!evidence.empty()){auto d=evidence/("job-"+std::to_string(jobs));CHECK(std::filesystem::create_directory(d));write209(d/"request.bin",request.data(),request.size());write209(d/"payload.bin",payload,n);write209(d/"result.bin",result.data(),result.size());}
  if(beforeReply)beforeReply();completion=serial;return true;
 }
 void retire(){++retirements;}
};
@interface ModelChannel209:NSObject<RTXOwnedChannel208>{@public W::Core*core;Backend209*backend;std::mutex*mutex;W::Peer peer;BOOL cancelled;}
@end
@implementation ModelChannel209
- (BOOL)exchange:(NSData*)request reply:(NSData**)reply serverUID:(uint32_t*)uid serverPID:(int64_t*)pid{
 std::lock_guard<std::mutex> lock(*mutex);if(cancelled)return NO;
 auto result=core->receive(peer,static_cast<const uint8_t*>(request.bytes),request.length,true,*backend);
 if(!backend->evidence.empty()){auto d=backend->evidence/("frame-"+std::to_string(++backend->frames));CHECK(std::filesystem::create_directory(d));write209(d/"request.bin",request.bytes,request.length);write209(d/"reply.bin",result.bytes.data(),result.size());}
 *reply=[NSData dataWithBytes:result.bytes.data()length:result.size()];*uid=0;*pid=222;return YES;
}
- (void)cancel{cancelled=YES;}
@end
using Create=id<MTLDevice>(*)(id<RTXOwnedChannel208>,NSData*,uint64_t,id<MTLLibrary>*,NSError**);
using Close=void(*)(id<MTLDevice>);using Info=NSDictionary*(*)(id<MTLDevice>);
static std::vector<uint8_t>snapshot(id<MTLBuffer>b){auto*p=static_cast<const uint8_t*>(b.contents);CHECK(p);return {p,p+b.length};}
static void fill(id<MTLBuffer>b,unsigned salt){auto*p=static_cast<uint8_t*>(b.contents);CHECK(p);for(NSUInteger i=0;i<b.length;++i)p[i]=uint8_t(i*17+salt);}
#include "texture-objects224.inc"

#include <cmath>
static unsigned clearCases231;
static void clearCase231(id<MTLDevice>d,Backend209&backend,MTLPixelFormat format,bool view,unsigned attachments=1,unsigned passes=1,unsigned fault=0,bool unknown=false){
 const unsigned caseId=++clearCases231,first=backend.jobs;
 const auto directory=backend.evidence/("case-"+std::to_string(caseId));CHECK(std::filesystem::create_directory(directory));
 const uint32_t metadata[]={caseId,unsigned(format),unsigned(view),attachments,passes,fault,unsigned(unknown),first};write209(directory/"case.bin",metadata,sizeof(metadata));
 auto*pass=[MTLRenderPassDescriptor renderPassDescriptor];std::vector<id<MTLTexture>>textures;std::vector<id<MTLBuffer>>backings;std::vector<std::vector<uint8_t>>before;
 for(unsigned i=0;i<attachments;++i){
  auto*td=descriptor224(format,17,3);td.usage=fault==1?MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite:unknown?MTLTextureUsageUnknown:MTLTextureUsageRenderTarget;
  id<MTLBuffer>backing=view?[d newBufferWithLength:8193 options:MTLResourceStorageModeShared]:nil;if(backing)fill(backing,13+i);
  const unsigned pitch=format==MTLPixelFormatRGBA32Float?512:256,pixel=format==MTLPixelFormatRGBA32Float?16:4;
  id<MTLTexture>texture=view?[backing newTextureWithDescriptor:td offset:256 bytesPerRow:pitch]:[d newTextureWithDescriptor:td];CHECK(texture);
  std::vector<uint8_t>pixels(17*3*pixel,uint8_t(0x31+i));[texture replaceRegion:MTLRegionMake2D(0,0,17,3)mipmapLevel:0 withBytes:pixels.data()bytesPerRow:17*pixel];
  std::vector<uint8_t>saved=view?snapshot(backing):pixels;before.push_back(saved);backings.push_back(backing);textures.push_back(texture);
  pass.colorAttachments[i].texture=texture;pass.colorAttachments[i].loadAction=MTLLoadActionClear;pass.colorAttachments[i].storeAction=MTLStoreActionStore;pass.colorAttachments[i].clearColor=MTLClearColorMake(i+.25,.1,.5,1.5);
 }
 if(fault==2)pass.colorAttachments[0].clearColor=MTLClearColorMake(NAN,0,0,1);
 if(fault==3)pass.colorAttachments[0].loadAction=MTLLoadActionLoad;
 if(fault==4)pass.colorAttachments[0].storeAction=MTLStoreActionDontCare;
 if(fault==5)pass.colorAttachments[0].level=1;
 if(fault==6)for(unsigned i=0;i<attachments;++i)pass.colorAttachments[i].texture=nil;
 if(fault==7){id<MTLBuffer>visibility=[d newBufferWithLength:256 options:MTLResourceStorageModeShared];CHECK(visibility);pass.visibilityResultBuffer=visibility;[visibility release];}
 if(fault==10){pass.colorAttachments[1].texture=textures[0];pass.colorAttachments[1].loadAction=MTLLoadActionClear;pass.colorAttachments[1].storeAction=MTLStoreActionStore;}
 if(fault==12)pass.renderTargetWidth=18;
 if(fault==13)pass.defaultRasterSampleCount=4;
 id<MTLCommandQueue>queue=[d newCommandQueue];CHECK(queue);id<MTLCommandBuffer>command=[[queue commandBuffer]retain];CHECK(command);
 backend.beforeReply=[&]{unsigned i=(backend.jobs-first-1)%attachments;uint32_t scratch[4]={};CHECK(textureThrows224([&]{[textures[i] replaceRegion:MTLRegionMake2D(0,0,1,1)mipmapLevel:0 withBytes:scratch bytesPerRow:0];}));};
 for(unsigned iteration=0;iteration<passes;++iteration){@autoreleasepool{
  id<MTLRenderCommandEncoder>encoder=[command renderCommandEncoderWithDescriptor:pass];
  const bool deferred=fault==8||fault==9||fault==11;
  if(fault&&!deferred){CHECK(!encoder);break;}
  CHECK(encoder&&[encoder conformsToProtocol:@protocol(MTLRenderCommandEncoder)]&&encoder.device==d);
  encoder.label=@"Color clear231";[encoder pushDebugGroup:@"clear"];[encoder popDebugGroup];
  if(fault==8)[encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
  if(fault==9)CHECK(![command computeCommandEncoder]);
  if(fault!=11)[encoder endEncoding]; // Scope exit tests the unended-encoder destructor.
 }}
 // Render-pass attachment and clear arguments are copied at encoder creation.
 for(unsigned i=0;i<attachments;++i){pass.colorAttachments[i].clearColor=MTLClearColorMake(0,0,0,0);pass.colorAttachments[i].texture=nil;}
 [command commit];[command waitUntilCompleted];backend.beforeReply={};
 if(fault)CHECK(command.status==MTLCommandBufferStatusError&&command.error&&backend.jobs==first);
 else{
  CHECK(command.status==MTLCommandBufferStatusCompleted&&!command.error&&backend.jobs==first+attachments*passes);
  for(unsigned i=0;i<attachments;++i){const unsigned pixel=format==MTLPixelFormatRGBA32Float?16:4,pitch=pixel==16?512:256;
   if(view){auto expected=before[i];for(unsigned j=0;j<passes;++j)for(size_t n=0;n<expected.size();++n)expected[n]^=uint8_t(0x5a+n%31);CHECK(snapshot(backings[i])==expected);}
   else{std::vector<uint8_t>actual(before[i].size()),expected=before[i];[textures[i] getBytes:actual.data()bytesPerRow:17*pixel fromRegion:MTLRegionMake2D(0,0,17,3)mipmapLevel:0];
    for(unsigned j=0;j<passes;++j)for(unsigned y=0;y<3;++y)for(unsigned x=0;x<17*pixel;++x)expected[y*17*pixel+x]^=uint8_t(0x5a+(y*pitch+x)%31);CHECK(actual==expected);}
  }
 }
 for(auto texture:textures)[texture release];for(auto backing:backings)[backing release];[command release];[queue release];
}
int main(int argc,char**argv){@autoreleasepool{
 if(argc!=4||geteuid()!=501)return 2;Backend209 backend;W::Core core;std::mutex mutex;int code=1;
 @try{try{
  NSData*image=[NSData dataWithContentsOfFile:@(argv[2])];CHECK(image&&image.length==backend.image.size());std::memcpy(backend.image.data(),image.bytes,image.length);
  CHECK(RTXCatalog187::decode(backend.image.data(),backend.image.size(),backend.catalog)&&backend.catalog.containerABI==3);backend.generation=231;backend.evidence=argv[3];CHECK(std::filesystem::create_directory(backend.evidence));
  NSBundle*bundle=[NSBundle bundleWithPath:@(argv[1])];NSError*error=nil;CHECK([bundle loadAndReturnError:&error]&&!error);
  void*handle=dlopen(bundle.executablePath.fileSystemRepresentation,RTLD_NOW|RTLD_LOCAL);CHECK(handle);
  auto create=reinterpret_cast<Create>(dlsym(handle,"RTXCreateOwnedApplication209"));auto close=reinterpret_cast<Close>(dlsym(handle,"RTXCloseOwnedApplication209"));CHECK(create&&close);
  ModelChannel209*channel=[ModelChannel209 new];channel->core=&core;channel->backend=&backend;channel->mutex=&mutex;
  id<MTLLibrary>library=nil;id<MTLDevice>device=create(channel,image,231,&library,&error);CHECK(device&&library&&!error);
  textures224(device);
  for(unsigned fault=1;fault<=13;++fault)clearCase231(device,backend,MTLPixelFormatRGBA8Unorm,true,1,1,fault);
  for(auto format:{MTLPixelFormatR32Float,MTLPixelFormatRGBA8Unorm,MTLPixelFormatBGRA8Unorm,MTLPixelFormatRGBA32Float}){
   clearCase231(device,backend,format,false);clearCase231(device,backend,format,true);
  }
  clearCase231(device,backend,MTLPixelFormatRGBA8Unorm,true,4);
  clearCase231(device,backend,MTLPixelFormatRGBA32Float,true,1,2);
  clearCase231(device,backend,MTLPixelFormatR32Float,false,1,1,0,true);
  CHECK(backend.jobs==15);close(device);[library release];[device release];[channel release];core.stop(backend);CHECK(backend.retirements==1);code=0;
 }catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());}}@catch(NSException*e){std::fprintf(stderr,"%s\n",e.description.UTF8String);}
 std::printf("{\"passed\":%s,\"checks\":%u,\"cases\":%u,\"cpu_model_jobs\":%u,\"actual_metal_objects\":true,\"actual_render_pass_objects\":true,\"actual_xpc\":false,\"gpu_jobs\":0,\"texture_arithmetic_emulated\":false}\n",code?"false":"true",checks,clearCases231,backend.jobs);return code;
}}
