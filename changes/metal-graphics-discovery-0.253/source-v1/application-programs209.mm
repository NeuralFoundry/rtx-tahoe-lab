#define main prior_application209_main
#include "application-owned-test209.mm"
#undef main
static id<MTLLibrary> library209(id<MTLDevice>d,NSData*image){NSError*e=nil;NSData*copy=[image copy];dispatch_data_t data=dispatch_data_create(copy.bytes,copy.length,dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{[copy release];});id<MTLLibrary>l=[d newLibraryWithData:data error:&e];dispatch_release(data);CHECK(l&&!e);return l;}
int main(int argc,char**argv){@autoreleasepool{
 if(argc!=4||geteuid()!=501)return 2;int code=1;Backend209 backend;W::Core core;std::mutex mutex;
 @try{try{
  backend.generation=4294974483ULL;backend.programPattern=true;backend.evidence=argv[3];CHECK(std::filesystem::create_directory(backend.evidence));
  NSData*images[3];RTXCatalog187::Catalog catalogs[3];unsigned index=0;
  for(const char*name:{"first","changed","coordinates"}){auto path=std::filesystem::path(argv[2])/(std::string(name)+".rtxlib");images[index]=[NSData dataWithContentsOfFile:@(path.c_str())];CHECK(images[index]&&RTXCatalog187::decode(static_cast<const uint8_t*>(images[index].bytes),images[index].length,catalogs[index]));auto*b=static_cast<const uint8_t*>(images[index].bytes);backend.approved.emplace_back(b,b+images[index].length);++index;}
  CHECK(catalogs[0].abi==1&&catalogs[1].abi==1&&catalogs[2].abi==2);CHECK(!std::strcmp(catalogs[0].names[0],catalogs[1].names[0])&&![images[0]isEqualToData:images[1]]);
  std::memcpy(backend.image.data(),images[0].bytes,images[0].length);backend.catalog=catalogs[0];
  NSBundle*bundle=[NSBundle bundleWithPath:@(argv[1])];NSError*error=nil;CHECK([bundle loadAndReturnError:&error]&&!error);void*handle=dlopen(bundle.executablePath.fileSystemRepresentation,RTLD_NOW|RTLD_LOCAL);CHECK(handle);
  auto create=reinterpret_cast<Create>(dlsym(handle,"RTXCreateOwnedApplication209"));auto close=reinterpret_cast<Close>(dlsym(handle,"RTXCloseOwnedApplication209"));auto info=reinterpret_cast<Info>(dlsym(handle,"RTXCopyOwnedApplicationInfo209"));CHECK(create&&close&&info);
  ModelChannel209*channel=[ModelChannel209 new];channel->core=&core;channel->backend=&backend;channel->mutex=&mutex;id<MTLLibrary>initial=nil;
  id<MTLDevice>d=create(channel,images[0],backend.generation,&initial,&error);CHECK(d&&initial&&!error&&[d conformsToProtocol:@protocol(MTLDevice)]);
  id<MTLComputePipelineState>pipelines[3];
  for(unsigned i=0;i<3;++i){id<MTLLibrary>l=i?library209(d,images[i]):[initial retain];id<MTLFunction>f=[l newFunctionWithName:@(catalogs[i].names[0])];CHECK(f);pipelines[i]=[d newComputePipelineStateWithFunction:f error:&error];CHECK(pipelines[i]&&!error&&pipelines[i].device==d);[f release];[l release];}
  [initial release]; // Pipelines must own their distinct program libraries.
  CHECK(pipelines[0]!=pipelines[1]);
  const NSUInteger sizes[]={4097,8193,16385},offsets[]={4,4092,12284};id<MTLBuffer>buffers[3];std::vector<uint8_t>expected[3];
  for(unsigned i=0;i<3;++i){buffers[i]=[d newBufferWithLength:sizes[i]options:MTLResourceStorageModeShared];CHECK(buffers[i]);fill(buffers[i],7+i*29);expected[i]=snapshot(buffers[i]);write209(backend.evidence/("buffer-before-"+std::to_string(i)+".bin"),expected[i].data(),expected[i].size());}
  id<MTLCommandQueue>queue=[d newCommandQueue];CHECK(queue);const unsigned order[]={0,1,0,2,1,0,1};
  auto submit=[&](unsigned start,unsigned count,bool invalid){id<MTLCommandBuffer>command=[queue commandBuffer];CHECK(command);
   for(unsigned n=0;n<count;++n){unsigned k=order[start+n];auto local=MTLSizeMake(catalogs[k].library.programs[0].localX,catalogs[k].library.programs[0].localY,catalogs[k].library.programs[0].localZ);id<MTLComputeCommandEncoder>encoder=[command computeCommandEncoder];CHECK(encoder);[encoder setComputePipelineState:pipelines[k]];for(unsigned i=0;i<3;++i)[encoder setBuffer:buffers[i]offset:offsets[i]atIndex:i];[encoder dispatchThreads:MTLSizeMake(invalid?128:64,1,1)threadsPerThreadgroup:local];[encoder endEncoding];}
   [command commit];[command waitUntilCompleted];if(invalid){CHECK(command.status==MTLCommandBufferStatusError&&command.error);return;}
   if(command.error)std::fprintf(stderr,"command error: %s\n",command.error.description.UTF8String);CHECK(command.status==MTLCommandBufferStatusCompleted&&!command.error);
   for(unsigned n=0;n<count;++n){auto&v=backend.approved[order[start+n]];uint8_t salt=W::programHash(v.data()+640,4608)[0];for(size_t j=0;j<expected[2].size();++j)expected[2][j]^=uint8_t(salt+j%31);}
   for(unsigned i=0;i<3;++i){auto actual=snapshot(buffers[i]);CHECK(actual==expected[i]);write209(backend.evidence/("buffer-after-"+std::to_string(start+count)+"-"+std::to_string(i)+".bin"),actual.data(),actual.size());}
  };
  submit(0,2,false);submit(2,2,false);submit(4,2,false);CHECK(backend.jobs==6);
  unsigned frames=backend.frames;submit(6,1,true);CHECK(backend.jobs==6&&backend.frames==frames);for(unsigned i=0;i<3;++i)CHECK(snapshot(buffers[i])==expected[i]);
  submit(6,1,false);CHECK(backend.jobs==7&&backend.admissions==8);NSDictionary*state=info(d);CHECK([state[@"completed"]unsignedLongLongValue]==7&&[state[@"native_serial"]unsignedLongLongValue]==7);[state release];
  close(d);for(auto p:pipelines)[p release];for(auto b:buffers)[b release];[queue release];[d release];[channel release];core.stop(backend);CHECK(backend.claims==1&&backend.retirements==1&&backend.frames==9);
  NSDictionary*report=@{@"passed":@YES,@"checks":@(checks),@"cpu_model_jobs":@(backend.jobs),@"frames":@(backend.frames),@"admissions":@(backend.admissions),@"order":@[@0,@1,@0,@2,@1,@0,@1],@"command_ends":@[@2,@4,@6,@7],@"same_entry_different_programs":@YES,@"pipeline_libraries_released_before_dispatch":@YES,@"invalid_geometry_exchange_count":@0,@"actual_metal_objects":@YES,@"actual_xpc":@NO,@"gpu_jobs":@0,@"standard_metal_enumeration":@NO};NSData*raw=[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:&error];CHECK(raw&&!error);write209(backend.evidence/"result.json",raw.bytes,raw.length);code=0;
 }catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());}}@catch(NSException*e){std::fprintf(stderr,"%s\n",e.description.UTF8String);}
 std::printf("{\"passed\":%s,\"checks\":%u,\"cpu_model_jobs\":%u,\"actual_metal_objects\":true,\"actual_xpc\":false,\"gpu_jobs\":0}\n",code?"false":"true",checks,backend.jobs);return code;
}}
