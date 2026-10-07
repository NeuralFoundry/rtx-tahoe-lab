struct OwnedFileTree187 {
 NSString*directory;
 bool save(const char*name,const uint8_t*p,size_t n){FileSink sink{directory};return sink.save(name,p,n);}
 template<class Action>bool part(const std::string&name,Action action){
  NSString*leaf=[NSString stringWithUTF8String:name.c_str()];if(!leaf||[leaf rangeOfString:@"/"].location!=NSNotFound)return false;
  NSString*path=[directory stringByAppendingPathComponent:leaf];if(!newDirectory(path))return false;FileSink sink{path};return action(sink);
 }
};
@interface RTXNativeOwnedTransport187 : NSObject<RTXCommandTransport> {RTXNativeConnection108*_state;}
- (instancetype)initWithState:(RTXNativeConnection108*)state;
@end
@implementation RTXNativeOwnedTransport187
- (instancetype)initWithState:(RTXNativeConnection108*)state{if((self=[super init]))_state=[state retain];return self;}
- (void)dealloc{[_state release];[super dealloc];}
- (BOOL)executeRequest:(NSData*)request libraryPayload:(NSData*)payload result:(NSData**)result completion:(uint64_t*)completion error:(NSError**)error{
 if(!result||!completion||!error)return NO;*result=nil;*completion=0;*error=nil;if(getpid()!=process||geteuid()!=0)return NO;
 std::lock_guard<std::mutex>lock(_state->mutex);bool passed=false;uint64_t verified=0;RTXBatch187::Bytes output;
 try{@try {
  if(ownedStandard187&&!_state->closed&&_state->armed&&_state->dataSession->ready()&&_state->dispatchSession->ready()&&
     [request isKindOfClass:[NSData class]]&&request.length>=RTXBatch187::Header&&request.length<=RTXBatch187::Header+RTXBatch187::MaxPayload&&
     [payload isKindOfClass:[NSData class]]&&payload.length==4608){
   // Deep copy mutable NSData before decoding or retaining request bytes.
   RTXBatch187::Bytes snapshot(request.length);[request getBytes:snapshot.data()length:snapshot.size()];
   std::array<uint8_t,4608>image{};[payload getBytes:image.data()length:image.size()];
   RTXProgram205::Library library;RTXBatch187::Plan plan;
   if(RTXProgram205::decode(image.data(),512,image.data()+512,4096,library)&&
      RTXBatch187::decode(snapshot.data(),snapshot.size(),library.programs,_state->generation,_state->dispatchSession->completed()+1,plan)&&
      RTXProgram205::geometry(library,plan.program,plan.groups,plan.threads)&&admission208.admit(image.data(),image.size(),getpid(),geteuid())){
    NSString*folder=[_state->directory stringByAppendingPathComponent:[NSString stringWithFormat:@"metal-owned187-%llu",_state->dispatchSession->completed()+1]];
    if(newDirectory(folder)){OwnedFileTree187 sinks{folder};NativeIO io{_state};_state->firstError=0;
     bool selected=true;
     if(image!=_state->ownedPayload187){selected=sinks.part("select-program208",[&](auto&sink){return _state->dispatchSession->select(io,sink,image.data(),image.size());});if(selected)_state->ownedPayload187=image;}
     passed=selected&&RTXOwnedCommand208::execute(io,sinks,*_state->dataSession,*_state->dispatchSession,library,snapshot.data(),snapshot.size(),output,verified);
     FileSink summary{folder};const bool saved=saveSession(summary,_state,passed);passed=passed&&saved;
     if(passed){*result=[NSData dataWithBytes:output.data()length:output.size()];passed=*result!=nil;}
    }
   }
  }
 }@catch(NSException*exception){(void)exception;passed=false;}}catch(...){passed=false;}
 if(!passed){_state->dispatchSession->invalidate();_state->dataSession->invalidate();_state->session->retire();*result=nil;
  *error=[NSError errorWithDomain:MTLCommandBufferErrorDomain code:MTLCommandBufferErrorInternal userInfo:@{NSLocalizedDescriptionKey:@"Owned Metal command could not be independently verified; the session is retired"}];return NO;
 }
 *completion=verified;return YES;
}
@end
extern "C" uint32_t rtx_native_program_admission208(const RTXProgramAdmission208::Callbacks*callbacks){
 if(getpid()!=process||geteuid()!=0)return RTX_NATIVE_PROCESS;if(!callbacks)return RTX_NATIVE_ARGUMENT;
 std::lock_guard<std::mutex>lock(ownerMutex);if(ownerPhase!=RTXNativeCold||connectionAttempted.load())return RTX_NATIVE_STATE;
 return admission208.bind(*callbacks,getpid(),geteuid())?0:RTX_NATIVE_ARGUMENT;
}
extern "C" uint32_t rtx_native_arm_owned187(const char*directory,const void*container,size_t bytes){
 if(getpid()!=process||geteuid()!=0)return RTX_NATIVE_PROCESS;
 if(!directory||!container||bytes!=RTXCatalog187::Bytes)return RTX_NATIVE_ARGUMENT;
 std::array<uint8_t,RTXCatalog187::Bytes>copy{};std::memcpy(copy.data(),container,bytes);RTXCatalog187::Catalog catalog;
 if(!RTXCatalog187::decode(copy.data(),copy.size(),catalog))return RTX_NATIVE_ARGUMENT;
 std::lock_guard<std::mutex>lock(ownerMutex);auto*s=connection(ownerDevice);if(!s||ownerPhase!=RTXNativeOpen||graphicsClaimed243||brokerClaimed||ownedClaimed185||ownedStandard187)return RTX_NATIVE_STATE;
 if(!admission208.admit(copy.data()+640,4608,getpid(),geteuid()))return RTX_NATIVE_ARGUMENT;
 @autoreleasepool {id<MTLLibrary>replacement=nil;try{@try {
  NSError*error=nil;replacement=RTXNewCompiledLibrary(ownerDevice,[NSData dataWithBytes:copy.data()length:copy.size()],&error);
  if(!replacement)return RTX_NATIVE_EVIDENCE;
  {
   std::lock_guard<std::mutex>stateLock(s->mutex);
   if(s->armAttempted||s->closed||s->writeFailed){[replacement release];return RTX_NATIVE_STATE;}
   NSString*folder=[NSString stringWithUTF8String:directory];if(!newDirectory(folder)){[replacement release];return RTX_NATIVE_ARGUMENT;}
   s->armAttempted=YES;s->firstError=0;s->directory=[folder copy];NSString*arm=[folder stringByAppendingPathComponent:@"arm"];
   if(!newDirectory(arm)){s->dispatchSession->invalidate();s->dataSession->invalidate();s->session->retire();[replacement release];return RTX_NATIVE_EVIDENCE;}
   FileSink sink{arm};NativeIO io{s};bool passed=s->rootEvidence->collect(io,sink,s->generation)&&s->dataSession->collect(io,sink,*s->rootEvidence)&&s->session->arm(io,sink);
   const bool saved=saveSession(sink,s,passed);passed=passed&&saved;
   NSString*program=[folder stringByAppendingPathComponent:@"owned-program187"];
   if(passed&&newDirectory(program)){FileSink programSink{program};passed=s->dispatchSession->begin(io,programSink,*s->rootEvidence,*s->dataSession,*s->session,copy.data()+640,4608);}
   else passed=false;
   if(!passed){s->dispatchSession->invalidate();s->dataSession->invalidate();s->session->retire();[replacement release];return RTX_NATIVE_EVIDENCE;}
   std::memcpy(s->ownedPayload187.data(),copy.data()+640,4608);
  }
  auto*transport=[[RTXNativeOwnedTransport187 alloc]initWithState:s];BOOL configured=RTXConfigureCommandDevice(ownerDevice,replacement,s->generation,transport,&error);[transport release];
  if(!configured){std::lock_guard<std::mutex>stateLock(s->mutex);s->dispatchSession->invalidate();s->dataSession->invalidate();s->session->retire();[replacement release];return RTX_NATIVE_EVIDENCE;}
  [ownerLibrary release];ownerLibrary=replacement;replacement=nil;ownedStandard187=true;s->armed=YES;ownerPhase=RTXNativeArmed;return 0;
 }@catch(NSException*exception){(void)exception;}}catch(...){ }
 [replacement release];std::lock_guard<std::mutex>stateLock(s->mutex);s->dispatchSession->invalidate();s->dataSession->invalidate();s->session->retire();return RTX_NATIVE_EXCEPTION;}
}
