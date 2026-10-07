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
int main(int argc,char**argv){@autoreleasepool{
 if(argc!=4||geteuid()!=501)return 2;Backend209 backend;W::Core core;std::mutex mutex;unsigned rejections=0,callbacks=0;int code=1;
 @try{try{
  NSData*initial=[NSData dataWithContentsOfFile:@(argv[2])];CHECK(initial.length==backend.image.size());std::memcpy(backend.image.data(),initial.bytes,initial.length);CHECK(RTXCatalog187::decode(backend.image.data(),backend.image.size(),backend.catalog));backend.generation=247;
  NSBundle*bundle=[NSBundle bundleWithPath:@(argv[1])];NSError*error=nil;CHECK([bundle loadAndReturnError:&error]&&!error);
  void*handle=dlopen(bundle.executablePath.fileSystemRepresentation,RTLD_NOW|RTLD_LOCAL);CHECK(handle);
  auto create=reinterpret_cast<Create>(dlsym(handle,"RTXCreateOwnedApplication209"));auto close=reinterpret_cast<Close>(dlsym(handle,"RTXCloseOwnedApplication209"));auto payload=reinterpret_cast<NSData*(*)(id<MTLRenderPipelineState>)>(dlsym(handle,"RTXCopyRenderPipelineContainer247"));CHECK(create&&close&&payload);
  ModelChannel209*channel=[ModelChannel209 new];channel->core=&core;channel->backend=&backend;channel->mutex=&mutex;id<MTLLibrary>compute=nil;id<MTLDevice>device=create(channel,initial,247,&compute,&error);CHECK(device&&compute&&!error);
  const unsigned initialClaims=backend.claims,initialAdmissions=backend.admissions;CHECK(backend.jobs==0);
  NSData*catalog=[NSData dataWithContentsOfFile:@(argv[3])];CHECK(catalog.length==4160);NSMutableData*mutableCatalog=[catalog mutableCopy];id<MTLLibrary>graphics=loadGraphics247(device,mutableCatalog,&error);CHECK(graphics&&!error&&graphics.device==device&&graphics.functionNames.count==2);
  static_cast<uint8_t*>(mutableCatalog.mutableBytes)[0]^=1;[mutableCatalog release];
  id<MTLFunction>vertex=[graphics newFunctionWithName:@"vertex_varying_triangle"],fragment=[graphics newFunctionWithName:@"fragment_varying_color"];CHECK(vertex.functionType==MTLFunctionTypeVertex&&fragment.functionType==MTLFunctionTypeFragment);CHECK(![graphics newFunctionWithName:@"missing"]);
  CHECK(![device newComputePipelineStateWithFunction:vertex error:&error]&&error);++rejections;error=nil;
  for(unsigned offset:{0u,7u,8u,12u,16u,48u,63u,64u,192u,320u,4095u,4159u}){NSMutableData*bad=[catalog mutableCopy];static_cast<uint8_t*>(bad.mutableBytes)[offset]^=1;CHECK(!loadGraphics247(device,bad,&error)&&error);error=nil;[bad release];++rejections;}
  for(unsigned length:{0u,4159u,4161u}){NSMutableData*bad=[NSMutableData dataWithLength:length];CHECK(!loadGraphics247(device,bad,&error)&&error);error=nil;++rejections;}
  auto*d=descriptor247(graphics);id<MTLRenderPipelineState>pipeline=[device newRenderPipelineStateWithDescriptor:d error:&error];CHECK(pipeline&&!error&&[pipeline conformsToProtocol:@protocol(MTLRenderPipelineState)]&&pipeline.device==device&&pipeline.allocatedSize==0&&!pipeline.supportIndirectCommandBuffers&&pipeline.gpuResourceID._impl==0);
  NSData*copy=payload(pipeline);CHECK([copy isEqualToData:catalog]);[copy release];d.label=@"changed";d.vertexFunction=nil;d.vertexDescriptor.layouts[0].stride=32;d.colorAttachments[0].pixelFormat=MTLPixelFormatBGRA8Unorm;CHECK([pipeline.label isEqualToString:@"retained pipeline247"]);copy=payload(pipeline);CHECK([copy isEqualToData:catalog]);[copy release];
  for(unsigned fault=0;fault<23;++fault){auto*bad=descriptor247(graphics);switch(fault){
   case 0:bad.vertexFunction=nil;break;case 1:bad.fragmentFunction=nil;break;case 2:bad.vertexFunction=fragment;break;case 3:bad.fragmentFunction=vertex;break;
   case 4:bad.vertexFunction=[[compute newFunctionWithName:compute.functionNames[0]]autorelease];break;
   case 5:bad.vertexDescriptor=nil;break;case 6:bad.vertexDescriptor.attributes[1].offset=4;break;case 7:bad.vertexDescriptor.attributes[0].format=MTLVertexFormatFloat4;break;
   case 8:bad.vertexDescriptor.layouts[0].stride=32;break;case 9:bad.vertexDescriptor.layouts[0].stepFunction=MTLVertexStepFunctionPerInstance;break;case 10:bad.vertexDescriptor.attributes[2].format=MTLVertexFormatFloat;break;
   case 11:bad.colorAttachments[0].blendingEnabled=YES;break;case 12:bad.colorAttachments[0].writeMask=MTLColorWriteMaskRed;break;case 13:bad.colorAttachments[0].pixelFormat=MTLPixelFormatBGRA8Unorm;break;case 14:bad.colorAttachments[1].pixelFormat=MTLPixelFormatRGBA8Unorm;break;
   case 15:bad.depthAttachmentPixelFormat=MTLPixelFormatDepth32Float;break;case 16:bad.rasterSampleCount=4;break;case 17:bad.alphaToCoverageEnabled=YES;break;case 18:bad.rasterizationEnabled=NO;break;case 19:bad.maxVertexAmplificationCount=2;break;case 20:bad.supportIndirectCommandBuffers=YES;break;case 21:bad.inputPrimitiveTopology=MTLPrimitiveTopologyClassLine;break;case 22:bad.supportAddingVertexBinaryFunctions=YES;break;
  }CHECK(![device newRenderPipelineStateWithDescriptor:bad error:&error]&&error);error=nil;++rejections;}
  auto*good=descriptor247(graphics);MTLRenderPipelineReflection*reflection=(MTLRenderPipelineReflection*)graphics;
  id<MTLRenderPipelineState>options=[device newRenderPipelineStateWithDescriptor:good options:MTLPipelineOptionNone reflection:&reflection error:&error];CHECK(options&&!reflection&&!error);[options release];
  reflection=(MTLRenderPipelineReflection*)graphics;CHECK(![device newRenderPipelineStateWithDescriptor:good options:MTLPipelineOptionArgumentInfo reflection:&reflection error:&error]&&error&&!reflection);error=nil;++rejections;
  dispatch_semaphore_t done=dispatch_semaphore_create(0);__block BOOL accepted=NO;
  [device newRenderPipelineStateWithDescriptor:good completionHandler:^(id<MTLRenderPipelineState>p,NSError*e){accepted=p&&p.device==device&&!e&&[p.label isEqualToString:@"retained pipeline247"];dispatch_semaphore_signal(done);}];good.label=@"mutated after asynchronous call";good.fragmentFunction=nil;CHECK(dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC))==0&&accepted);++callbacks;
  [device newRenderPipelineStateWithDescriptor:descriptor247(graphics) options:MTLPipelineOptionArgumentInfo completionHandler:^(id<MTLRenderPipelineState>p,MTLRenderPipelineReflection*r,NSError*e){accepted=!p&&!r&&e;dispatch_semaphore_signal(done);}];CHECK(dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC))==0&&accepted);++callbacks;dispatch_release(done);
  CHECK(backend.jobs==0&&backend.claims==initialClaims&&backend.admissions==initialAdmissions);[pipeline release];[vertex release];[fragment release];[graphics release];close(device);[compute release];[device release];[channel release];core.stop(backend);CHECK(backend.retirements==1);code=0;
 }catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());}}@catch(NSException*e){std::fprintf(stderr,"%s\n",e.description.UTF8String);}
 std::printf("{\"passed\":%s,\"checks\":%u,\"rejections\":%u,\"async_callbacks\":%u,\"real_application_factory\":true,\"standard_render_pipeline_api\":true,\"gpu_jobs\":0,\"actual_xpc\":false,\"draw_encoder_implemented\":false}\n",code?"false":"true",checks,rejections,callbacks);return code;
}}
