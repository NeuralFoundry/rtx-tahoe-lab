#import "RTXApplicationRuntime115.h"
#import "RTXApplicationClient.h"
#import "RTXAcceleratorIdentity103.h"
#import <objc/runtime.h>
#include "ApplicationAdmission253.hpp"
#include <CommonCrypto/CommonDigest.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <climits>
#include <cstdlib>
#include <cstring>
namespace {
char runtimeKey;const pid_t process=getpid();
NSError *failure(NSInteger code,NSString *message){return [NSError errorWithDomain:@"RTXGraphicsRuntime253" code:code userInfo:@{NSLocalizedDescriptionKey:message}];}
BOOL fail(NSError **error,NSInteger code,NSString *message){if(error)*error=failure(code,message);return NO;}
bool text(id a,id b){return [a isKindOfClass:NSString.class]&&[b isKindOfClass:NSString.class]&&[a isEqualToString:b];}
bool integer(id value,uint64_t&out){
 if(![value isKindOfClass:NSNumber.class]||CFGetTypeID((CFTypeRef)value)!=CFNumberGetTypeID()||CFNumberIsFloatType((CFNumberRef)value))return false;
 int64_t v=0;if(!CFNumberGetValue((CFNumberRef)value,kCFNumberSInt64Type,&v)||v<0)return false;out=uint64_t(v);return true;
}
bool number(id value,uint64_t expected){uint64_t v=0;return integer(value,v)&&v==expected;}
bool counter(id value,uint64_t&out){
 if(![value isKindOfClass:NSNumber.class]||CFGetTypeID((CFTypeRef)value)!=CFNumberGetTypeID()||CFNumberIsFloatType((CFNumberRef)value))return false;
 const char*t=[value objCType];if(t&&t[0]=='Q'&&!t[1]){out=[value unsignedLongLongValue];return true;}return integer(value,out);
}
bool absolute(NSString*p){char resolved[PATH_MAX];return [p isKindOfClass:NSString.class]&&p.isAbsolutePath&&realpath(p.fileSystemRepresentation,resolved)&&!std::strcmp(p.fileSystemRepresentation,resolved);}
NSString *digest(NSData*data){if(![data isKindOfClass:NSData.class]||data.length>UINT32_MAX)return nil;unsigned char out[CC_SHA256_DIGEST_LENGTH];CC_SHA256(data.bytes,CC_LONG(data.length),out);NSMutableString*s=[NSMutableString string];for(auto b:out)[s appendFormat:@"%02x",b];return s;}
bool sha(id value){if(![value isKindOfClass:NSString.class]||[value length]!=64)return false;for(NSUInteger i=0;i<64;++i){unichar c=[value characterAtIndex:i];if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;}return true;}
NSString *boot(){char value[96]={};size_t n=sizeof(value);if(sysctlbyname("kern.bootsessionuuid",value,&n,nullptr,0)||!n||n>sizeof(value)||value[n-1]||std::strlen(value)!=36)return nil;return [NSString stringWithUTF8String:value];}
bool specValid(NSDictionary*s){
 return [s isKindOfClass:NSDictionary.class]&&s.count==5&&number(s[@"abi"],253)&&text(s[@"service"],@(RTXAccelerator103::BrokerService))&&text(s[@"application_version"],@"0.253.0")&&absolute(s[@"ready_path"])&&sha(s[@"container_sha256"]);
}
bool sameFile(const struct stat&a,const struct stat&b){return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size&&a.st_uid==b.st_uid&&a.st_mode==b.st_mode&&a.st_mtimespec.tv_sec==b.st_mtimespec.tv_sec&&a.st_mtimespec.tv_nsec==b.st_mtimespec.tv_nsec&&a.st_ctimespec.tv_sec==b.st_ctimespec.tv_sec&&a.st_ctimespec.tv_nsec==b.st_ctimespec.tv_nsec;}
NSString *memory(const RTXAccelerator103::Binding&b){unsigned char raw[RTXMemory107::Bytes];if(!RTXMemory107::encode(b.memory,raw,sizeof(raw)))return nil;return [[NSData dataWithBytes:raw length:sizeof(raw)]base64EncodedStringWithOptions:0];}
NSDictionary *decode(NSData*raw,NSDictionary*s,const RTXAccelerator103::Binding&binding,uint64_t pid,RTXApplication253::Plan*admission,NSError**error){
 if(error)*error=nil;
 if(![raw isKindOfClass:NSData.class]||!raw.length||raw.length>65536||!specValid(s)){fail(error,5,@"Invalid owned runtime inputs");return nil;}
 id object=[NSJSONSerialization JSONObjectWithData:raw options:0 error:error];if(![object isKindOfClass:NSDictionary.class]){fail(error,6,@"Readiness must be an object");return nil;}
 NSDictionary*r=object;id candidate=r[@"port_binding"];
 if(r.count!=9||![candidate isKindOfClass:NSDictionary.class]||[candidate count]!=15||!text(r[@"service"],s[@"service"])||!number(r[@"abi"],253)||!number(r[@"protocol"],251)) {fail(error,7,@"Owned runtime configuration differs");return nil;}
 NSDictionary*p=candidate;RTXApplication253::Ready ready;ready.abi=253;ready.protocol=251;
 if(!integer(r[@"allowed_uid"],ready.allowedUID)||!integer(r[@"generation"],ready.generation)||!integer(r[@"root_pid"],ready.rootPID)||
  !integer(p[@"child_registry"],ready.child)||!integer(p[@"program_epoch"],ready.epoch)||!integer(p[@"publication_session"],ready.publicationSession)||
  !integer(p[@"root_abi"],ready.rootABI)||!integer(p[@"host_buffer_abi"],ready.hostABI)||!integer(p[@"owned_data_abi"],ready.dataABI)||!integer(p[@"owned_dispatch_abi"],ready.dispatchABI)||!integer(p[@"publication_abi"],ready.publicationABI)||!integer(p[@"owned_graphics_abi"],ready.graphicsABI)||
  !number(p[@"abi"],RTXRootReady200::PortBindingABI)||p[@"owned_root_verified"]!=(id)kCFBooleanTrue||!text(p[@"probe_version"],@(RTXAccelerator103::ParentVersion))||!text(p[@"accelerator_version"],@(RTXAccelerator103::ChildVersion))||!text(p[@"application_version"],s[@"application_version"])){fail(error,8,@"Owned runtime identity or ABI mismatch");return nil;}
 NSString*bootUUID=boot();ready.bootMatches=text(r[@"boot_uuid"],bootUUID);ready.catalogMatches=text(r[@"container_sha256"],s[@"container_sha256"]);ready.memoryMatches=text(p[@"memory_base64"],memory(binding));
 RTXApplication253::Plan plan;if(!RTXApplication253::prepare(binding,ready,{pid,uint64_t(geteuid())},plan)){fail(error,9,@"Owned readiness does not match this live publication");return nil;}
 if(admission)*admission=plan;
 return @{@"abi":@253,@"spec":[[s copy]autorelease],@"generation":@(binding.parentGeneration),@"child_registry":@(binding.childRegistry),@"root_pid":@(ready.rootPID),@"client_pid":@(pid),@"epoch":@(binding.epoch),@"publication_session":@(binding.session),@"memory_base64":memory(binding),@"ready_sha256":digest(raw),@"boot_uuid":bootUUID};
}
}
extern "C" NSData *RTXReadApplicationRuntimeFile115(NSString*path,NSUInteger maximum,NSError**error){
 if(error)*error=nil;if(!absolute(path)||!maximum||maximum>65536){fail(error,1,@"Invalid canonical runtime path or bound");return nil;}
 struct stat parent{},before{},after{};
 if(lstat(path.stringByDeletingLastPathComponent.fileSystemRepresentation,&parent)||!S_ISDIR(parent.st_mode)||parent.st_uid||(parent.st_mode&0022)){fail(error,2,@"Runtime record directory is not protected");return nil;}
 int fd=open(path.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(fd<0){fail(error,3,@"Runtime readiness unavailable");return nil;}NSData*result=nil;
 if(!fstat(fd,&before)&&S_ISREG(before.st_mode)&&!before.st_uid&&!(before.st_mode&0022)&&before.st_size>0&&uint64_t(before.st_size)<=maximum){
  NSMutableData*raw=[NSMutableData dataWithLength:NSUInteger(before.st_size)];size_t offset=0;
  while(offset<raw.length){ssize_t n=read(fd,static_cast<unsigned char*>(raw.mutableBytes)+offset,raw.length-offset);if(n<=0)break;offset+=size_t(n);}
  if(offset==raw.length&&!fstat(fd,&after)&&sameFile(before,after))result=[[raw copy]autorelease];
 }
 close(fd);if(!result)fail(error,4,@"Runtime record changed while reading");return result;
}
extern "C" NSDictionary *RTXDecodeApplicationRuntime115(NSData*raw,NSDictionary*spec,const RTXAccelerator103::Binding&binding,uint64_t pid,NSError**error){return decode(raw,spec,binding,pid,nullptr,error);}
extern "C" NSDictionary *RTXPrepareApplicationRuntime115(id device,const RTXAccelerator103::Binding&binding,NSError**error){
 if(error)*error=nil;if(getpid()!=process||geteuid()!=501||!device||object_getClass(device)!=objc_getClass("RTXMetalApplicationDevice209")){fail(error,10,@"Runtime requires this ordinary application device");return nil;}
 NSBundle*bundle=[NSBundle bundleForClass:object_getClass(device)];NSData*config=[NSData dataWithContentsOfFile:[bundle pathForResource:@"runtime-config253" ofType:@"json"]];
 id spec=config?[NSJSONSerialization JSONObjectWithData:config options:0 error:error]:nil;
 if(!specValid(spec)){fail(error,11,@"Application runtime configuration unavailable");return nil;}
 NSData*container=[NSData dataWithContentsOfFile:[bundle pathForResource:@"selected" ofType:@"rtxlib"]];
 if(!text(digest(container),spec[@"container_sha256"])){fail(error,12,@"Selected application catalog differs");return nil;}
 NSData*raw=RTXReadApplicationRuntimeFile115(spec[@"ready_path"],65536,error);return raw?decode(raw,spec,binding,uint64_t(getpid()),nullptr,error):nil;
}
extern "C" BOOL RTXActivateApplicationRuntime115(id<MTLDevice>device,NSDictionary*plan,uint32_t port,NSError**error){
 if(error)*error=nil;if(getpid()!=process||geteuid()!=501||!device||object_getClass(device)!=objc_getClass("RTXMetalApplicationDevice209")||![plan isKindOfClass:NSDictionary.class]||plan.count!=11||!number(plan[@"abi"],253)||!specValid(plan[@"spec"]))return fail(error,13,@"Invalid application activation plan");
 RTXAccelerator103::Binding current;if(!RTXReadAcceleratorBinding103(port,current))return fail(error,14,@"Metal publication changed during the handshake");
 NSDictionary*s=plan[@"spec"];NSData*raw=RTXReadApplicationRuntimeFile115(s[@"ready_path"],65536,error);if(!raw)return NO;
 RTXApplication253::Plan admitted;NSDictionary*rechecked=decode(raw,s,current,uint64_t(getpid()),&admitted,error);
 if(!rechecked||![rechecked isEqualToDictionary:plan])return fail(error,15,@"Owned readiness changed during the handshake");
 NSDictionary*info=RTXCopyApplicationDeviceInfo(device);RTXApplication253::Peer peer;
 bool typed=integer(info[@"phase"],peer.phase)&&integer(info[@"generation"],peer.generation)&&counter(info[@"session"],peer.session)&&counter(info[@"completed"],peer.completed)&&counter(info[@"native_serial"],peer.nativeSerial)&&integer(info[@"server_pid"],peer.serverPID)&&integer(info[@"process_id"],peer.processID)&&integer(info[@"uid"],peer.uid)&&counter(info[@"channel_exchanges"],peer.exchanges);[info release];
 if(!typed||!RTXApplication253::connect(admitted,peer,{uint64_t(getpid()),uint64_t(geteuid())},current,true))return fail(error,16,@"Owned broker peer differs from the published owner");
 @synchronized(device){
  if(objc_getAssociatedObject(device,&runtimeKey))return fail(error,19,@"Runtime already attached");
  NSMutableDictionary*receipt=[NSMutableDictionary dictionaryWithDictionary:plan];receipt[@"automatic_configuration"]=@YES;receipt[@"graphics_only"]=@YES;receipt[@"peer_session"]=@(peer.session);receipt[@"native_serial_floor"]=@(peer.nativeSerial);
  objc_setAssociatedObject(device,&runtimeKey,receipt,OBJC_ASSOCIATION_COPY_NONATOMIC);return YES;
 }
}
extern "C" NSDictionary *RTXCopyApplicationRuntimeInfo115(id<MTLDevice>device){@synchronized(device){return [objc_getAssociatedObject(device,&runtimeKey)copy];}}
