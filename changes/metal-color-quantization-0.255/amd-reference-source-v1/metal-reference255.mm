#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <unistd.h>
#include <sys/sysctl.h>
#include "cases255.hpp"
static void need(bool ok,NSString *why){if(!ok){fprintf(stderr,"%s\n",why.UTF8String);exit(2);}}
int main(int argc,const char **argv){@autoreleasepool{
 need(argc==3,@"Output directory and expected boot UUID required");NSString *root=@(argv[1]);
 need(getuid()==501 && geteuid()==501,@"Reference must run as the Mac desktop user");
 char boot[128]={};size_t bootBytes=sizeof(boot);
 need(sysctlbyname("kern.bootsessionuuid",boot,&bootBytes,nullptr,0)==0,@"Boot UUID query");
 need(strcmp(boot,argv[2])==0,@"Boot changed before reference execution");
 NSArray<id<MTLDevice>> *devices=MTLCopyAllDevices();id<MTLDevice> device=nil;
 NSMutableArray *names=[NSMutableArray array];
 for(id<MTLDevice> candidate in devices){[names addObject:@{ @"name":candidate.name,@"registry_id":@(candidate.registryID)}];if([candidate.name containsString:@"AMD"]){need(!device,@"Multiple AMD devices need explicit selection");device=candidate;}}
 need(device!=nil,@"Real AMD Metal device required");need(![device.name containsString:@"RTX"],@"Reference must not use RTX");
 NSError *error=nil;
 NSString *vs=[NSString stringWithContentsOfFile:[root stringByAppendingPathComponent:@"vertex.metal"] encoding:NSUTF8StringEncoding error:&error];need(vs!=nil,error.description);
 NSString *fs=[NSString stringWithContentsOfFile:[root stringByAppendingPathComponent:@"fragment.metal"] encoding:NSUTF8StringEncoding error:&error];need(fs!=nil,error.description);
 id<MTLCommandQueue> queue=[device newCommandQueue];need(queue!=nil,@"AMD command queue");
 // Use the same NDC positions as color255. The actual AMD246 capture
 // proves its reflected input has opposite coverage to this RTX experiment.
 // Only uniform UV values vary; bit patterns match the actual RTX capture.
 float vertices[]={-.75f,-.75f,0,0, .75f,-.75f,0,0, 0,.75f,0,0};
 NSMutableArray *runs=[NSMutableArray array];
 for(unsigned caseIndex=0;caseIndex<16;++caseIndex){@autoreleasepool{
 const auto &item=cases255[caseIndex];
 for(unsigned v=0;v<3;++v){memcpy(&vertices[v*4+2],&item.red,4);memcpy(&vertices[v*4+3],&item.green,4);}
 id<MTLBuffer> vertex=[device newBufferWithBytes:vertices length:sizeof(vertices) options:MTLResourceStorageModeShared];need(vertex!=nil,@"Vertex buffer");
 MTLCompileOptions *options=[MTLCompileOptions new];options.fastMathEnabled=NO;
 id<MTLLibrary> library=[device newLibraryWithSource:[vs stringByAppendingString:fs] options:options error:&error];need(library!=nil,error.description);
 for(unsigned format=0;format<2;++format){@autoreleasepool{
 const MTLPixelFormat pixelFormat=format?MTLPixelFormatRGBA32Float:MTLPixelFormatRGBA8Unorm;
 const NSUInteger pixelBytes=format?16:4,pitch=64*pixelBytes;
 MTLRenderPipelineDescriptor *pd=[MTLRenderPipelineDescriptor new];pd.vertexFunction=[library newFunctionWithName:@"vertex_varying_triangle"];pd.fragmentFunction=[library newFunctionWithName:@"fragment_varying_color"];
 pd.colorAttachments[0].pixelFormat=pixelFormat;pd.colorAttachments[0].blendingEnabled=NO;pd.rasterSampleCount=1;
 MTLVertexDescriptor *vd=[MTLVertexDescriptor new];vd.attributes[0].format=MTLVertexFormatFloat2;vd.attributes[0].offset=0;vd.attributes[0].bufferIndex=0;vd.attributes[1].format=MTLVertexFormatFloat2;vd.attributes[1].offset=8;vd.attributes[1].bufferIndex=0;vd.layouts[0].stride=16;vd.layouts[0].stepFunction=MTLVertexStepFunctionPerVertex;pd.vertexDescriptor=vd;
 id<MTLRenderPipelineState> pipeline=[device newRenderPipelineStateWithDescriptor:pd error:&error];need(pipeline!=nil,error.description);
 MTLTextureDescriptor *td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixelFormat width:64 height:64 mipmapped:NO];td.storageMode=MTLStorageModeManaged;td.usage=MTLTextureUsageRenderTarget;
 id<MTLTexture> texture=[device newTextureWithDescriptor:td];need(texture!=nil,@"Managed reference texture");
 std::vector<unsigned char> data(pitch*64);
 if(format){std::vector<float> initial(64*64*4,-2.0f);memcpy(data.data(),initial.data(),data.size());}
 else for(NSUInteger y=0;y<64;++y)for(NSUInteger x=0;x<256;++x)data[y*pitch+x]=(unsigned char)(((256+y*384+x)*29+7)&255);
 [texture replaceRegion:MTLRegionMake2D(0,0,64,64) mipmapLevel:0 withBytes:data.data() bytesPerRow:pitch];
 MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];pass.colorAttachments[0].texture=texture;pass.colorAttachments[0].loadAction=MTLLoadActionLoad;pass.colorAttachments[0].storeAction=MTLStoreActionStore;
 id<MTLCommandBuffer> command=[queue commandBuffer];id<MTLRenderCommandEncoder> encoder=[command renderCommandEncoderWithDescriptor:pass];need(encoder!=nil,@"Render encoder");
 [encoder setRenderPipelineState:pipeline];[encoder setViewport:MTLViewport{0,0,64,64,0,1}];[encoder setCullMode:MTLCullModeNone];[encoder setVertexBuffer:vertex offset:0 atIndex:0];[encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[encoder endEncoding];
 id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];[blit synchronizeResource:texture];[blit endEncoding];[command commit];[command waitUntilCompleted];need(command.status==MTLCommandBufferStatusCompleted,command.error.description?:@"Command did not complete");
 [texture getBytes:data.data() bytesPerRow:pitch fromRegion:MTLRegionMake2D(0,0,64,64) mipmapLevel:0];
 NSString *name=[NSString stringWithFormat:@"case-%02u-%@.bin",caseIndex+1,format?@"rgba32float":@"rgba8unorm"];
 need([[NSData dataWithBytes:data.data() length:data.size()] writeToFile:[root stringByAppendingPathComponent:name] options:0 error:&error],error.description);
 [runs addObject:@{@"file":name,@"bytes":@(data.size()),@"status":@(command.status),@"fast_math":@NO,@"serial":@(caseIndex+1),@"case_name":@(item.name),@"uniform_uv_bits":@[@(item.red),@(item.green)],@"pixel_format":@(pixelFormat),@"gpu_seconds":@(command.GPUEndTime-command.GPUStartTime)}];
 }} }}
 NSDictionary *result=@{@"passed":@YES,@"pid":@(getpid()),@"uid":@(getuid()),@"boot_uuid":@(boot),
 @"device_class":NSStringFromClass([(id)device class]),@"device_bundle":[NSBundle bundleForClass:[(id)device class]].bundlePath?:@"",@"reference_device":device.name,@"reference_registry_id":@(device.registryID),@"enumerated_devices":names,@"runs":runs,@"rtx_test":@NO,@"standard_metal_api":@YES,@"vertex_y_reflected_to_match_framebuffer":@NO,@"os":NSProcessInfo.processInfo.operatingSystemVersionString};
 NSData *json=[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:&error];need(json!=nil,error.description);need([json writeToFile:[root stringByAppendingPathComponent:@"reference-result.json"] options:0 error:&error],error.description);puts([[NSString alloc]initWithData:json encoding:NSUTF8StringEncoding].UTF8String);return 0;
}}
