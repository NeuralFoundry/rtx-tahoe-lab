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
static void submit(id<MTLDevice>d,id<MTLLibrary>library,bool alias,bool useHeap,bool inlineBytes,bool purge,Backend209&backend,const char*functionName="coordinates164"){
 NSError*error=nil;id<MTLFunction>f=[library newFunctionWithName:@(functionName)];CHECK(f);id<MTLComputePipelineState>p=[d newComputePipelineStateWithFunction:f error:&error];CHECK(p&&!error);
 CHECK(p.requiredThreadsPerThreadgroup.width==64&&p.requiredThreadsPerThreadgroup.height==1&&p.requiredThreadsPerThreadgroup.depth==1);
 id<MTLHeap>heap=nil;if(useHeap){MTLHeapDescriptor*desc=[MTLHeapDescriptor new];desc.size=65536;desc.storageMode=MTLStorageModeShared;desc.hazardTrackingMode=MTLHazardTrackingModeTracked;heap=[d newHeapWithDescriptor:desc];[desc release];CHECK(heap);}
 const NSUInteger size[]={4097,8193,16385},offset[]={4,4092,alias?4UL:12284UL};id<MTLBuffer>b[3]={};
 for(unsigned i=0;i<3;++i){b[i]=alias&&i==2?[b[0]retain]:(heap?[heap newBufferWithLength:size[i]options:MTLResourceStorageModeShared]:[d newBufferWithLength:size[i]options:MTLResourceStorageModeShared]);CHECK(b[i]);if(!(alias&&i==2))fill(b[i],i*13+5);}
 std::vector<uint8_t>before[3],expected[3];for(unsigned i=0;i<3;++i){before[i]=snapshot(b[i]);expected[i]=before[i];}
 id<MTLTexture>outputView=[b[2]newTextureWithDescriptor:descriptor224(MTLPixelFormatRGBA8Unorm,256,4)offset:0 bytesPerRow:1024];CHECK(outputView&&outputView.buffer==b[2]);
 for(size_t j=0;j<expected[2].size();++j)expected[2][j]^=uint8_t(0x5a+j%31);if(alias)expected[0]=expected[2];
 if(purge){CHECK(heap);backend.beforeReply=[heap]{[heap setPurgeableState:MTLPurgeableStateEmpty];[heap setPurgeableState:MTLPurgeableStateNonVolatile];};}
 else backend.beforeReply=[outputView]{uint32_t pixel=0;CHECK(textureThrows224([&]{[outputView replaceRegion:MTLRegionMake2D(0,0,1,1)mipmapLevel:0 withBytes:&pixel bytesPerRow:0];}));};
 id<MTLCommandQueue>queue=[d newCommandQueue];CHECK(queue);id<MTLCommandBuffer>command=[queue commandBuffer];CHECK(command);id<MTLComputeCommandEncoder>encoder=[command computeCommandEncoder];CHECK(encoder);[encoder setComputePipelineState:p];
 for(unsigned i=0;i<3;++i){if(inlineBytes&&i==0){uint32_t values[64];for(unsigned j=0;j<64;++j)values[j]=j*3;[encoder setBytes:values length:sizeof(values)atIndex:0];}else[encoder setBuffer:b[i]offset:offset[i]atIndex:i];}
 [encoder dispatchThreads:MTLSizeMake(64,1,1)threadsPerThreadgroup:MTLSizeMake(64,1,1)];[encoder endEncoding];[command commit];[command waitUntilCompleted];backend.beforeReply={};
 if(purge){CHECK(command.status==MTLCommandBufferStatusError&&command.error);for(auto buffer:b){auto bytes=snapshot(buffer);CHECK(bytes==std::vector<uint8_t>(bytes.size(),0));}}
 else{CHECK(command.status==MTLCommandBufferStatusCompleted&&!command.error);for(unsigned i=0;i<3;++i)CHECK(snapshot(b[i])==expected[i]);}
 std::vector<uint8_t>textureResult(4096);[outputView getBytes:textureResult.data()bytesPerRow:1024 fromRegion:MTLRegionMake2D(0,0,256,4)mipmapLevel:0];auto backingResult=snapshot(b[2]);CHECK(!std::memcmp(textureResult.data(),backingResult.data(),4096));[outputView release];
 for(auto buffer:b)[buffer release];[heap release];[queue release];[p release];[f release];
}
int main(int argc,char**argv){@autoreleasepool {
 if(argc!=3||geteuid()!=501)return 2;Backend209 backend;W::Core core;std::mutex mutex;int code=1;unsigned textureChecks=0;
 @try{try{
  NSData*image=[NSData dataWithContentsOfFile:@(argv[2])];CHECK(image&&image.length==backend.image.size());std::memcpy(backend.image.data(),image.bytes,image.length);CHECK(RTXCatalog187::decode(backend.image.data(),backend.image.size(),backend.catalog)&&(backend.catalog.abi==1||backend.catalog.abi==2));backend.generation=4294974483ULL;
  NSBundle*bundle=[NSBundle bundleWithPath:@(argv[1])];NSError*error=nil;CHECK([bundle loadAndReturnError:&error]&&!error);
  void*handle=dlopen(bundle.executablePath.fileSystemRepresentation,RTLD_NOW|RTLD_LOCAL);CHECK(handle);
  auto create=reinterpret_cast<Create>(dlsym(handle,"RTXCreateOwnedApplication209"));auto close=reinterpret_cast<Close>(dlsym(handle,"RTXCloseOwnedApplication209"));auto info=reinterpret_cast<Info>(dlsym(handle,"RTXCopyOwnedApplicationInfo209"));CHECK(create&&close&&info);
  ModelChannel209*channel=[ModelChannel209 new];channel->core=&core;channel->backend=&backend;channel->mutex=&mutex;
  id<MTLLibrary>library=nil;id<MTLDevice>device=create(channel,image,backend.generation,&library,&error);CHECK(device&&library&&!error&&[device conformsToProtocol:@protocol(MTLDevice)]);
  {const unsigned before=checks;textures224(device);textureChecks=checks-before;CHECK(textureChecks>0);}
  submit(device,library,false,false,false,false,backend,backend.catalog.names[0]);
  submit(device,library,true,false,false,false,backend,backend.catalog.names[0]);
  submit(device,library,false,true,true,false,backend,backend.catalog.names[0]);
  submit(device,library,false,true,false,true,backend,backend.catalog.names[0]);
  close(device);[library release];[device release];[channel release];
  ModelChannel209*next=[ModelChannel209 new];next->core=&core;next->backend=&backend;next->mutex=&mutex;
  library=nil;device=create(next,image,backend.generation,&library,&error);CHECK(device&&library&&!error);NSDictionary*state=info(device);CHECK([state[@"session"]unsignedLongLongValue]==2&&[state[@"native_serial"]unsignedLongLongValue]==4&&[state[@"completed"]unsignedLongLongValue]==0);[state release];
  submit(device,library,false,false,false,false,backend,backend.catalog.names[0]);close(device);[library release];[device release];[next release];core.stop(backend);
  CHECK(backend.claims==1&&backend.jobs==5&&backend.retirements==1);code=0;
 }catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());}}@catch(NSException*e){std::fprintf(stderr,"%s\n",e.description.UTF8String);}
 std::printf("{\"passed\":%s,\"checks\":%u,\"texture_api_checks\":%u,\"cpu_model_jobs\":%u,\"actual_metal_objects\":true,\"actual_xpc\":false,\"gpu_jobs\":0,\"standard_metal_enumeration\":false}\n",code?"false":"true",checks,textureChecks,backend.jobs);return code;
}}
