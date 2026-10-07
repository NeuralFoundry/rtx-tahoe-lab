#import <Foundation/Foundation.h>
#include "RTXBrokerWire.hpp"
#include <xpc/xpc.h>
#include <memory>
#include <unistd.h>
using namespace RTXBroker040;
struct Answer {Frame frame;uid_t serverUID=UINT32_MAX;pid_t serverPID=0;bool shape=false;dispatch_semaphore_t done;
 Answer():done(dispatch_semaphore_create(0)){}~Answer(){dispatch_release(done);}};
int main(int argc,const char **argv){if(argc!=4||geteuid()==0)return 2;@autoreleasepool {
 NSString *service=@(argv[1]),*mode=@(argv[2]);if(![service hasPrefix:@"local.emre.RTXMetalBroker040."]||!([mode isEqual:@"hello"]||[mode isEqual:@"bad_opcode"]||[mode isEqual:@"bad_type"]))return 3;
 dispatch_queue_t queue=dispatch_queue_create("local.emre.RTXBroker040.client",DISPATCH_QUEUE_SERIAL);
 xpc_connection_t connection=xpc_connection_create_mach_service(argv[1],queue,XPC_CONNECTION_MACH_SERVICE_PRIVILEGED);if(!connection)return 4;
 xpc_connection_set_event_handler(connection,^(xpc_object_t e){(void)e;});xpc_connection_resume(connection);
 auto answer=std::make_shared<Answer>();auto request=write({Op::Hello,Status::OK,false,0,1,0,0});
 if([mode isEqual:@"bad_opcode"])P::Q::put32(request.bytes.data()+12,77);
 xpc_object_t message=xpc_dictionary_create(nullptr,nullptr,0);
 if([mode isEqual:@"bad_type"])xpc_dictionary_set_string(message,"frame","invalid data type");
 else xpc_dictionary_set_data(message,"frame",request.bytes.data(),request.size);
 xpc_connection_send_message_with_reply(connection,message,queue,^(xpc_object_t reply){
  if(xpc_get_type(reply)==XPC_TYPE_DICTIONARY&&xpc_dictionary_get_count(reply)==1){xpc_object_t frame=xpc_dictionary_get_value(reply,"frame");
   if(frame&&xpc_get_type(frame)==XPC_TYPE_DATA){size_t n=xpc_data_get_length(frame);if(n>=Header&&n<=MaxFrame){answer->frame.size=n;std::memcpy(answer->frame.bytes.data(),xpc_data_get_bytes_ptr(frame),n);answer->shape=true;}}
  }
  answer->serverUID=xpc_connection_get_euid(connection);answer->serverPID=xpc_connection_get_pid(connection);dispatch_semaphore_signal(answer->done);
 });xpc_release(message);
 if(dispatch_semaphore_wait(answer->done,dispatch_time(DISPATCH_TIME_NOW,8*NSEC_PER_SEC))){xpc_connection_cancel(connection);return 5;}
 HeaderView h;bool parsed=answer->shape&&read(answer->frame.bytes.data(),answer->frame.size,h)&&h.reply;
 NSDictionary *result=@{@"parsed":@(parsed),@"mode":mode,@"client_uid":@(geteuid()),@"client_pid":@(getpid()),@"server_uid":@(answer->serverUID),@"server_pid":@(answer->serverPID),@"status":@(parsed?uint32_t(h.status):999),@"reply":[[NSData dataWithBytes:answer->frame.bytes.data()length:answer->frame.size]base64EncodedStringWithOptions:0],@"gpu_commands_submitted":@NO};
 NSData *json=[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingSortedKeys error:nil];BOOL saved=[json writeToFile:@(argv[3])options:NSDataWritingWithoutOverwriting error:nil];
 xpc_connection_cancel(connection);xpc_release(connection);dispatch_release(queue);
 if(!saved||!parsed||answer->serverUID!=0||answer->serverPID<=0)return 6;
 printf("{\"parsed\":true,\"client_uid\":%u,\"server_uid\":%u,\"status\":%u}\n",unsigned(geteuid()),unsigned(answer->serverUID),unsigned(h.status));
 }return 0;}
