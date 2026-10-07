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
static void textureJob226(id<MTLDevice>d,id<MTLLibrary>library,Backend209&backend,bool view,unsigned invalid,MTLPixelFormat format){
 NSError*error=nil;id<MTLFunction>function=[library newFunctionWithName:@"texture_copy"];CHECK(function);
 id<MTLComputePipelineState>pipeline=[d newComputePipelineStateWithFunction:function error:&error];CHECK(pipeline&&!error);
 const unsigned pixel=format==MTLPixelFormatRGBA32Float?16:4;const unsigned pitch=pixel==16?512:256;
 auto*desc=descriptor224(invalid==1?MTLPixelFormatR32Uint:format,17,9);
 if(invalid==2)desc.usage=MTLTextureUsageShaderWrite;
 id<MTLBuffer>sourceBuffer=nil,destBuffer=nil;id<MTLTexture>source=nil,dest=nil;
 if(view){sourceBuffer=[d newBufferWithLength:8193 options:MTLResourceStorageModeShared];destBuffer=[d newBufferWithLength:8193 options:MTLResourceStorageModeShared];CHECK(sourceBuffer&&destBuffer);fill(sourceBuffer,7);fill(destBuffer,13);
  source=[sourceBuffer newTextureWithDescriptor:desc offset:256 bytesPerRow:1024]; // nine rows do not fit; rejection is required.
  CHECK(!source);source=[sourceBuffer newTextureWithDescriptor:desc offset:256 bytesPerRow:pitch];
  dest=[destBuffer newTextureWithDescriptor:descriptor224(format,17,9)offset:512 bytesPerRow:pitch];
 }else{source=[d newTextureWithDescriptor:desc];dest=[d newTextureWithDescriptor:descriptor224(format,17,9)];}
 CHECK(source&&dest);std::vector<uint32_t>pixels(17*9*pixel/4,0x3f800000);
 if(!invalid)[source replaceRegion:MTLRegionMake2D(0,0,17,9)mipmapLevel:0 withBytes:pixels.data()bytesPerRow:17*pixel];
 std::vector<uint32_t>before(17*9*pixel/4);[dest getBytes:before.data()bytesPerRow:17*pixel fromRegion:MTLRegionMake2D(0,0,17,9)mipmapLevel:0];
 std::vector<uint8_t>whole;if(view)whole=snapshot(destBuffer);
 backend.beforeReply=[dest]{uint32_t v[4]={};CHECK(textureThrows224([&]{[dest replaceRegion:MTLRegionMake2D(0,0,1,1)mipmapLevel:0 withBytes:v bytesPerRow:0];}));};
 const unsigned jobs=backend.jobs;id<MTLCommandQueue>queue=[d newCommandQueue];CHECK(queue);id<MTLCommandBuffer>command=[queue commandBuffer];CHECK(command);
 id<MTLComputeCommandEncoder>encoder=[command computeCommandEncoder];CHECK(encoder);[encoder setComputePipelineState:pipeline];
 const id<MTLTexture>textures[]={invalid==3?nil:source,dest};[encoder setTextures:textures withRange:NSMakeRange(0,2)];
 if(invalid==4)[encoder setTexture:source atIndex:128];
 [encoder dispatchThreadgroups:MTLSizeMake(3,3,1)threadsPerThreadgroup:MTLSizeMake(8,4,1)];
 // An operation owns the backing and copied descriptors despite rebinding and
 // releasing the source objects before commit.
 [encoder setTexture:nil atIndex:0];[encoder setTexture:nil atIndex:1];[source release];[sourceBuffer release];
 [encoder endEncoding];[command commit];[command waitUntilCompleted];backend.beforeReply={};
 if(invalid){CHECK(command.status==MTLCommandBufferStatusError&&command.error&&backend.jobs==jobs);}
 else{CHECK(command.status==MTLCommandBufferStatusCompleted&&!command.error&&backend.jobs==jobs+1);
  std::vector<uint32_t>actual(17*9*pixel/4);[dest getBytes:actual.data()bytesPerRow:17*pixel fromRegion:MTLRegionMake2D(0,0,17,9)mipmapLevel:0];
  auto*old=reinterpret_cast<uint8_t*>(before.data());auto*now=reinterpret_cast<uint8_t*>(actual.data());
  for(unsigned y=0;y<9;++y)for(unsigned x=0;x<17*pixel;++x){const auto index=y*17*pixel+x;const auto address=(view?512u:0u)+y*pitch+x;CHECK(now[index]==uint8_t(old[index]^uint8_t(0x5a+address%31)));}
  if(view){auto actualWhole=snapshot(destBuffer);for(size_t i=0;i<whole.size();++i)whole[i]^=uint8_t(0x5a+i%31);CHECK(actualWhole==whole);}
 }
 [destBuffer release];[dest release];[queue release];[pipeline release];[function release];
}
int main(int argc,char**argv){@autoreleasepool{
 if(argc!=4||geteuid()!=501)return 2;Backend209 backend;W::Core core;std::mutex mutex;int code=1;
 @try{try{
  NSData*image=[NSData dataWithContentsOfFile:@(argv[2])];CHECK(image&&image.length==backend.image.size());std::memcpy(backend.image.data(),image.bytes,image.length);
  CHECK(RTXCatalog187::decode(backend.image.data(),backend.image.size(),backend.catalog)&&backend.catalog.containerABI==3);backend.generation=226;backend.evidence=argv[3];CHECK(std::filesystem::create_directory(backend.evidence));
  NSBundle*bundle=[NSBundle bundleWithPath:@(argv[1])];NSError*error=nil;CHECK([bundle loadAndReturnError:&error]&&!error);
  void*handle=dlopen(bundle.executablePath.fileSystemRepresentation,RTLD_NOW|RTLD_LOCAL);CHECK(handle);
  auto create=reinterpret_cast<Create>(dlsym(handle,"RTXCreateOwnedApplication209"));auto close=reinterpret_cast<Close>(dlsym(handle,"RTXCloseOwnedApplication209"));CHECK(create&&close);
  ModelChannel209*channel=[ModelChannel209 new];channel->core=&core;channel->backend=&backend;channel->mutex=&mutex;
  id<MTLLibrary>library=nil;id<MTLDevice>device=create(channel,image,226,&library,&error);CHECK(device&&library&&!error);
  textures224(device);
  for(unsigned invalid=1;invalid<=4;++invalid)textureJob226(device,library,backend,false,invalid,MTLPixelFormatRGBA8Unorm);
  for(auto format:{MTLPixelFormatR32Float,MTLPixelFormatRGBA8Unorm,MTLPixelFormatBGRA8Unorm,MTLPixelFormatRGBA32Float}){
   textureJob226(device,library,backend,false,0,format);textureJob226(device,library,backend,true,0,format);
  }
  CHECK(backend.jobs==8);close(device);[library release];[device release];[channel release];core.stop(backend);CHECK(backend.retirements==1);code=0;
 }catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());}}@catch(NSException*e){std::fprintf(stderr,"%s\n",e.description.UTF8String);}
 std::printf("{\"passed\":%s,\"checks\":%u,\"cpu_model_jobs\":%u,\"actual_metal_objects\":true,\"actual_xpc\":false,\"gpu_jobs\":0,\"texture_arithmetic_emulated\":false}\n",code?"false":"true",checks,backend.jobs);return code;
}}
