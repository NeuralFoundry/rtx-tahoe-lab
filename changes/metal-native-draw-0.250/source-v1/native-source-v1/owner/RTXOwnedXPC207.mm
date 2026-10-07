#import "RTXOwnedBrokerTransport207.h"
#include "OwnedBroker207.hpp"
#include <xpc/xpc.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <unistd.h>
using namespace RTXOwnedBroker207;
struct OwnedAnswer207 {
 std::mutex mutex;Frame frame;uint32_t uid=UINT32_MAX;int64_t pid=0;bool shape=false,done=false,abandoned=false;
 dispatch_semaphore_t semaphore=dispatch_semaphore_create(0);
 ~OwnedAnswer207(){dispatch_release(semaphore);}
};
@interface RTXOwnedXPC207:NSObject<RTXOwnedChannel207>{
 xpc_connection_t _connection;dispatch_queue_t _queue;uint32_t _timeout;pid_t _process;uid_t _uid;
 std::mutex _exchangeMutex;std::shared_ptr<std::atomic<bool>> _failed,_cancelled;
}
- (id)initWithService:(NSString *)service timeout:(uint32_t)timeout;
@end
@implementation RTXOwnedXPC207
- (id)initWithService:(NSString *)service timeout:(uint32_t)timeout {
 if((self=[super init])){
  _process=getpid();_uid=geteuid();_timeout=timeout;_failed=std::make_shared<std::atomic<bool>>(false);_cancelled=std::make_shared<std::atomic<bool>>(false);
  _queue=dispatch_queue_create("local.emre.RTXOwnedApplication207.reply",DISPATCH_QUEUE_SERIAL);
  _connection=xpc_connection_create_mach_service(service.UTF8String,_queue,XPC_CONNECTION_MACH_SERVICE_PRIVILEGED);
  if(!_connection){[self release];return nil;}
  auto failed=_failed;
  xpc_connection_set_event_handler(_connection,^(xpc_object_t event){if(xpc_get_type(event)==XPC_TYPE_ERROR)failed->store(true);});
  xpc_connection_resume(_connection);
 }return self;
}
- (void)cancel {if(_cancelled)_cancelled->store(true);if(_failed)_failed->store(true);if(_connection)xpc_connection_cancel(_connection);}
- (void)dealloc {[self cancel];if(_connection)xpc_release(_connection);if(_queue)dispatch_release(_queue);[super dealloc];}
- (BOOL)exchange:(NSData *)request reply:(NSData **)reply serverUID:(uint32_t *)uid serverPID:(int64_t *)pid {
 if(!reply||!uid||!pid)return NO;*reply=nil;*uid=UINT32_MAX;*pid=0;
 std::lock_guard<std::mutex> call(_exchangeMutex);
 if(_failed->load()||getpid()!=_process||geteuid()!=_uid||request.length<Header||request.length>MaxFrame)return NO;
 auto answer=std::make_shared<OwnedAnswer207>();auto failed=_failed;
 xpc_object_t message=xpc_dictionary_create(nullptr,nullptr,0);xpc_dictionary_set_data(message,"frame",request.bytes,request.length);
 // The callback owns this retain even after caller timeout and object release.
 xpc_connection_t remote=_connection;xpc_retain(remote);
 xpc_connection_send_message_with_reply(remote,message,_queue,^(xpc_object_t response){
  {
   std::lock_guard<std::mutex> lock(answer->mutex);
   try{if(!answer->abandoned){
    xpc_object_t value=nullptr;
    if(xpc_get_type(response)==XPC_TYPE_DICTIONARY&&xpc_dictionary_get_count(response)==1)value=xpc_dictionary_get_value(response,"frame");
    if(value&&xpc_get_type(value)==XPC_TYPE_DATA){size_t n=xpc_data_get_length(value);if(n>=Header&&n<=MaxFrame){const auto*p=static_cast<const uint8_t*>(xpc_data_get_bytes_ptr(value));answer->frame.bytes.assign(p,p+n);answer->shape=true;}}
    answer->uid=xpc_connection_get_euid(remote);answer->pid=xpc_connection_get_pid(remote);answer->done=true;
    if(!answer->shape)failed->store(true);
   }}catch(...){answer->shape=false;answer->done=true;failed->store(true);}
  }
  dispatch_semaphore_signal(answer->semaphore);xpc_release(remote);
 });xpc_release(message);
 long waited=dispatch_semaphore_wait(answer->semaphore,dispatch_time(DISPATCH_TIME_NOW,uint64_t(_timeout)*NSEC_PER_MSEC));
 std::lock_guard<std::mutex> lock(answer->mutex);
 // Preserve a valid final reply preceding connection invalidation. _failed
 // still prevents another exchange. Explicit cancellation suppresses results.
 if(waited||!answer->done||!answer->shape||_cancelled->load()){answer->abandoned=true;[self cancel];return NO;}
 *reply=[NSData dataWithBytes:answer->frame.bytes.data()length:answer->frame.size()];*uid=answer->uid;*pid=answer->pid;return YES;
}
@end
id<RTXOwnedChannel207> RTXNewOwnedXPC207(NSString *service,uint32_t timeoutMilliseconds){
 if(geteuid()==0||![service isKindOfClass:[NSString class]]||![service hasPrefix:@"local.emre.RTXOwnedBroker207."]||service.length>100||timeoutMilliseconds<50||timeoutMilliseconds>60000)return nil;
 for(NSUInteger i=0;i<service.length;++i){unichar c=[service characterAtIndex:i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_'))return nil;}
 return [[RTXOwnedXPC207 alloc]initWithService:service timeout:timeoutMilliseconds];
}
