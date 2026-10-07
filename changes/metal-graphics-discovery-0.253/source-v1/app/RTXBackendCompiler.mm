#import "RTXBackendCompiler.h"
#import "RTXNativeFunction.h"
#import "RTXDeviceCompiler.h"
#include "RTXLibraryCatalog187.hpp"
#import <objc/runtime.h>
#include <atomic>
#include <signal.h>
#include <unistd.h>
#include <climits>
#include <cstdlib>
#include <cstring>
static char compilerKey;
static std::atomic<unsigned> created{0},destroyed{0},requests{0},successes{0};
static NSError *failure(NSInteger code,NSString *message){return [NSError errorWithDomain:@"RTXBackendCompiler056" code:code userInfo:@{NSLocalizedDescriptionKey:message}];}
@interface RTXApplication209_RTXBackendCompiler056:NSObject {
 NSString *_helper,*_configuration,*_diagnostics;
}
- (id)initWithHelper:(NSString *)helper configuration:(NSString *)configuration diagnostics:(NSString *)diagnostics;
- (NSData *)copyLibrary:(NSData *)air name:(NSString *)name error:(NSError **)error;
@end
@implementation RTXApplication209_RTXBackendCompiler056
- (id)initWithHelper:(NSString *)helper configuration:(NSString *)configuration diagnostics:(NSString *)diagnostics{
 if((self=[super init])){_helper=[helper copy];_configuration=[configuration copy];_diagnostics=[diagnostics copy];++created;}return self;
}
- (void)dealloc{++destroyed;[_helper release];[_configuration release];[_diagnostics release];[super dealloc];}
- (NSData *)copyLibrary:(NSData *)air name:(NSString *)name error:(NSError **)error{
 @synchronized(self){
  ++requests;NSString *job=[_diagnostics stringByAppendingPathComponent:NSUUID.UUID.UUIDString];NSFileManager *fm=NSFileManager.defaultManager;
  if(![fm createDirectoryAtPath:job withIntermediateDirectories:NO attributes:@{NSFilePosixPermissions:@0700} error:error])return nil;
  if(![air writeToFile:[job stringByAppendingPathComponent:@"input.air"]options:NSDataWritingWithoutOverwriting error:error]||![name writeToFile:[job stringByAppendingPathComponent:@"entry.txt"]atomically:NO encoding:NSUTF8StringEncoding error:error])return nil;
  NSString *log=[job stringByAppendingPathComponent:@"helper.log"];if(![fm createFileAtPath:log contents:[NSData data]attributes:@{NSFilePosixPermissions:@0600}]){if(error)*error=failure(4,@"Cannot create compiler log");return nil;}
  NSFileHandle *output=[NSFileHandle fileHandleForWritingAtPath:log];NSTask *task=[NSTask new];task.executableURL=[NSURL fileURLWithPath:@"/usr/bin/python3"];
  task.arguments=@[@"-B",_helper,_configuration,job];task.standardInput=[NSFileHandle fileHandleWithNullDevice];task.standardOutput=output;task.standardError=output;
  BOOL launched=[task launchAndReturnError:error];BOOL timedOut=NO;
  if(launched){NSTimeInterval deadline=NSProcessInfo.processInfo.systemUptime+50;
   while(task.running&&NSProcessInfo.processInfo.systemUptime<deadline)[NSThread sleepForTimeInterval:0.02];
   if(task.running){timedOut=YES;pid_t pid=task.processIdentifier;if(getpgid(pid)==pid)kill(-pid,SIGKILL);else kill(pid,SIGKILL);}
   [task waitUntilExit];
  }
  BOOL ok=launched&&!timedOut&&task.terminationReason==NSTaskTerminationReasonExit&&task.terminationStatus==0;[output closeFile];[task release];
  if(!ok){NSData *raw=[NSData dataWithContentsOfFile:[job stringByAppendingPathComponent:@"result.json"]];NSDictionary *r=raw?[NSJSONSerialization JSONObjectWithData:raw options:0 error:nullptr]:nil;
   if(error)*error=failure(timedOut?5:4,[r[@"error"]isKindOfClass:NSString.class]?r[@"error"]:@"Backend compiler process failed");return nil;}
  NSData *container=[[NSData alloc]initWithContentsOfFile:[job stringByAppendingPathComponent:@"compiled.rtxlib"]];RTXCatalog187::Catalog catalog;
  if(!container||!RTXCatalog187::decode(static_cast<const uint8_t *>(container.bytes),container.length,catalog)||catalog.library.count!=1||![name isEqualToString:[NSString stringWithUTF8String:catalog.names[0]]]){
   [container release];if(error)*error=failure(6,@"Compiler returned an invalid library or entry");return nil;}
  ++successes;return container;
 }
}
@end
extern "C" BOOL RTXConfigureBackendCompiler056(id<MTLDevice> device,NSString *helper,NSString *configuration,NSString *diagnostics,NSError **error){
 if(error)*error=nil;
 if(!RTXDeviceCompilerOwnsDevice055(device)){if(error)*error=failure(1,@"Compiler requires this application device");return NO;}
 for(NSString *p in @[helper?:@"",configuration?:@"",diagnostics?:@""]){char resolved[PATH_MAX];if(![p isKindOfClass:NSString.class]||!p.isAbsolutePath||!realpath(p.fileSystemRepresentation,resolved)||std::strcmp(p.fileSystemRepresentation,resolved)){if(error)*error=failure(1,@"Compiler paths must be absolute and resolved");return NO;}}
 BOOL directory=NO;NSFileManager *fm=NSFileManager.defaultManager;
 if(![fm isReadableFileAtPath:helper]||![fm isReadableFileAtPath:configuration]||![fm fileExistsAtPath:diagnostics isDirectory:&directory]||!directory){if(error)*error=failure(1,@"Compiler configuration is missing");return NO;}
 @synchronized(device){
  if(objc_getAssociatedObject(device,&compilerKey)){if(error)*error=failure(2,@"Compiler already configured for device");return NO;}
  RTXApplication209_RTXBackendCompiler056 *compiler=[[RTXApplication209_RTXBackendCompiler056 alloc]initWithHelper:helper configuration:configuration diagnostics:diagnostics];objc_setAssociatedObject(device,&compilerKey,compiler,OBJC_ASSOCIATION_RETAIN_NONATOMIC);[compiler release];return YES;
 }
}
NSData *RTXCopyCompiledNativeLibrary056(id<MTLDevice> device,id<MTLFunction> function,NSError **error){
 if(error)*error=nil;NSData *air=RTXCopyNativeFunctionAIR055(device,function,error);if(!air)return nil;
 RTXApplication209_RTXBackendCompiler056 *compiler=nil;@synchronized(device){compiler=[objc_getAssociatedObject(device,&compilerKey)retain];}
 if(!compiler){[air release];if(error)*error=failure(3,@"Runtime backend compiler is not configured");return nil;}
 NSData *result=nil;@try{result=[compiler copyLibrary:air name:function.name error:error];}@finally{[air release];[compiler release];}return result;
}
extern "C" NSDictionary *RTXCopyBackendCompilerInfo056(void){return [@{@"created":@(created.load()),@"destroyed":@(destroyed.load()),@"requests":@(requests.load()),@"successes":@(successes.load()),@"gpu_uploads":@0}copy];}
