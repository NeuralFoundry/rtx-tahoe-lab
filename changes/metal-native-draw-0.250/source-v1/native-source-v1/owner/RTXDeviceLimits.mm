#import "RTXDeviceLimits.h"
#import <objc/runtime.h>
#include "RTXDeviceLimitsLayout.hpp"
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
char initializedKey;
void initLimits(id device,SEL);
BOOL ownsLimits(id device){
 if(!device || class_getInstanceSize(object_getClass(device))!=712)return NO;
 Method m=class_getInstanceMethod(object_getClass(device),sel_registerName("initLimits"));
 return m && method_getImplementation(m)==reinterpret_cast<IMP>(initLimits);
}
void initLimits(id device,SEL){
 if(!RTXInitializeDeviceLimits(device))
  [NSException raise:NSInternalInconsistencyException format:@"Unsupported RTX Metal limit layout"];
}
BOOL noFamily(id,SEL,NSInteger){return NO;}
BOOL noFeatureSet(id,SEL,NSUInteger){return NO;}
BOOL noSampleCount(id,SEL,NSUInteger){return NO;}
NSUInteger maxBuffer(id,SEL){return RTXSoftware039::MaxBuffer;}
NSUInteger noThreadgroupMemory(id,SEL){return 0;}
MTLSize maxThreads(id,SEL){return MTLSizeMake(1024,1024,64);} // ABI2 geometry164; total is capped at 1024.
struct Entry{const char *name;IMP imp;const char *encoding;};
const Entry entries[]={
 {"initLimits",reinterpret_cast<IMP>(initLimits),"v16@0:8"},
 {"supportsFamily:",reinterpret_cast<IMP>(noFamily),"c24@0:8q16"},
 {"supportsFeatureSet:",reinterpret_cast<IMP>(noFeatureSet),"c24@0:8Q16"},
 {"supportsTextureSampleCount:",reinterpret_cast<IMP>(noSampleCount),"c24@0:8Q16"},
 {"maxBufferLength",reinterpret_cast<IMP>(maxBuffer),"Q16@0:8"},
 {"maxThreadgroupMemoryLength",reinterpret_cast<IMP>(noThreadgroupMemory),"Q16@0:8"},
 {"maxThreadsPerThreadgroup",reinterpret_cast<IMP>(maxThreads),nullptr}
};
}
unsigned RTXDeviceLimitMethodCount(){return sizeof(entries)/sizeof(entries[0]);}
BOOL RTXDeviceLimitsRuntimeCompatible(){
 Class base=objc_getClass("_MTLDevice");if(!base)return NO;
 Ivar limits=class_getInstanceVariable(base,"_limits"),next=class_getInstanceVariable(base,"_serialQueue");
 return limits && next && RTXLimits039::layoutMatches(ivar_getOffset(limits),ivar_getOffset(next),class_getInstanceSize(base),ivar_getTypeEncoding(limits));
}
BOOL RTXInstallDeviceLimits(Class cls){
 if(!cls || class_getSuperclass(cls)!=objc_getClass("_MTLDevice") ||
    objc_getClass(class_getName(cls)) || !RTXDeviceLimitsRuntimeCompatible())return NO;
 // A caller must dispose the unpublished class if any installer fails.
 const std::string sizeEncoding=std::string(@encode(MTLSize))+"16@0:8";
 for(const auto &e:entries)if(!class_addMethod(cls,sel_registerName(e.name),e.imp,e.encoding?e.encoding:sizeEncoding.c_str()))return NO;
 return YES;
}
BOOL RTXInitializeDeviceLimits(id device){
 if(!ownsLimits(device)||!RTXDeviceLimitsRuntimeCompatible())return NO;
 @synchronized(device){
  if(objc_getAssociatedObject(device,&initializedKey))return YES;
  auto value=RTXLimits039::supported();
  // The validated inherited slot belongs to our newly constructed object.
  // No class metadata, framework code, AMD device or process-global table is changed.
  auto *slot=reinterpret_cast<unsigned char *>(device)+8;
  std::memcpy(slot,&value,sizeof(value));
  objc_setAssociatedObject(device,&initializedKey,@YES,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
 }
 return YES;
}
NSData *RTXCopyDeviceLimits(id device){
 if(!ownsLimits(device)||!RTXDeviceLimitsRuntimeCompatible())return nil;
 @synchronized(device){
  if(!objc_getAssociatedObject(device,&initializedKey))return nil;
  return [[NSData alloc]initWithBytes:reinterpret_cast<unsigned char *>(device)+8 length:sizeof(RTXLimits039::Limits)];
 }
}
