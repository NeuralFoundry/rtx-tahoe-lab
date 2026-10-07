#import <Foundation/Foundation.h>
#include "RTXBrokerServer.h"
#include "RTXNativeBroker.h"
#include "RTXNativeOwner.h"
#include "RTXBrokerWire.hpp"
#include <xpc/xpc.h>
#include <memory>
#include <unistd.h>

using namespace RTXBroker040;
using Claim=BOOL(*)(id<RTXCommandTransport> *,NSData **,uint64_t *,uint64_t *);
using NativeInfo=uint32_t(*)(RTXNativeOwnerInfo *,size_t);
struct NativeBackend {
 Claim claimFunction;id<RTXCommandTransport> transport=nil;NSData *container=nil;
 explicit NativeBackend(Claim f):claimFunction(f){}
 ~NativeBackend(){[transport release];[container release];}
 bool claim(std::array<uint8_t,RTXLibrary036::Bytes> &out,uint64_t &gen,uint64_t &completed){
  if(transport)return false;id<RTXCommandTransport> t=nil;NSData *data=nil;
  if(!claimFunction(&t,&data,&gen,&completed))return false;
  if(!t||data.length!=out.size()){[t release];[data release];return false;}
  transport=t;container=data;std::memcpy(out.data(),data.bytes,out.size());return true;
 }
 bool execute(const std::array<uint8_t,2112>&wire,std::array<uint8_t,2048>&out,uint64_t &completion){
  if(!transport||!container)return false;
  NSData *request=[NSData dataWithBytes:wire.data()length:wire.size()];NSData *payload=[container subdataWithRange:NSMakeRange(640,4608)],*result=nil;NSError *error=nil;
  BOOL ok=[transport executeRequest:request libraryPayload:payload result:&result completion:&completion error:&error];
  if(!ok||error||result.length!=out.size())return false;std::memcpy(out.data(),result.bytes,out.size());return true;
 }
};
struct Server {
 Core core;NativeBackend backend;NativeInfo native;uint32_t uid,stopAfter;unsigned connections=0;uint64_t messages=0,discarded=0;bool finishing=false;
 NSString *directory;NSMutableArray *records;RTXNativeOwnerInfo before{};
 Server(Claim c,NativeInfo n,uint32_t u,NSString *d,uint32_t stop):backend(c),native(n),uid(u),stopAfter(stop),directory([d copy]),records([NSMutableArray new]){}
 ~Server(){[directory release];[records release];}
 void finish(const char *reason){
  if(finishing)return;finishing=true;RTXNativeOwnerInfo after{};uint32_t rc=native(&after,sizeof(after));
  NSDictionary *report=@{@"reason":@(reason),@"server_uid":@(geteuid()),@"server_pid":@(getpid()),@"allowed_uid":@(uid),@"messages":@(messages),@"records":records,@"backend_ready":@(core.ready()),@"backend_failed":@(core.failed()),@"backend_executions":@(core.executions()),@"native_info_status":@(rc),@"native_before":[[NSData dataWithBytes:&before length:sizeof(before)]base64EncodedStringWithOptions:0],@"native_after":[[NSData dataWithBytes:&after length:sizeof(after)]base64EncodedStringWithOptions:0]};
  NSData *json=[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingSortedKeys error:nil];
  BOOL saved=[json writeToFile:[directory stringByAppendingPathComponent:@"server-result.json"]options:NSDataWritingWithoutOverwriting error:nil];
  fflush(stdout);fflush(stderr);exit(saved?0:20);
 }
};
extern "C" int rtx_broker_serve(const char *service,uint32_t allowedUID,void *claim,void *nativeInfo,const char *evidenceDirectory,uint32_t stopAfter,uint32_t timeoutSeconds){
 if(geteuid()!=0||!service||!claim||!nativeInfo||!evidenceDirectory||allowedUID==0||timeoutSeconds>3600)return 2;
 @autoreleasepool {
  NSString *name=@(service),*directory=@(evidenceDirectory);
  if(![name hasPrefix:@"local.emre.RTXMetalBroker040."]||name.length>100||![directory isAbsolutePath])return 3;
  auto state=std::make_shared<Server>(reinterpret_cast<Claim>(claim),reinterpret_cast<NativeInfo>(nativeInfo),allowedUID,directory,stopAfter);
  if(state->native(&state->before,sizeof(state->before)))return 4;
  dispatch_queue_t queue=dispatch_queue_create("local.emre.RTXBroker040.serial",DISPATCH_QUEUE_SERIAL);
  xpc_connection_t listener=xpc_connection_create_mach_service(service,queue,XPC_CONNECTION_MACH_SERVICE_LISTENER);if(!listener)return 5;
  xpc_connection_set_event_handler(listener,^(xpc_object_t event){
   if(xpc_get_type(event)!=XPC_TYPE_CONNECTION)return;
   xpc_connection_t connection=(xpc_connection_t)event;
   if(state->connections>=16){xpc_connection_cancel(connection);return;}++state->connections;
   auto peer=std::make_shared<Peer>();auto accounted=std::make_shared<bool>(true);
   xpc_connection_set_target_queue(connection,queue);
   __unsafe_unretained xpc_connection_t remote=connection;
   xpc_connection_set_event_handler(connection,^(xpc_object_t message){@autoreleasepool {
    if(xpc_get_type(message)==XPC_TYPE_ERROR){if(*accounted){--state->connections;*accounted=false;}peer->closed=true;return;}
    uid_t uid=xpc_connection_get_euid(remote);pid_t pid=xpc_connection_get_pid(remote);
    Frame input,output;bool shape=xpc_get_type(message)==XPC_TYPE_DICTIONARY&&xpc_dictionary_get_count(message)==1;
    xpc_object_t value=shape?xpc_dictionary_get_value(message,"frame"):nullptr;
    shape=shape&&value&&xpc_get_type(value)==XPC_TYPE_DATA;
    if(shape){size_t n=xpc_data_get_length(value);shape=n>=Header&&n<=MaxFrame;if(shape){input.size=n;std::memcpy(input.bytes.data(),xpc_data_get_bytes_ptr(value),n);}}
    output=state->core.receive(*peer,shape?input.bytes.data():nullptr,shape?input.size:0,uid==state->uid&&pid>0,state->backend);
    HeaderView response;bool decoded=read(output.bytes.data(),output.size,response);
    if(state->records.count==256){[state->records removeObjectAtIndex:0];++state->discarded;}
    [state->records addObject:@{@"peer_uid":@(uid),@"peer_pid":@(pid),@"authorized":@(uid==state->uid&&pid>0),@"shape":@(shape),@"status":@(decoded?uint32_t(response.status):999),@"request":[[NSData dataWithBytes:input.bytes.data()length:input.size]base64EncodedStringWithOptions:0],@"reply":[[NSData dataWithBytes:output.bytes.data()length:output.size]base64EncodedStringWithOptions:0]}];
    ++state->messages;xpc_object_t reply=xpc_dictionary_create_reply(message);
    if(reply){xpc_dictionary_set_data(reply,"frame",output.bytes.data(),output.size);xpc_connection_send_message(remote,reply);xpc_release(reply);}
    if(state->stopAfter&&state->messages>=state->stopAfter)dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC),queue,^{state->finish("message_limit");});
   }});
   xpc_connection_resume(connection);
  });
  xpc_connection_resume(listener);
  NSData *ready=[NSJSONSerialization dataWithJSONObject:@{@"listening":@YES,@"pid":@(getpid()),@"uid":@(geteuid()),@"service":name}options:NSJSONWritingSortedKeys error:nil];
  if(![ready writeToFile:[directory stringByAppendingPathComponent:@"listening.json"]options:NSDataWritingWithoutOverwriting error:nil])return 6;
  if(timeoutSeconds)dispatch_after(dispatch_time(DISPATCH_TIME_NOW,uint64_t(timeoutSeconds)*NSEC_PER_SEC),queue,^{state->finish("deadline");});
  dispatch_main();
 }
}
