extern "C" BOOL RTXClaimNativeOwnedBroker188(id<RTXCommandTransport>*transport,NSData**container,uint64_t*generation,uint64_t*completed){
 if(!transport||!container||!generation||!completed)return NO;*transport=nil;*container=nil;*generation=*completed=0;
 if(getpid()!=process||geteuid()!=0)return NO;
 std::lock_guard<std::mutex>lock(ownerMutex);auto*s=connection(ownerDevice);
 if(!s||ownerPhase!=RTXNativeArmed||!ownedStandard187||graphicsClaimed243||brokerClaimed||ownedClaimed185)return NO;
 // An atomic empty-context transfer forbids a pending local worker from
 // competing with the single serialized process broker.
 if(!RTXClaimIdleCommandDeviceForBroker(ownerDevice))return NO;
 std::lock_guard<std::mutex>stateLock(s->mutex);if(s->closed||!s->armed||!s->dataSession->ready()||!s->dispatchSession->ready()||s->dispatchSession->completed())return NO;
 NSData*image=nil;RTXNativeOwnedTransport187*target=nil;
 try{@try{
  image=RTXCopyLibraryContainer(ownerLibrary);if(image.length==RTXCatalog187::Bytes){target=[[RTXNativeOwnedTransport187 alloc]initWithState:s];
   if(target){brokerClaimed=true;*transport=target;*container=image;*generation=s->generation;*completed=0;return YES;}
  }
 }@catch(NSException*exception){(void)exception;}}catch(...){ }
 [target release];[image release];s->dispatchSession->invalidate();s->dataSession->invalidate();s->session->retire();return NO;
}
extern "C" void RTXRetireNativeOwnedBroker188(){
 if(getpid()!=process||geteuid()!=0)return;std::lock_guard<std::mutex>lock(ownerMutex);auto*s=connection(ownerDevice);if(!s||!ownedStandard187)return;
 std::lock_guard<std::mutex>stateLock(s->mutex);s->dispatchSession->invalidate();s->dataSession->invalidate();s->session->retire();
}
