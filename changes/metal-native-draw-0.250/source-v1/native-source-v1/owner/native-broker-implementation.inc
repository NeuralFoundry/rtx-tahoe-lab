extern "C" BOOL RTXClaimNativeBroker(id<RTXCommandTransport> *transport,NSData **container,uint64_t *generation,uint64_t *completed){
 if(!transport||!container||!generation||!completed)return NO;
 *transport=nil;*container=nil;*generation=0;*completed=0;
 if(getpid()!=process||geteuid()!=0)return NO;
 std::lock_guard<std::mutex> lock(ownerMutex);auto *s=connection(ownerDevice);
 if(graphicsClaimed243||brokerClaimed||ownedClaimed185||ownedStandard187||ownerPhase!=RTXNativeArmed||!s)return NO;
 // The no-queue/no-call gate shares the command creation/admission mutex.
 // An asynchronous close alone would leave a window for an admitted worker.
 if(!RTXClaimIdleCommandDeviceForBroker(ownerDevice))return NO;
 std::lock_guard<std::mutex> stateLock(s->mutex);
 if(s->closed||!s->armed||!s->session->ready()||s->session->completed()!=0)return NO;
 NSData *image=RTXCopyLibraryContainer(ownerLibrary);if(image.length!=RTXLibrary036::Bytes){[image release];return NO;}
 auto *target=[[RTXNativeTransport108 alloc]initWithState:s];if(!target){[image release];return NO;}
 brokerClaimed=true;*transport=target;*container=image;*generation=s->generation;*completed=s->session->completed();return YES;
}
