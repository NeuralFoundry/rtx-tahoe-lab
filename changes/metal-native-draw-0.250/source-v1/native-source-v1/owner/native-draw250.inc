extern "C" uint32_t rtx_native_graphics_draw250(const void*input,size_t inputBytes,void*image,size_t bytes,uint64_t*completion){
 namespace D=RTXDrawTransfer248;
 if(getpid()!=process||geteuid()!=0)return RTX_NATIVE_PROCESS;
 if(!completion||!input||inputBytes<D::Header||inputBytes>D::MaxPacket||!image||!bytes||bytes>D::MaxVertexBytes+D::MaxColorBytes||
    !P::separate(input,inputBytes,image,bytes)||!P::separate(input,inputBytes,completion,sizeof(*completion))||!P::separate(image,bytes,completion,sizeof(*completion)))return RTX_NATIVE_ARGUMENT;
 *completion=0;std::lock_guard<std::mutex>lock(ownerMutex);auto*s=connection(ownerDevice);
 if(!s||ownerPhase!=RTXNativeArmed||!graphicsClaimed243||brokerClaimed||ownedClaimed185||ownedStandard187)return RTX_NATIVE_STATE;
 std::lock_guard<std::mutex>stateLock(s->mutex);
 if(s->closed||!s->armed||!s->session->ready()||!s->dataSession->ready()||!s->graphicsSession->ready()||s->dispatchSession->active())return RTX_NATIVE_STATE;
 bool attempted=false,passed=false;uint64_t verified=0;D::Bytes request,result;
 try{@autoreleasepool{@try{
  request.assign(static_cast<const uint8_t*>(input),static_cast<const uint8_t*>(input)+inputBytes);
  D::Digest digest{};GSPDigest::SHA256 sha;sha.update(RTXGraphicsTemplate240::Program,4096);sha.finish(digest.data());D::Plan plan;
  if(!D::decode(request.data(),request.size(),s->generation,s->graphicsSession->completed()+1,digest,plan)||plan.vertexCount!=3||plan.width>64||plan.height>64||bytes!=plan.payloadBytes||
     !RTXGraphicsRequest240::validVertices249(request.data()+D::Header+plan.vertexBegin))return RTX_NATIVE_ARGUMENT;
  NSString*folder=[s->directory stringByAppendingPathComponent:[NSString stringWithFormat:@"graphics-draw250-%llu",s->graphicsSession->completed()+1]];
  if(newDirectory(folder)){OwnedFileTree187 sinks{folder};NativeIO io{s};s->firstError=0;
   if(sinks.save("application-request.bin",request.data(),request.size())){
    attempted=true;passed=s->graphicsSession->draw(io,sinks,*s->dataSession,result,verified,&request);
    if(passed)passed=result.size()==bytes&&verified==plan.serial&&D::readback(plan,request,result.data(),result.size(),verified)&&sinks.save("application-result.bin",result.data(),result.size());
   }
   FileSink summary{folder};const bool saved=saveSession(summary,s,passed);passed=passed&&saved;
  }
 }@catch(NSException*exception){(void)exception;}}}catch(...){ }
 if(!passed){if(attempted){s->graphicsSession->invalidate();s->dataSession->invalidate();s->session->retire();}return RTX_NATIVE_EVIDENCE;}
 std::memcpy(image,result.data(),bytes);*completion=verified;return 0;
}
