extern "C" uint32_t rtx_native_owned_begin185(const void*payload,size_t bytes){
 if(getpid()!=process||geteuid()!=0)return RTX_NATIVE_PROCESS;
 if(!payload||bytes!=4608)return RTX_NATIVE_ARGUMENT;
 std::array<uint8_t,4608>snapshot{};std::memcpy(snapshot.data(),payload,bytes);RtxProgram164::Library checked{};
 if(!RtxProgram164::decode(snapshot.data(),512,snapshot.data()+512,4096,checked))return RTX_NATIVE_ARGUMENT;
 std::lock_guard<std::mutex> lock(ownerMutex);auto*s=connection(ownerDevice);
 if(!s||ownerPhase!=RTXNativeArmed||graphicsClaimed243||brokerClaimed||ownedClaimed185||ownedStandard187)return RTX_NATIVE_STATE;
 // This atomic command-layer gate proves that no legacy worker can race the
 // transition. A normal close would not provide that guarantee.
 if(!RTXClaimIdleCommandDeviceForBroker(ownerDevice))return RTX_NATIVE_STATE;
 ownedClaimed185=true;std::lock_guard<std::mutex> stateLock(s->mutex);
 if(s->closed||!s->armed||!s->session->ready()||s->session->completed()||!s->dataSession->ready())return RTX_NATIVE_STATE;
 bool passed=false;
 try{@autoreleasepool {@try {
  NSString*folder=[s->directory stringByAppendingPathComponent:@"owned-dispatch-begin185"];
  if(newDirectory(folder)){FileSink sink{folder};NativeIO io{s};s->firstError=0;
   passed=s->dispatchSession->begin(io,sink,*s->rootEvidence,*s->dataSession,*s->session,snapshot.data(),snapshot.size());
   const bool saved=saveSession(sink,s,passed);passed=passed&&saved;
  }
 }@catch(NSException*exception){(void)exception;}}}catch(...){ }
 if(!passed){s->dispatchSession->invalidate();s->dataSession->invalidate();s->session->retire();}
 return passed?0:RTX_NATIVE_EVIDENCE;
}
extern "C" uint32_t rtx_native_owned_submit185(uint32_t program,const RTXOwnedBinding185*bindings,size_t count,RTXOwnedSize185 groups,RTXOwnedSize185 threads,uint64_t*completion){
 if(getpid()!=process||geteuid()!=0)return RTX_NATIVE_PROCESS;
 if(!completion)return RTX_NATIVE_ARGUMENT;*completion=0;if(!bindings||!count||count>8)return RTX_NATIVE_ARGUMENT;
 std::array<RTXNativeDispatch205::Binding,8>snapshot{};for(size_t i=0;i<count;++i)snapshot[i]={bindings[i].slot,bindings[i].index,bindings[i].offset,bindings[i].bytes};
 std::lock_guard<std::mutex> lock(ownerMutex);auto*s=connection(ownerDevice);
 if(!s||ownerPhase!=RTXNativeArmed||!ownedClaimed185||graphicsClaimed243||brokerClaimed)return RTX_NATIVE_STATE;
 std::lock_guard<std::mutex> stateLock(s->mutex);
 if(s->closed||!s->armed||!s->session->ready()||!s->dataSession->ready()||!s->dispatchSession->ready())return RTX_NATIVE_STATE;
 bool passed=false;uint64_t verified=0;
 try{@autoreleasepool {@try {
  NSString*folder=[s->directory stringByAppendingPathComponent:[NSString stringWithFormat:@"owned-job185-%llu",s->dispatchSession->completed()+1]];
  if(newDirectory(folder)){FileSink sink{folder};NativeIO io{s};s->firstError=0;
   passed=s->dispatchSession->submit(io,sink,program,snapshot.data(),count,{groups.x,groups.y,groups.z},{threads.x,threads.y,threads.z},verified);
   const bool saved=saveSession(sink,s,passed);passed=passed&&saved;
  }
 }@catch(NSException*exception){(void)exception;}}}catch(...){ }
 if(!passed){s->dispatchSession->invalidate();s->dataSession->invalidate();s->session->retire();return RTX_NATIVE_EVIDENCE;}
 *completion=verified;return 0;
}
