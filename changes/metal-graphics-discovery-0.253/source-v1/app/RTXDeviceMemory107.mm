#import "RTXDeviceMemory107.h"
#import <objc/runtime.h>
#include <unistd.h>
namespace {char memoryKey;const pid_t process=getpid();}
extern "C" BOOL RTXAttachDeviceMemory107(id device,const RTXMemory107::Evidence &e,uint64_t generation){
 if(getpid()!=process||!device||object_getClass(device)!=objc_getClass("RTXMetalApplicationDevice209")||!generation||e.generation!=generation)return NO;
 unsigned char bytes[RTXMemory107::Bytes]={};
 if(!RTXMemory107::encode(e,bytes,sizeof(bytes)))return NO;
 NSData *candidate=[NSData dataWithBytes:bytes length:sizeof(bytes)];if(!candidate)return NO;
 @synchronized(device){
  NSData *old=objc_getAssociatedObject(device,&memoryKey);
  if(old)return [old isEqualToData:candidate];
  objc_setAssociatedObject(device,&memoryKey,candidate,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  return objc_getAssociatedObject(device,&memoryKey)==candidate;
 }
}
uint64_t RTXDedicatedMemory107(id device,SEL){
 if(getpid()!=process||!device)return 0;
 @synchronized(device){
  NSData *data=objc_getAssociatedObject(device,&memoryKey);RTXMemory107::Evidence e;
  // Explicit CPU/legacy factories without a native binding have no measured
  // capacity. Zero means unavailable here; it is not a physical zero-VRAM claim.
  if(!data||data.length!=RTXMemory107::Bytes||!RTXMemory107::decode(static_cast<const unsigned char *>(data.bytes),RTXMemory107::Bytes,e))return 0;
  return e.reportedBytes;
 }
}
