#import "RTXNativeOwnedBroker188.h"
#include "Lifecycle202.hpp"
#include "ProgramAdmission208.hpp"
#include "OwnedBroker208.hpp"
#include <xpc/xpc.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <stdexcept>
#include <cstdlib>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
namespace OB188=RTXOwnedBroker208;
using Claim188=BOOL(*)(id<RTXCommandTransport>*,NSData**,uint64_t*,uint64_t*);
using Retire188=void(*)();
struct OwnedBackend208 {
 Claim188 claimFunction;Retire188 retireFunction;id<RTXCommandTransport>transport=nil;NSData*container=nil;bool retired=false,retireFailed=false;
 RTXOwnedLifecycle202::State lifecycle;RTXProgramAdmission208::Gate admission;
 OwnedBackend208(Claim188 c,Retire188 r,const RTXOwnedLifecycle202::Callbacks&callbacks,const RTXProgramAdmission208::Callbacks&program):claimFunction(c),retireFunction(r),lifecycle(callbacks){if(!admission.bind(program,getpid(),geteuid()))throw std::runtime_error("Root program admission");}
 ~OwnedBackend208(){if(!lifecycle.blocked())releaseObjects();}
 void releaseObjects(){if(lifecycle.blocked())return;id old=transport;transport=nil;[old release];old=container;container=nil;[old release];}
 bool claim(std::array<uint8_t,RTXCatalog187::Bytes>&out,uint64_t&gen,uint64_t&completed){
  if(transport||retired)return false;id<RTXCommandTransport>target=nil;NSData*image=nil;
  if(!claimFunction(&target,&image,&gen,&completed)){[target release];[image release];return false;}
  if(!target||image.length!=out.size()){[target release];[image release];return false;}
  transport=target;container=image;std::memcpy(out.data(),image.bytes,out.size());return true;
 }
 bool admit(const uint8_t*p,size_t n){return admission.admit(p,n,getpid(),geteuid());}
 bool execute(const OB188::Bytes&request,const uint8_t*payload,size_t bytes,OB188::Bytes&out,uint64_t&completion){
  if(!transport||!container||retired||!payload||bytes!=4608)return false;NSData*result=nil;NSError*error=nil;BOOL okay=NO;
  @try{okay=[transport executeRequest:[NSData dataWithBytes:request.data()length:request.size()]libraryPayload:[NSData dataWithBytes:payload length:bytes]result:&result completion:&completion error:&error];}
  @catch(NSException*exception){(void)exception;return false;}
  if(!okay||error||![result isKindOfClass:[NSData class]]||result.length>RTXBatch187::MaxPayload)return false;
  OB188::Bytes copy(result.length);[result getBytes:copy.data()length:copy.size()];out.swap(copy);return true;
 }
 void retire()noexcept{
  if(retired||lifecycle.blocked())return;
  if(!lifecycle.withdraw()){retireFailed=true;return;}
  @try{try{retireFunction();retired=true;}catch(...){retireFailed=true;lifecycle.quarantine();}}
  @catch(NSException*exception){(void)exception;retireFailed=true;lifecycle.quarantine();}
 }
};
static NSString*frameHash208(const OB188::Bytes&bytes){GSPDigest::SHA256 h;uint8_t digest[32];if(!bytes.empty())h.update(bytes.data(),bytes.size());h.finish(digest);NSMutableString*s=[NSMutableString string];for(auto b:digest)[s appendFormat:@"%02x",b];return s;}
struct OwnedPeer208 {OB188::Peer peer;bool barrier=false;};
struct OwnedServer208:std::enable_shared_from_this<OwnedServer208> {
 OB188::Core core;OwnedBackend208 backend;NSString*directory;NSMutableArray*records;
 dispatch_queue_t queue=dispatch_queue_create("local.emre.RTXOwnedBroker208.serial",DISPATCH_QUEUE_SERIAL);
 dispatch_semaphore_t drained=dispatch_semaphore_create(0);dispatch_source_t timer=nullptr;xpc_connection_t listener=nullptr;
 std::map<xpc_connection_t,std::shared_ptr<OwnedPeer208>> peers;
 uint32_t uid,stopAfter;uint64_t messages=0,accepted=0,closed=0,lateMessages=0,barriers=0;
 bool stopping=false,listenerClosed=false,timerClosed=false,finished=false,failedBeforeStop=false;int result=1;std::string reason;
 OwnedServer208(Claim188 c,Retire188 r,NSString*d,uint32_t u,uint32_t stop,const RTXOwnedLifecycle202::Callbacks&callbacks,const RTXProgramAdmission208::Callbacks&program):backend(c,r,callbacks,program),directory([d copy]),records([NSMutableArray new]),uid(u),stopAfter(stop){}
 ~OwnedServer208(){for(auto&p:peers)xpc_release(p.first);if(listener)xpc_release(listener);if(timer)dispatch_release(timer);dispatch_release(queue);dispatch_release(drained);[directory release];[records release];}
 bool save(NSString*name,NSDictionary*value){NSError*error=nil;NSData*raw=[NSJSONSerialization dataWithJSONObject:value options:NSJSONWritingSortedKeys error:&error];return raw&&!error&&[raw writeToFile:[directory stringByAppendingPathComponent:name]options:NSDataWritingWithoutOverwriting error:&error]&&!error;}
 void maybeDone(){
  if(!stopping||finished||!listenerClosed||!timerClosed||!peers.empty()||barriers)return;finished=true;
  bool okay=!failedBeforeStop&&!backend.retireFailed&&(reason=="requested"||reason=="message_limit");
  NSDictionary*report=@{@"implementation_abi":@208,@"ready_attempted":@(backend.lifecycle.readyAttempted()),@"activated":@(backend.lifecycle.activated()),@"withdraw_attempted":@(backend.lifecycle.withdrawAttempted()),@"withdraw_succeeded":@(backend.lifecycle.withdrawn()),@"close_permitted":@(!backend.lifecycle.blocked()),@"reason":@(reason.c_str()),@"pid":@(getpid()),@"uid":@(geteuid()),@"allowed_uid":@(uid),@"messages":@(messages),@"executions":@(core.executions()),@"completed":@(core.completed()),@"failed_before_stop":@(failedBeforeStop),@"records":records};
  bool saved=save(@"server-result188.json",report);
  NSDictionary*closure=@{@"implementation_abi":@208,@"drained":@YES,@"listener_closed":@(listenerClosed),@"timer_closed":@(timerClosed),@"active_connections":@(peers.size()),@"accepted_connections":@(accepted),@"closed_connections":@(closed),@"pending_send_barriers":@(barriers),@"rejected_after_stop":@(lateMessages),@"backend_retired":@(backend.retired),@"retire_failed":@(backend.retireFailed),@"backend_released":@(!backend.transport&&!backend.container),@"result_saved":@(saved),@"pid":@(getpid())};
  saved=save(@"server-drained208.json",closure)&&saved;result=backend.lifecycle.blocked()?70:(saved?(okay?0:result):20);dispatch_semaphore_signal(drained);
 }
 void cancelPeer(xpc_connection_t remote,const std::shared_ptr<OwnedPeer208>&peer){
  if(peer->barrier)return;peer->barrier=true;++barriers;std::weak_ptr<OwnedServer208>weak=shared_from_this();
  // A send barrier is not a target-queue barrier. Marshal accounting back to
  // our serial queue and wait separately for the final INVALID callback.
  xpc_retain(remote);xpc_connection_send_barrier(remote,^{
   xpc_connection_cancel(remote);
   if(auto state=weak.lock()){dispatch_async(state->queue,^{@autoreleasepool{--state->barriers;state->maybeDone();}});}
   xpc_release(remote);
  });
 }
 void stop(const char*why,int code){
  if(stopping)return;stopping=true;reason=why;result=code;failedBeforeStop=core.failed();
  // This method and execute callbacks use one serial queue. Retire after the
  // last effective call returned; no later frame can reach the backend.
  core.stop(backend);backend.releaseObjects();dispatch_source_cancel(timer);xpc_connection_cancel(listener);
  for(auto&p:peers)cancelPeer(p.first,p.second);maybeDone();
 }
 void disconnected(xpc_connection_t remote,const std::shared_ptr<OwnedPeer208>&peer){
  peer->peer.closed=true;auto found=peers.find(remote);if(found==peers.end())return;
  peers.erase(found);++closed;xpc_release(remote);maybeDone();
 }
 void message(xpc_connection_t remote,const std::shared_ptr<OwnedPeer208>&peer,xpc_object_t message){
  if(xpc_get_type(message)==XPC_TYPE_ERROR){
   if(message==XPC_ERROR_CONNECTION_INVALID)disconnected(remote,peer);else{xpc_connection_cancel(remote);peer->peer.closed=true;}return;
  }
  if(stopping){++lateMessages;cancelPeer(remote,peer);return;}
  try{@try{
   const uid_t caller=xpc_connection_get_euid(remote);const pid_t pid=xpc_connection_get_pid(remote);
   bool shape=xpc_get_type(message)==XPC_TYPE_DICTIONARY&&xpc_dictionary_get_count(message)==1;
   xpc_object_t value=shape?xpc_dictionary_get_value(message,"frame"):nullptr;shape=shape&&value&&xpc_get_type(value)==XPC_TYPE_DATA;OB188::Bytes input;
   if(shape){const size_t n=xpc_data_get_length(value);shape=n>=OB188::Header&&n<=OB188::MaxFrame;if(shape){const auto*p=static_cast<const uint8_t*>(xpc_data_get_bytes_ptr(value));input.assign(p,p+n);}}
   auto output=core.receive(peer->peer,input.data(),input.size(),caller==uid&&pid>0,backend);OB188::Identity response;bool parsed=OB188::read(output.bytes.data(),output.size(),response);
   if(records.count==256)[records removeObjectAtIndex:0];[records addObject:@{@"peer_uid":@(caller),@"peer_pid":@(pid),@"shape":@(shape),@"request_bytes":@(input.size()),@"request_sha256":frameHash208(input),@"reply_bytes":@(output.size()),@"reply_sha256":frameHash208(output.bytes),@"status":@(parsed?unsigned(response.status):999)}];++messages;
   xpc_object_t reply=xpc_get_type(message)==XPC_TYPE_DICTIONARY?xpc_dictionary_create_reply(message):nullptr;
   if(reply){xpc_dictionary_set_data(reply,"frame",output.bytes.data(),output.size());xpc_connection_send_message(remote,reply);xpc_release(reply);}else peer->peer.closed=true;
   if(peer->peer.closed)cancelPeer(remote,peer);
   if(core.failed())stop("backend_failure",21);else if(stopAfter&&messages>=stopAfter)stop("message_limit",0);
  }@catch(NSException*exception){(void)exception;stop("exception",22);}}catch(...){stop("exception",22);}
 }
 void accept(xpc_object_t event){
  if(xpc_get_type(event)==XPC_TYPE_ERROR){if(event==XPC_ERROR_CONNECTION_INVALID){listenerClosed=true;if(!stopping)stop("listener_error",23);maybeDone();}else stop("listener_error",23);return;}
  if(xpc_get_type(event)!=XPC_TYPE_CONNECTION){stop("listener_event",24);return;}
  auto remote=reinterpret_cast<xpc_connection_t>(event);auto peer=std::make_shared<OwnedPeer208>();xpc_retain(remote);peers.emplace(remote,peer);++accepted;
  std::weak_ptr<OwnedServer208>weak=shared_from_this();xpc_connection_set_target_queue(remote,queue);
  // The map owns the retain until INVALID. The connection's own persistent
  // handler must not retain that same connection through its block capture.
  __unsafe_unretained xpc_connection_t borrowed=remote;
  xpc_connection_set_event_handler(remote,^(xpc_object_t event){@autoreleasepool{if(auto state=weak.lock())state->message(borrowed,peer,event);}});xpc_connection_resume(remote);
  if(stopping)cancelPeer(remote,peer);else if(peers.size()>16)stop("connection_limit",25);
 }
};
static std::mutex activeMutex208;static std::weak_ptr<OwnedServer208>active208;static bool serving208=false;static std::shared_ptr<OwnedServer208>quarantined208;
extern "C" int rtx_owned_broker_stop208(){
 if(geteuid()!=0)return 2;std::shared_ptr<OwnedServer208>state;{std::lock_guard<std::mutex>lock(activeMutex208);state=active208.lock();}
 if(!state)return 1;dispatch_async(state->queue,^{@autoreleasepool{state->stop("requested",0);}});return 0;
}
extern "C" int rtx_owned_broker_serve208(const char*service,uint32_t allowedUID,void*claim,void*retire,const char*evidenceDirectory,uint32_t stopAfter,uint32_t timeoutSeconds,const RTXOwnedLifecycle202::Callbacks*callbacks,const RTXProgramAdmission208::Callbacks*program){
 if(!program||program->abi!=208||program->bytes!=sizeof(*program)||!program->context||!program->function||program->reserved)return 2;
 if(!callbacks||!RTXOwnedLifecycle202::valid(*callbacks))return 2;
 if(geteuid()!=0||!service||!claim||!retire||!evidenceDirectory||!allowedUID||!timeoutSeconds||timeoutSeconds>3600)return 2;
 @autoreleasepool{
  NSString*name=@(service),*directory=@(evidenceDirectory);struct stat metadata{};char canonical[PATH_MAX];
  // Foundation abbreviates /private/var to /var; POSIX realpath is the
  // canonical contract shared with the native launcher and file inventory.
  if(![name hasPrefix:@"local.emre.RTXOwnedBroker208."]||name.length>100||![directory isAbsolutePath]||!realpath(evidenceDirectory,canonical)||std::strcmp(canonical,evidenceDirectory)||lstat(evidenceDirectory,&metadata)||!S_ISDIR(metadata.st_mode)||metadata.st_uid||(metadata.st_mode&0022))return 3;
  for(NSUInteger i=0;i<name.length;++i){unichar c=[name characterAtIndex:i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_'))return 3;}
  {std::lock_guard<std::mutex>lock(activeMutex208);if(serving208||quarantined208)return 6;serving208=true;}
  auto state=std::make_shared<OwnedServer208>(reinterpret_cast<Claim188>(claim),reinterpret_cast<Retire188>(retire),directory,allowedUID,stopAfter,*callbacks,*program);
  state->listener=xpc_connection_create_mach_service(service,state->queue,XPC_CONNECTION_MACH_SERVICE_LISTENER);
  if(!state->listener){std::lock_guard<std::mutex>lock(activeMutex208);serving208=false;return 4;}
  state->timer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,state->queue);std::weak_ptr<OwnedServer208>weak=state;
  xpc_connection_set_event_handler(state->listener,^(xpc_object_t event){@autoreleasepool{if(auto s=weak.lock())s->accept(event);}});
  dispatch_source_set_event_handler(state->timer,^{@autoreleasepool{if(auto s=weak.lock())s->stop("deadline",124);}});
  dispatch_source_set_cancel_handler(state->timer,^{@autoreleasepool{if(auto s=weak.lock()){s->timerClosed=true;s->maybeDone();}}});
  dispatch_sync(state->queue,^{@autoreleasepool{
   dispatch_source_set_timer(state->timer,dispatch_time(DISPATCH_TIME_NOW,uint64_t(timeoutSeconds)*NSEC_PER_SEC),DISPATCH_TIME_FOREVER,0);dispatch_resume(state->timer);xpc_connection_resume(state->listener);
   {std::lock_guard<std::mutex>lock(activeMutex208);active208=state;}
   if(!state->save(@"listening188.json",@{@"implementation_abi":@208,@"listening":@YES,@"pid":@(getpid()),@"service":name}))state->stop("ready_write",5);
   else if(!state->backend.lifecycle.activate())state->stop("activation_failure",26);
   else if(!state->save(@"activated208.json",@{@"implementation_abi":@208,@"activated":@YES,@"pid":@(getpid())}))state->stop("activation_write",27);
  }});
  dispatch_semaphore_wait(state->drained,DISPATCH_TIME_FOREVER);
  // The signaling callback must finish before the caller closes its native
  // owner. All callbacks use weak state ownership to avoid retained cycles.
  dispatch_sync(state->queue,^{});const int result=state->result;
  {std::lock_guard<std::mutex>lock(activeMutex208);active208.reset();serving208=false;if(state->backend.lifecycle.blocked())quarantined208=state;}
  return result;
 }
}

// Serve result70 or close_permitted==0 forbids closing the native owner. A
// failed withdrawal/retirement retains the backend for recovery; no retry.
extern "C" int rtx_owned_broker_close_permitted208(){
 if(geteuid()!=0)return 0;std::lock_guard<std::mutex>lock(activeMutex208);
 return !serving208&&!quarantined208;
}
