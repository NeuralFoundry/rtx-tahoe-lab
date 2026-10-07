#import "RTXDeviceFeatures.h"
#import "RTXDeviceLimits.h"
#import <objc/message.h>
#import <objc/runtime.h>
#include "RTXDeviceFeaturesLayout.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>

namespace {
Class queryBase,queryClass;
std::atomic<uint64_t> created{0},destroyed{0};
void initializeFeatures(id,SEL);
bool validQuery(id query){
 if(!query || object_getClass(query)!=queryClass)return false;
 const auto *bytes=reinterpret_cast<const unsigned char *>(query)+8;
 for(size_t i=0;i<RTXFeatures039::FamilyBytes;++i)if(bytes[i])return false;
 return true;
}
id queryInitialize(id query,SEL selector){
 struct objc_super super={query,queryBase};
 query=reinterpret_cast<id(*)(struct objc_super *,SEL)>(objc_msgSendSuper)(&super,selector);
 if(query)++created;return query;
}
void queryDeallocate(id query,SEL selector){
 ++destroyed;struct objc_super super={query,queryBase};
 reinterpret_cast<void(*)(struct objc_super *,SEL)>(objc_msgSendSuper)(&super,selector);
}
void queryValidate(id query,SEL){if(!validQuery(query))[NSException raise:NSInternalInconsistencyException format:@"Invalid RTX family feature state"];}
bool makeQueryClass(){
 static std::once_flag once;
 std::call_once(once,[]{
  queryBase=objc_getClass("MTLDeviceFeatureQueries");if(!queryBase)return;
  Class cls=objc_allocateClassPair(queryBase,"RTXDeviceFeatureQueries039",0);if(!cls)return;
  if(!class_addMethod(cls,sel_registerName("init"),reinterpret_cast<IMP>(queryInitialize),"@16@0:8") ||
     !class_addMethod(cls,sel_registerName("dealloc"),reinterpret_cast<IMP>(queryDeallocate),"v16@0:8") ||
     !class_addMethod(cls,sel_registerName("validate"),reinterpret_cast<IMP>(queryValidate),"v16@0:8")){objc_disposeClassPair(cls);return;}
  objc_registerClassPair(cls);queryClass=cls;
 });return queryClass!=Nil;
}
BOOL ownDevice(id device){
 if(!device || class_getInstanceSize(object_getClass(device))!=712)return NO;
 Method m=class_getInstanceMethod(object_getClass(device),sel_registerName("initFeatureQueries"));
 return m && method_getImplementation(m)==reinterpret_cast<IMP>(initializeFeatures);
}
id readQuery(id device){id query=nil;std::memcpy(&query,reinterpret_cast<unsigned char *>(device)+RTXFeatures039::QuerySlot,sizeof(query));return query;}
void writeQuery(id device,id query){std::memcpy(reinterpret_cast<unsigned char *>(device)+RTXFeatures039::QuerySlot,&query,sizeof(query));}
bool emptyFamilies(id device){
 std::array<uintptr_t,3> vector{};std::memcpy(vector.data(),reinterpret_cast<unsigned char *>(device)+RTXFeatures039::FamilyVector,sizeof(vector));
 return !vector[0]&&!vector[1]&&!vector[2];
}
void initializeFamilies(id device,SEL){
 if(!ownDevice(device)||!RTXDeviceFeaturesRuntimeCompatible()||!emptyFamilies(device))
  [NSException raise:NSInternalInconsistencyException format:@"Unexpected RTX GPU family storage"];
 // No complete GPU family is implemented. Keep the base vector empty.
}
void initializeFeatures(id device,SEL){
 if(!RTXInitializeDeviceFeatures(device))
  [NSException raise:NSInternalInconsistencyException format:@"RTX feature query initialization failed"];
}
}
BOOL RTXDeviceFeaturesRuntimeCompatible(){
 if(!RTXDeviceLimitsRuntimeCompatible())return NO;
 Class base=objc_getClass("_MTLDevice"),features=objc_getClass("MTLDeviceFeatureQueries");
 if(!features||class_getInstanceSize(features)!=RTXFeatures039::QueryBytes)return NO;
 Ivar slot=class_getInstanceVariable(base,"_featureQueries"),families=class_getInstanceVariable(base,"_supportedGPUFamilies");
 if(!slot||!families||ivar_getOffset(slot)!=680||std::strcmp(ivar_getTypeEncoding(slot),"@\"MTLDeviceFeatureQueries\"")||ivar_getOffset(families)!=648)return NO;
 unsigned count=0;Ivar *ivars=class_copyIvarList(features,&count);bool ok=count==RTXFeatures039::FamilyBytes;
 for(unsigned i=0;i<count&&ok;++i)ok=std::strcmp(ivar_getName(ivars[i]),RTXFeatures039::FamilyIvars[i])==0&&ivar_getOffset(ivars[i])==8+i&&std::strcmp(ivar_getTypeEncoding(ivars[i]),"c")==0;
 free(ivars);return ok;
}
unsigned RTXDeviceFeatureMethodCount(){return 2;}
BOOL RTXInstallDeviceFeatures(Class cls){
 if(!cls||objc_getClass(class_getName(cls))||class_getSuperclass(cls)!=objc_getClass("_MTLDevice")||!RTXDeviceFeaturesRuntimeCompatible()||!makeQueryClass())return NO;
 return class_addMethod(cls,sel_registerName("initGPUFamilySupport"),reinterpret_cast<IMP>(initializeFamilies),"v16@0:8")&&
  class_addMethod(cls,sel_registerName("initFeatureQueries"),reinterpret_cast<IMP>(initializeFeatures),"v16@0:8");
}
BOOL RTXInitializeDeviceFeatures(id device){
 if(!ownDevice(device)||!RTXDeviceFeaturesRuntimeCompatible())return NO;
 @synchronized(device){
  if(!RTXInitializeDeviceLimits(device)||!emptyFamilies(device))return NO;
  id current=readQuery(device);if(current)return validQuery(current);
  // The measured base +allocWithZone: delegates to NSObject for a subclass.
  // Our init uses NSObject's inherited initialization, never the feature-profile
  // class-cluster initializer. All 231 family flags start false by allocation.
  id query=[[queryClass alloc]init];if(!validQuery(query)){[query release];return NO;}
  writeQuery(device,query); // transfer the +1 reference into our inherited slot
 }
 return YES;
}
id RTXCopyDeviceFeatureQueries(id device){
 if(!ownDevice(device)||!RTXDeviceFeaturesRuntimeCompatible())return nil;
 @synchronized(device){id query=readQuery(device);return validQuery(query)?[query retain]:nil;}
}
void RTXCloseDeviceFeatures(id device){
 if(!ownDevice(device)||!RTXDeviceFeaturesRuntimeCompatible())return;
 id released=nil;
 @synchronized(device){id query=readQuery(device);if(query&&object_getClass(query)==queryClass){released=query;writeQuery(device,nil);}}
 // Clear the inherited slot before release, so base teardown cannot release
 // this reference twice. Destruction runs outside the device lock.
 [released release];
}
NSDictionary *RTXCopyDeviceFeatureInfo(){return [@{@"created":@(created.load()),@"destroyed":@(destroyed.load()),@"family_fields":@(RTXFeatures039::FamilyBytes),@"query_class":queryClass?NSStringFromClass(queryClass):@"",@"query_bytes":@(queryClass?class_getInstanceSize(queryClass):0)}copy];}
