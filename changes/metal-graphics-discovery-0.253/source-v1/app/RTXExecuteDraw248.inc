// Called on the existing serialized command worker, retaining all operation
// objects until the same terminal status and callback path as compute work.
static NSError*executeDraw248(RTXApplication209_RTXCommandContext036*c,DrawOperation248&op){
 uint64_t serial=0;
 {std::lock_guard<std::mutex>lock(c->mutex);if(!c->graphics||c->closed||c->uncertain||!c->nextSerial)return [commandError(@"The graphics transport is unavailable")retain];serial=c->nextSerial;}
 NSError*error=nil;id transaction=RTXNewDrawSubmission248(op.pipeline,op.vertex,op.vertexOffset,op.first,op.count,op.color,op.colorOffset,op.width,op.height,op.pitch,c->generation,serial,&error);
 if(!transaction)return [commandError(error.localizedDescription?:@"Unable to stage graphics buffers",MTLCommandBufferErrorInvalidResource)retain];
 NSData*request=RTXCopyDrawRequest248(transaction),*catalog=RTXCopyRenderPipelineContainer247(op.pipeline),*result=nil;uint64_t completion=0;BOOL called=NO,success=NO;
 {std::lock_guard<std::mutex>lock(c->mutex);if(request&&catalog&&!c->closed&&!c->uncertain){called=YES;++c->calls;c->nextSerial=serial==UINT64_MAX?0:serial+1;}}
 if(called){insideTransport=c;
  try{@try{success=[c->graphics executeGraphicsRequest:request libraryContainer:catalog result:&result completion:&completion error:&error];}@catch(NSException*e){error=commandError(e.reason?:@"Graphics transport raised an exception");success=NO;}}catch(...){error=commandError(@"Graphics transport raised a C++ exception");success=NO;}
  insideTransport=nullptr;result=[result isKindOfClass:[NSData class]]?[result copy]:nil;
 }
 BOOL published=NO;
 {std::lock_guard<std::mutex>lock(c->mutex);
  if(called&&success&&!error&&!c->closed&&result)published=RTXPublishDrawSubmission248(transaction,request,result,completion);
  if(published)++c->publications;else if(called){c->uncertain=YES;for(auto*q:c->queues)kickLocked(q);}
 }
 NSError*failure=nil;if(!published){RTXCancelDrawSubmission248(transaction);failure=[commandError(error.localizedDescription?:@"Graphics completion could not be published; further device work is disabled")retain];}
 [result release];[catalog release];[request release];[transaction release];return failure;
}
