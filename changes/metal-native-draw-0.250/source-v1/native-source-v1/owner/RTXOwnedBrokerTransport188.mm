#import "RTXOwnedBrokerTransport188.h"
#include "OwnedBroker188.hpp"
#include <memory>
#include <mutex>
#include <unistd.h>
#import <objc/runtime.h>
namespace O188=RTXOwnedBroker188;
static NSError*ownedError188(NSString*message){return [NSError errorWithDomain:@"RTXOwnedBroker188" code:1 userInfo:@{NSLocalizedDescriptionKey:message}];}
@interface RTXOwnedTransport188:NSObject<RTXCommandTransport>{
@public id<RTXOwnedChannel188>_channel;std::unique_ptr<O188::Client>_state;std::mutex _mutex;pid_t _pid;uid_t _uid;uint64_t _exchanges;
}
- (instancetype)initWithChannel:(id<RTXOwnedChannel188>)channel container:(NSData*)container generation:(uint64_t)generation;
- (BOOL)exchange:(const O188::Frame&)frame result:(O188::Bytes&)result completion:(uint64_t&)completion;
- (BOOL)hello;
- (void)close;
@end
@implementation RTXOwnedTransport188
- (instancetype)initWithChannel:(id<RTXOwnedChannel188>)channel container:(NSData*)container generation:(uint64_t)generation{
 if((self=[super init])){_channel=[channel retain];_pid=getpid();_uid=geteuid();_state=std::make_unique<O188::Client>(static_cast<const uint8_t*>(container.bytes),container.length,generation);}return self;
}
- (BOOL)exchange:(const O188::Frame&)frame result:(O188::Bytes&)result completion:(uint64_t&)completion{
 BOOL okay=NO;NSData*reply=nil;uint32_t uid=UINT32_MAX;int64_t pid=0;
 try{@try{
  if(getpid()==_pid&&geteuid()==_uid){++_exchanges;okay=[_channel exchange:[NSData dataWithBytes:frame.bytes.data()length:frame.size()]reply:&reply serverUID:&uid serverPID:&pid];}
  okay=okay&&[reply isKindOfClass:[NSData class]]&&_state->accept(static_cast<const uint8_t*>(reply.bytes),reply.length,uid,pid,result,completion);
 }@catch(NSException*exception){(void)exception;okay=NO;}}catch(...){okay=NO;}
 if(!okay){_state->fail();[_channel cancel];completion=0;}return okay;
}
- (BOOL)hello{if(getpid()!=_pid||geteuid()!=_uid)return NO;std::lock_guard<std::mutex>lock(_mutex);O188::Frame frame;O188::Bytes result;uint64_t completed=0;return _state->hello(frame)&&[self exchange:frame result:result completion:completed];}
- (BOOL)executeRequest:(NSData*)request libraryPayload:(NSData*)payload result:(NSData**)result completion:(uint64_t*)completion error:(NSError**)error{
 if(!result||!completion||!error)return NO;*result=nil;*completion=0;*error=nil;
 if(getpid()!=_pid||geteuid()!=_uid){*error=ownedError188(@"Transport belongs to a different process or user");return NO;}
 std::lock_guard<std::mutex>lock(_mutex);BOOL okay=NO;
 try{@try{
  if([request isKindOfClass:[NSData class]]&&[payload isKindOfClass:[NSData class]]){O188::Frame frame;O188::Bytes data;uint64_t local=0;
   if(_state->execute(static_cast<const uint8_t*>(request.bytes),request.length,static_cast<const uint8_t*>(payload.bytes),payload.length,frame)&&[self exchange:frame result:data completion:local]){
    *result=[NSData dataWithBytes:data.data()length:data.size()];okay=*result!=nil;if(okay)*completion=local;
   }
  }
 }@catch(NSException*exception){(void)exception;okay=NO;}}catch(...){okay=NO;}
 if(!okay){_state->fail();[_channel cancel];*result=nil;*completion=0;*error=ownedError188(@"Owned command reply could not be verified; this connection is retired");}return okay;
}
- (void)close{if(getpid()!=_pid||geteuid()!=_uid)return;std::lock_guard<std::mutex>lock(_mutex);O188::Frame frame;O188::Bytes result;uint64_t complete=0;if(_state&&_state->close(frame))[self exchange:frame result:result completion:complete];[_channel cancel];}
- (void)dealloc{[self close];[_channel release];[super dealloc];}
@end
id<RTXCommandTransport>RTXNewOwnedTransport188(id<RTXOwnedChannel188>channel,NSData*container,uint64_t generation,NSError**error){
 if(error)*error=nil;if(geteuid()==0||!channel||![container isKindOfClass:[NSData class]]||container.length!=RTXCatalog187::Bytes||!generation){if(error)*error=ownedError188(@"Invalid owned application transport inputs");return nil;}
 RTXOwnedTransport188*transport=nil;
 try{@try{transport=[[RTXOwnedTransport188 alloc]initWithChannel:channel container:container generation:generation];if(transport&&[transport hello])return transport;}@catch(NSException*exception){(void)exception;}}catch(...){ }
 [transport release];if(error)*error=ownedError188(@"Owned broker handshake failed");return nil;
}
void RTXCloseOwnedTransport188(id<RTXCommandTransport>transport){if(object_getClass(transport)==[RTXOwnedTransport188 class])[(RTXOwnedTransport188*)transport close];}
NSDictionary*RTXCopyOwnedTransportInfo188(id<RTXCommandTransport>transport){
 if(object_getClass(transport)!=[RTXOwnedTransport188 class])return nil;auto*t=(RTXOwnedTransport188*)transport;std::lock_guard<std::mutex>lock(t->_mutex);
 return [@{@"phase":@(unsigned(t->_state->phase())),@"generation":@(t->_state->generation()),@"session":@(t->_state->session()),@"completed":@(t->_state->completed()),@"native_serial":@(t->_state->nativeSerial()),@"server_pid":@(t->_state->serverPID()),@"process_id":@(t->_pid),@"uid":@(t->_uid),@"channel_exchanges":@(t->_exchanges)}copy];
}
