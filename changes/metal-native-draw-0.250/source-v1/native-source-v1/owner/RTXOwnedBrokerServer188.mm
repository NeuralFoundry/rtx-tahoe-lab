#import "RTXNativeOwnedBroker188.h"
#include "OwnedBroker188.hpp"
#include <xpc/xpc.h>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
namespace OB188=RTXOwnedBroker188;
using Claim188=BOOL(*)(id<RTXCommandTransport>*,NSData**,uint64_t*,uint64_t*);
using Retire188=void(*)();
struct OwnedBackend188 {
 Claim188 claimFunction;Retire188 retireFunction;id<RTXCommandTransport>transport=nil;NSData*container=nil;
 ~OwnedBackend188(){[transport release];[container release];}
 bool claim(std::array<uint8_t,RTXCatalog187::Bytes>&out,uint64_t&gen,uint64_t&completed){
  if(transport)return false;id<RTXCommandTransport>target=nil;NSData*image=nil;
  if(!claimFunction(&target,&image,&gen,&completed)){[target release];[image release];return false;}
  if(!target||image.length!=out.size()){[target release];[image release];return false;}transport=target;container=image;std::memcpy(out.data(),image.bytes,out.size());return true;
 }
 bool execute(const OB188::Bytes&request,OB188::Bytes&out,uint64_t&completion){
  if(!transport||!container)return false;NSData*result=nil;NSError*error=nil;BOOL okay=NO;
  @try{okay=[transport executeRequest:[NSData dataWithBytes:request.data()length:request.size()]libraryPayload:[container subdataWithRange:NSMakeRange(640,4608)]result:&result completion:&completion error:&error];}
  @catch(NSException*exception){(void)exception;return false;}
  if(!okay||error||![result isKindOfClass:[NSData class]]||result.length>RTXBatch187::MaxPayload)return false;
  OB188::Bytes copy(result.length);[result getBytes:copy.data()length:copy.size()];out.swap(copy);return true;
 }
 void retire(){retireFunction();}
};
static NSString*frameHash188(const OB188::Bytes&bytes){GSPDigest::SHA256 h;uint8_t digest[32];if(!bytes.empty())h.update(bytes.data(),bytes.size());h.finish(digest);NSMutableString*s=[NSMutableString string];for(auto b:digest)[s appendFormat:@"%02x",b];return s;}
struct OwnedServer188 {
 OB188::Core core;OwnedBackend188 backend;NSString*directory;NSMutableArray*records;uint32_t uid,stopAfter;unsigned connections=0;uint64_t messages=0;bool finishing=false;
 OwnedServer188(Claim188 c,Retire188 r,NSString*d,uint32_t u,uint32_t stop):backend{c,r},directory([d copy]),records([NSMutableArray new]),uid(u),stopAfter(stop){}
 ~OwnedServer188(){[directory release];[records release];}
 void finish(const char*reason){if(finishing)return;finishing=true;
  NSDictionary*report=@{@"reason":@(reason),@"pid":@(getpid()),@"uid":@(geteuid()),@"allowed_uid":@(uid),@"messages":@(messages),@"executions":@(core.executions()),@"completed":@(core.completed()),@"failed":@(core.failed()),@"records":records};
  BOOL saved=[[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingSortedKeys error:nil]writeToFile:[directory stringByAppendingPathComponent:@"server-result188.json"]options:NSDataWritingWithoutOverwriting error:nil];fflush(stdout);fflush(stderr);exit(saved?0:20);
 }
};
extern "C" int rtx_owned_broker_serve188(const char*service,uint32_t allowedUID,void*claim,void*retire,const char*evidenceDirectory,uint32_t stopAfter,uint32_t timeoutSeconds){
 if(geteuid()!=0||!service||!claim||!retire||!evidenceDirectory||!allowedUID||!timeoutSeconds||timeoutSeconds>3600)return 2;
 @autoreleasepool{
  NSString*name=@(service),*directory=@(evidenceDirectory);struct stat metadata{};
  if(![name hasPrefix:@"local.emre.RTXOwnedBroker188."]||name.length>100||![directory isAbsolutePath]||![directory isEqual:directory.stringByResolvingSymlinksInPath]||lstat(evidenceDirectory,&metadata)||!S_ISDIR(metadata.st_mode)||metadata.st_uid||(metadata.st_mode&0022))return 3;
  auto state=std::make_shared<OwnedServer188>(reinterpret_cast<Claim188>(claim),reinterpret_cast<Retire188>(retire),directory,allowedUID,stopAfter);
  dispatch_queue_t queue=dispatch_queue_create("local.emre.RTXOwnedBroker188.serial",DISPATCH_QUEUE_SERIAL);xpc_connection_t listener=xpc_connection_create_mach_service(service,queue,XPC_CONNECTION_MACH_SERVICE_LISTENER);if(!listener)return 4;
  xpc_connection_set_event_handler(listener,^(xpc_object_t event){
   if(xpc_get_type(event)!=XPC_TYPE_CONNECTION)return;auto connection=(xpc_connection_t)event;if(state->connections>=16){xpc_connection_cancel(connection);return;}++state->connections;
   auto peer=std::make_shared<OB188::Peer>();auto counted=std::make_shared<bool>(true);xpc_connection_set_target_queue(connection,queue);__unsafe_unretained xpc_connection_t remote=connection;
   xpc_connection_set_event_handler(connection,^(xpc_object_t message){@autoreleasepool{
    if(xpc_get_type(message)==XPC_TYPE_ERROR){if(*counted){--state->connections;*counted=false;}peer->closed=true;return;}
    try{@try{
     const uid_t uid=xpc_connection_get_euid(remote);const pid_t pid=xpc_connection_get_pid(remote);bool shape=xpc_get_type(message)==XPC_TYPE_DICTIONARY&&xpc_dictionary_get_count(message)==1;
     xpc_object_t value=shape?xpc_dictionary_get_value(message,"frame"):nullptr;shape=shape&&value&&xpc_get_type(value)==XPC_TYPE_DATA;OB188::Bytes input;
     if(shape){const size_t n=xpc_data_get_length(value);shape=n>=OB188::Header&&n<=OB188::MaxFrame;if(shape){const auto*p=static_cast<const uint8_t*>(xpc_data_get_bytes_ptr(value));input.assign(p,p+n);}}
     auto output=state->core.receive(*peer,input.data(),input.size(),uid==state->uid&&pid>0,state->backend);OB188::Identity response;const bool parsed=OB188::read(output.bytes.data(),output.size(),response);
     if(state->records.count==256)[state->records removeObjectAtIndex:0];[state->records addObject:@{@"peer_uid":@(uid),@"peer_pid":@(pid),@"shape":@(shape),@"request_bytes":@(input.size()),@"request_sha256":frameHash188(input),@"reply_bytes":@(output.size()),@"reply_sha256":frameHash188(output.bytes),@"status":@(parsed?unsigned(response.status):999)}];++state->messages;
     xpc_object_t reply=xpc_get_type(message)==XPC_TYPE_DICTIONARY?xpc_dictionary_create_reply(message):nullptr;
     if(reply){xpc_dictionary_set_data(reply,"frame",output.bytes.data(),output.size());xpc_connection_send_message(remote,reply);xpc_release(reply);}else{peer->closed=true;xpc_connection_cancel(remote);}
     if(state->stopAfter&&state->messages>=state->stopAfter)dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC),queue,^{state->finish("message_limit");});
    }@catch(NSException*exception){(void)exception;peer->closed=true;state->core.stop(state->backend);xpc_connection_cancel(remote);}}catch(...){peer->closed=true;state->core.stop(state->backend);xpc_connection_cancel(remote);}
   }});xpc_connection_resume(connection);
  });xpc_connection_resume(listener);
  NSData*ready=[NSJSONSerialization dataWithJSONObject:@{@"listening":@YES,@"pid":@(getpid()),@"service":name}options:NSJSONWritingSortedKeys error:nil];if(![ready writeToFile:[directory stringByAppendingPathComponent:@"listening188.json"]options:NSDataWritingWithoutOverwriting error:nil])return 5;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW,uint64_t(timeoutSeconds)*NSEC_PER_SEC),queue,^{state->finish("deadline");});dispatch_main();
 }
}
