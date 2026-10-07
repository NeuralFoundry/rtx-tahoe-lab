#include "RTXFloatResult110.hpp"
#import "RTXApplicationClient.h"
#import "RTXBackendCompiler.h"
#import "RTXMetalLibrary.h"
#import <objc/runtime.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include "ResidentBrokerWire.hpp"
extern "C" NSData *RTXCopyNativePipelineContainer056(id<MTLComputePipelineState> pipeline);
static unsigned checks=0;
#define CHECK(...) do{++checks;if(!(__VA_ARGS__)){fprintf(stderr,"runtime100 line %d: %s\n",__LINE__,#__VA_ARGS__);abort();}}while(0)
static void save(NSString *folder,NSString *name,id value){
 NSError *error=nil;NSData *data=[NSJSONSerialization dataWithJSONObject:value options:NSJSONWritingSortedKeys error:&error];
 CHECK(data&&!error&&[data writeToFile:[folder stringByAppendingPathComponent:name]options:NSDataWritingWithoutOverwriting error:&error]&&!error);
}
static NSString *body(unsigned program){CHECK(program<3);return @[@"float4 p=float4(a[tid],b[tid],a[tid]+1.0f,b[tid]-2.0f); float4 q=floor(p.wzyx)+trunc(p); out[tid]=fma(q.x,q.z,q.y*q.w);",@"float2 p=float2(a[tid],b[tid]); uint n=tid&3u;\n#pragma clang loop unroll(disable)\nfor(uint i=0u;i<n;++i){p=ceil(p*float2(0.5f,2.0f))+rint(p.yx);} out[tid]=p.x-p.y;",@"float2 p=float2(a[tid],b[tid]); float2 q=rint(p); out[tid]=q[tid&1u];"][program];}
static NSString *text(NSString *name,unsigned program){return [NSString stringWithFormat:@"#include <metal_stdlib>\nusing namespace metal;\nkernel void %@(device const float *a [[buffer(0)]], device const float *b [[buffer(1)]], device float *out [[buffer(2)]], uint tid [[thread_position_in_grid]]) { %@ }\n",name,body(program)];}

