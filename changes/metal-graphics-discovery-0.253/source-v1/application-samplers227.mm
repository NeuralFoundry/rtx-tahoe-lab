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
static MTLSamplerDescriptor*samplerDescriptor227(unsigned n,unsigned s,unsigned t,unsigned filtering){
 auto*d=[MTLSamplerDescriptor new];const MTLSamplerAddressMode modes[]={MTLSamplerAddressModeClampToEdge,MTLSamplerAddressModeRepeat,MTLSamplerAddressModeMirrorRepeat,MTLSamplerAddressModeClampToZero};
 d.normalizedCoordinates=n!=0;d.sAddressMode=modes[s];d.tAddressMode=modes[t];d.minFilter=d.magFilter=filtering?MTLSamplerMinMagFilterLinear:MTLSamplerMinMagFilterNearest;d.label=@"sampler original";return[d autorelease];
}
static void samplerFactories227(id<MTLDevice>d){
 for(unsigned invalid=0;invalid<9;++invalid){auto*desc=samplerDescriptor227(1,0,0,0);
  switch(invalid){case 0:desc.minFilter=MTLSamplerMinMagFilterLinear;break;case 1:desc.mipFilter=MTLSamplerMipFilterNearest;break;
  case 2:desc.maxAnisotropy=2;break;case 3:desc.compareFunction=MTLCompareFunctionLess;break;case 4:desc.supportArgumentBuffers=YES;break;
  case 5:desc.lodMinClamp=1;break;case 6:desc.sAddressMode=MTLSamplerAddressModeClampToBorderColor;break;
  case 7:desc.tAddressMode=MTLSamplerAddressModeMirrorClampToEdge;break;default:desc.normalizedCoordinates=NO;desc.sAddressMode=MTLSamplerAddressModeRepeat;}
  CHECK(![d newSamplerStateWithDescriptor:desc]);
 }
}
static void samplerJob227(id<MTLDevice>d,id<MTLLibrary>library,Backend209&backend,unsigned normalized,unsigned s,unsigned t,unsigned filtering,unsigned invalid){
 NSError*error=nil;id<MTLFunction>function=[library newFunctionWithName:@"sample_texture227"];CHECK(function);
 id<MTLComputePipelineState>pipeline=[d newComputePipelineStateWithFunction:function error:&error];CHECK(pipeline&&!error);
 auto*descriptor=samplerDescriptor227(normalized,s,t,filtering);id<MTLSamplerState>sampler=[d newSamplerStateWithDescriptor:descriptor];CHECK(sampler&&sampler.device==d&&[sampler.label isEqualToString:@"sampler original"]);
 descriptor.label=@"mutated descriptor";descriptor.minFilter=descriptor.magFilter=filtering?MTLSamplerMinMagFilterNearest:MTLSamplerMinMagFilterLinear;CHECK([sampler.label isEqualToString:@"sampler original"]);
 const MTLPixelFormat formats[]={MTLPixelFormatR32Float,MTLPixelFormatRGBA8Unorm,MTLPixelFormatBGRA8Unorm,MTLPixelFormatRGBA32Float};const unsigned formatIndex=backend.jobs%4;
 id<MTLTexture>texture=[d newTextureWithDescriptor:descriptor224(formats[formatIndex],8,4)];CHECK(texture);
 std::vector<uint8_t>pixels(8*4*16,0x31);[texture replaceRegion:MTLRegionMake2D(0,0,8,4)mipmapLevel:0 withBytes:pixels.data()bytesPerRow:8*(formatIndex==3?16:4)];
 id<MTLBuffer>uv=[d newBufferWithLength:4097 options:MTLResourceStorageModeShared],output=[d newBufferWithLength:4097 options:MTLResourceStorageModeShared];CHECK(uv&&output);fill(uv,29);fill(output,47);auto before=snapshot(output);
 backend.beforeReply=[texture]{uint32_t pixels[4]={};CHECK(textureThrows224([&]{[texture replaceRegion:MTLRegionMake2D(0,0,1,1)mipmapLevel:0 withBytes:pixels bytesPerRow:0];}));};
 const unsigned jobs=backend.jobs;id<MTLCommandQueue>queue=[d newCommandQueue];CHECK(queue);id<MTLCommandBuffer>command=[queue commandBuffer];CHECK(command);id<MTLComputeCommandEncoder>encoder=[command computeCommandEncoder];CHECK(encoder);
 [encoder setComputePipelineState:pipeline];[encoder setBuffer:uv offset:0 atIndex:0];[encoder setBuffer:output offset:0 atIndex:1];[encoder setTexture:invalid==5?nil:texture atIndex:0];
 const id<MTLSamplerState>states[]={sampler};float low[]={0},high[]={65504};
 switch(jobs%4){case 0:[encoder setSamplerState:sampler atIndex:0];break;case 1:[encoder setSamplerStates:states withRange:NSMakeRange(0,1)];break;
 case 2:[encoder setSamplerState:sampler lodMinClamp:0 lodMaxClamp:65504 atIndex:0];break;default:[encoder setSamplerStates:states lodMinClamps:low lodMaxClamps:high withRange:NSMakeRange(0,1)];}
 if(invalid==1)[encoder setSamplerState:nil atIndex:0];
 if(invalid==2)[encoder setSamplerState:sampler atIndex:16];
 if(invalid==3){id foreign=[NSObject new];[encoder setSamplerState:(id<MTLSamplerState>)foreign atIndex:0];[foreign release];}
 if(invalid==4)[encoder setSamplerState:sampler lodMinClamp:1 lodMaxClamp:2 atIndex:0];
 [encoder dispatchThreadgroups:MTLSizeMake(2,1,1)threadsPerThreadgroup:MTLSizeMake(64,1,1)];
 [encoder setSamplerState:nil atIndex:0];[encoder setTexture:nil atIndex:0];[sampler release];[uv release];
 [encoder endEncoding];[command commit];[command waitUntilCompleted];backend.beforeReply={};
 if(invalid){CHECK(command.status==MTLCommandBufferStatusError&&command.error&&backend.jobs==jobs);CHECK(snapshot(output)==before);}
 else{
  CHECK(command.status==MTLCommandBufferStatusCompleted&&!command.error&&backend.jobs==jobs+1);for(size_t i=0;i<before.size();++i)before[i]^=uint8_t(0x5a+i%31);CHECK(snapshot(output)==before);
  const unsigned internalFormats[]={3,4,7,10};NSDictionary*expect=@{@"normalized":@(normalized),@"s":@(s),@"t":@(t),@"filter":@(filtering),@"format":@(internalFormats[formatIndex])};
  NSData*json=[NSJSONSerialization dataWithJSONObject:expect options:NSJSONWritingSortedKeys error:&error];CHECK(json&&!error);auto path=backend.evidence/("job-"+std::to_string(backend.jobs))/"api-input.json";write209(path,json.bytes,json.length);
 }
 [texture release];[output release];[queue release];[pipeline release];[function release];
}
int main(int argc,char**argv){@autoreleasepool{
 if(argc!=4||geteuid()!=501)return 2;Backend209 backend;W::Core core;std::mutex mutex;int code=1;
 @try{try{
  NSData*image=[NSData dataWithContentsOfFile:@(argv[2])];CHECK(image&&image.length==backend.image.size());std::memcpy(backend.image.data(),image.bytes,image.length);
  CHECK(RTXCatalog187::decode(backend.image.data(),backend.image.size(),backend.catalog)&&backend.catalog.containerABI==3);backend.generation=227;backend.evidence=argv[3];CHECK(std::filesystem::create_directory(backend.evidence));
  NSBundle*bundle=[NSBundle bundleWithPath:@(argv[1])];NSError*error=nil;CHECK([bundle loadAndReturnError:&error]&&!error);
  void*handle=dlopen(bundle.executablePath.fileSystemRepresentation,RTLD_NOW|RTLD_LOCAL);CHECK(handle);
  auto create=reinterpret_cast<Create>(dlsym(handle,"RTXCreateOwnedApplication209"));auto close=reinterpret_cast<Close>(dlsym(handle,"RTXCloseOwnedApplication209"));CHECK(create&&close);
  ModelChannel209*channel=[ModelChannel209 new];channel->core=&core;channel->backend=&backend;channel->mutex=&mutex;
  id<MTLLibrary>library=nil;id<MTLDevice>device=create(channel,image,227,&library,&error);CHECK(device&&library&&!error);samplerFactories227(device);
  for(unsigned invalid=1;invalid<=5;++invalid)samplerJob227(device,library,backend,1,0,0,0,invalid);
  for(unsigned n=0;n<2;++n)for(unsigned s=0;s<4;++s)for(unsigned t=0;t<4;++t)for(unsigned f=0;f<2;++f){
   if(!n&&((s!=0&&s!=3)||(t!=0&&t!=3)))continue;samplerJob227(device,library,backend,n,s,t,f,0);
  }
  CHECK(backend.jobs==40);close(device);[library release];[device release];[channel release];core.stop(backend);CHECK(backend.retirements==1);code=0;
 }catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());}}@catch(NSException*e){std::fprintf(stderr,"%s\n",e.description.UTF8String);}
 std::printf("{\"passed\":%s,\"checks\":%u,\"cpu_model_jobs\":%u,\"actual_metal_objects\":true,\"actual_xpc\":false,\"gpu_jobs\":0,\"texture_arithmetic_emulated\":false}\n",code?"false":"true",checks,backend.jobs);return code;
}}
