extern "C" uint32_t rtx_native_graphics_begin243(){
 if(getpid()!=process||geteuid()!=0)return RTX_NATIVE_PROCESS;
 std::lock_guard<std::mutex> lock(ownerMutex);auto*s=connection(ownerDevice);
 if(!s||ownerPhase!=RTXNativeArmed||brokerClaimed||ownedClaimed185||ownedStandard187||graphicsClaimed243)return RTX_NATIVE_STATE;
 // Atomic command-layer claim rejects queued/running legacy work before this
 // path takes the connection lock. It remains claimed through failure/close.
 if(!RTXClaimIdleCommandDeviceForBroker(ownerDevice))return RTX_NATIVE_STATE;
 graphicsClaimed243=true;std::lock_guard<std::mutex> stateLock(s->mutex);bool passed=false;
 try{@autoreleasepool {@try {
  if(!s->closed&&s->armed&&s->session->ready()&&!s->session->completed()&&s->dataSession->ready()&&!s->dispatchSession->active()){
   NSString*folder=[s->directory stringByAppendingPathComponent:@"graphics-begin243"];
   if(newDirectory(folder)){FileSink sink{folder};NativeIO io{s};s->firstError=0;
    passed=s->graphicsSession->begin(io,sink,*s->rootEvidence,*s->dataSession,*s->session);
    const bool saved=saveSession(sink,s,passed);passed=passed&&saved;
   }
  }
 }@catch(NSException*exception){(void)exception;}}}catch(...){ }
 if(!passed){s->graphicsSession->invalidate();s->dataSession->invalidate();s->session->retire();}
 return passed?0:RTX_NATIVE_EVIDENCE;
}
extern "C" uint32_t rtx_native_graphics_draw243(void*image,size_t bytes,uint64_t*completion){
 if(getpid()!=process||geteuid()!=0)return RTX_NATIVE_PROCESS;
 if(!completion)return RTX_NATIVE_ARGUMENT;
 if(!image||bytes!=RTXNativeGraphics243::ColorBytes||!P::separate(image,bytes,completion,sizeof(*completion)))return RTX_NATIVE_ARGUMENT;
 *completion=0;std::lock_guard<std::mutex> lock(ownerMutex);auto*s=connection(ownerDevice);
 if(!s||ownerPhase!=RTXNativeArmed||!graphicsClaimed243||brokerClaimed||ownedClaimed185||ownedStandard187)return RTX_NATIVE_STATE;
 std::lock_guard<std::mutex> stateLock(s->mutex);
 if(s->closed||!s->armed||!s->session->ready()||!s->dataSession->ready()||!s->graphicsSession->ready()||s->dispatchSession->active())return RTX_NATIVE_STATE;
 bool passed=false;uint64_t verified=0;RTXNativeGraphics243::Bytes result;
 try{@autoreleasepool {@try {
  NSString*folder=[s->directory stringByAppendingPathComponent:[NSString stringWithFormat:@"graphics-draw243-%llu",s->graphicsSession->completed()+1]];
  if(newDirectory(folder)){OwnedFileTree187 sinks{folder};NativeIO io{s};s->firstError=0;
   passed=s->graphicsSession->draw(io,sinks,*s->dataSession,result,verified);
   FileSink summary{folder};const bool saved=saveSession(summary,s,passed);passed=passed&&saved&&result.size()==bytes&&verified!=0;
  }
 }@catch(NSException*exception){(void)exception;}}}catch(...){ }
 if(!passed){s->graphicsSession->invalidate();s->dataSession->invalidate();s->session->retire();return RTX_NATIVE_EVIDENCE;}
 std::memcpy(image,result.data(),bytes);*completion=verified;return 0;
}