int main(int argc,char **argv){if(argc!=9||geteuid()!=501)return 2;@autoreleasepool{
 NSString *folder=@(argv[8]);NSError *error=nil;NSFileManager *fm=NSFileManager.defaultManager;
 CHECK([fm createDirectoryAtPath:folder withIntermediateDirectories:NO attributes:nil error:&error]&&!error);
 NSString *diagnostics=[folder stringByAppendingPathComponent:@"compiler"];CHECK([fm createDirectoryAtPath:diagnostics withIntermediateDirectories:NO attributes:nil error:&error]&&!error);
 NSData *requests=[NSData dataWithContentsOfFile:@(argv[5])],*expected=[NSData dataWithContentsOfFile:@(argv[6])];CHECK(requests.length==65*2112&&expected.length==65*2048);const uint64_t generation=RtxProgram033::get64(static_cast<const uint8_t *>(requests.bytes)+16);CHECK(generation);
 NSData *initial=[NSData dataWithContentsOfFile:@(argv[1])];CHECK(initial.length==5248);
 id<MTLLibrary> selected=nil;id<MTLDevice> device=RTXCreateApplicationDevice(@(argv[2]),initial,generation,5000,&selected,&error);CHECK(device&&selected&&!error);
 CHECK([NSStringFromClass(object_getClass(device))isEqual:@"RTXMetalApplicationDevice059"]);
 CHECK(RTXConfigureBackendCompiler056(device,@(argv[3]),@(argv[4]),diagnostics,&error)&&!error);
 const bool cpu=[@(argv[7])isEqual:@"cpu-fixture"];CHECK(cpu||[@(argv[7])isEqual:@"gpu-requested"]);unsigned nanDifferences=0;
 NSMutableArray *programs=[NSMutableArray array],*records=[NSMutableArray array];NSMutableData *results=[NSMutableData data];unsigned serial=0,unchangedOutput=0,changedOutput=0;
 NSString *name=[NSString stringWithFormat:@"runtime_%d_rounding",getpid()];
 id<MTLCommandQueue> queue=[device newCommandQueue];CHECK(queue);
 MTLCompileOptions *options=[MTLCompileOptions new];options.languageVersion=MTLLanguageVersion2_4;options.fastMathEnabled=NO;
 for(unsigned program=0;program<3;++program){@autoreleasepool{
  if(program==2){
   NSString *badName=[name stringByAppendingString:@"_remainder"];
   NSString *badText=[text(badName,0)stringByReplacingOccurrencesOfString:@"fma(q.x,q.z,q.y*q.w)" withString:@"fmod(q.x,q.y)"];
   id<MTLLibrary> library=[device newLibraryWithSource:badText options:options error:&error];CHECK(library&&!error);
   id<MTLFunction> function=[library newFunctionWithName:badName];CHECK(function);
   id<MTLComputePipelineState> bad=[device newComputePipelineStateWithFunction:function error:&error];CHECK(!bad&&error&&[error.domain isEqual:@"RTXBackendCompiler056"]&&error.code==4);
   save(folder,@"unsupported.json",@{@"source":badText,@"error_domain":error.domain,@"error_code":@(error.code),@"message":error.localizedDescription,@"completed_before_rejection":@(serial)});
   [function release];[library release];error=nil;
  }
  NSString *source=text(name,program);
  NSDictionary *before=RTXCopyApplicationDeviceInfo(device);CHECK([before[@"completed"]unsignedLongLongValue]==serial);
  id<MTLLibrary> library=[device newLibraryWithSource:source options:options error:&error];CHECK(library&&!error);
  id<MTLFunction> function=[library newFunctionWithName:name];CHECK(function&&[NSStringFromClass(object_getClass(function))isEqual:@"_MTLFunctionInternal"]);
  id<MTLComputePipelineState> pipeline=[device newComputePipelineStateWithFunction:function error:&error];CHECK(pipeline&&!error&&pipeline.device==device);
  NSData *container=RTXCopyNativePipelineContainer056(pipeline);CHECK(container&&container.length==5248&&![container isEqual:initial]);
  NSString *filename=[NSString stringWithFormat:@"program-%u.rtxlib",program];CHECK([container writeToFile:[folder stringByAppendingPathComponent:filename]options:NSDataWritingWithoutOverwriting error:&error]&&!error);
  NSDictionary *after=RTXCopyApplicationDeviceInfo(device);CHECK([after[@"completed"]unsignedLongLongValue]==serial);
  [programs addObject:@{@"index":@(program),@"operation":@[@"round_vec4",@"round_loop",@"round_select"][program],@"source":source,@"entry":name,@"file":filename,@"compiled_after_jobs":@(serial),@"before":before,@"after":after,@"function_class":NSStringFromClass(object_getClass(function)),@"pipeline_device_matches":@(pipeline.device==device)}];[before release];[after release];[container release];
  for(unsigned j=0;j<(program==2?21u:22u);++j){@autoreleasepool{
   const auto *wire=static_cast<const uint8_t *>(requests.bytes)+serial*2112,*reference=static_cast<const uint8_t *>(expected.bytes)+serial*2048;CHECK(RtxProgram033::get64(wire+16)==generation&&RtxProgram033::get64(wire+24)==serial+1);
   std::array<std::array<uint8_t,256>,3> input{};id<MTLBuffer> buffers[3]={};
   id<MTLCommandBuffer> command=[queue commandBuffer];id<MTLComputeCommandEncoder> encoder=[command computeCommandEncoder];CHECK(command&&encoder);[encoder setComputePipelineState:pipeline];
   for(unsigned b=0;b<3;++b){std::memcpy(input[b].data(),wire+64+b*256,256);buffers[b]=[device newBufferWithLength:256 options:MTLResourceStorageModeShared];CHECK(buffers[b]&&buffers[b].contents);std::memcpy(buffers[b].contents,input[b].data(),256);[encoder setBuffer:buffers[b]offset:0 atIndex:b];}
   [encoder dispatchThreadgroups:MTLSizeMake(1,1,1)threadsPerThreadgroup:MTLSizeMake(64,1,1)];[encoder endEncoding];[command commit];[command waitUntilCompleted];CHECK(command.status==MTLCommandBufferStatusCompleted&&!command.error);
   std::array<uint8_t,2048> output{};
   for(unsigned b=0;b<3;++b){const auto *data=static_cast<const uint8_t *>(buffers[b].contents);for(unsigned k=0;k<256;k+=4){const uint32_t got=RtxProgram033::get32(data+k),want=RtxProgram033::get32(reference+b*256+k);CHECK(RTXFloatResult110::matches(got,want,b==2));if(b==2&&got!=want)++nanDifferences;if(b==2){if(got==RtxProgram033::get32(input[b].data()+k))++unchangedOutput;else ++changedOutput;}}if(b!=2)CHECK(!std::memcmp(data,input[b].data(),256));std::memcpy(output.data()+b*256,data,256);[buffers[b]release];}
   [results appendBytes:output.data()length:output.size()];++serial;
   NSDictionary *state=RTXCopyApplicationDeviceInfo(device);CHECK([state[@"completed"]unsignedLongLongValue]==serial&&[state[@"native_serial"]unsignedLongLongValue]==serial&&[state[@"resident_epoch"]unsignedLongLongValue]==program+2);
   [records addObject:@{@"job":@(serial),@"program":@(program),@"state":state}];[state release];
  }}
  [pipeline release];[function release];[library release];
 }}
 CHECK(serial==65);CHECK(unchangedOutput==0&&changedOutput==4160);CHECK([results writeToFile:[folder stringByAppendingPathComponent:@"readbacks.bin"]options:NSDataWritingWithoutOverwriting error:&error]&&!error);
 RTXCloseApplicationDevice(device);[queue release];[selected release];[options release];[device release];
 NSDictionary *compiler=RTXCopyBackendCompilerInfo056();CHECK([compiler[@"requests"]unsignedIntValue]==4&&[compiler[@"successes"]unsignedIntValue]==3);
 save(folder,@"result.json",@{@"passed":@YES,@"uid":@(geteuid()),@"pid":@(getpid()),@"checks":@(checks),@"programs":programs,@"records":records,@"compiler":compiler,@"jobs":@65,@"unchanged_output_words":@(unchangedOutput),@"changed_output_words":@(changedOutput),@"new_compiled_programs":@3,@"unsupported_rejections":@1,@"cpu_fixture_only":@(cpu),@"gpu_backend_requested":@(!cpu),@"shader_arithmetic_evaluated_by_client":@NO,@"quiet_nan_payload_differences":@(nanDifferences),@"system_metal_registered":@NO});[compiler release];
 printf("{\"passed\":true,\"checks\":%u,\"jobs\":65,\"compiled\":3,\"cpu_fixture_only\":%s}\n",checks,cpu?"true":"false");
 }return 0;}
