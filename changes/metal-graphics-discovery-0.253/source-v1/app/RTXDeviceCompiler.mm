#import "RTXDeviceCompiler.h"
#include <atomic>
#include <cstring>

namespace {
std::atomic<uint64_t> profiles{0},processLimits{0};
// The native source frontend selects its LLVM version using this unassigned
// profile. This does not declare an Apple/AMD GPU family or a supported feature
// profile for system registration. RTX's feature queries remain empty.
NSUInteger compilerProfile(id,SEL){++profiles;return NSUIntegerMax;}
// Zero makes the inherited scheduler resize its process vector to zero before
// accessing slot zero. Four is the tested bounded native frontend pool limit.
int maximumCompilerProcesses(id,SEL){++processLimits;return 4;}
struct Entry{const char *name;IMP imp;const char *encoding;bool inherited;};
const Entry entries[]={
 {"featureProfile",reinterpret_cast<IMP>(compilerProfile),"Q16@0:8",false},
 {"maximumCompilerProcessesCount",reinterpret_cast<IMP>(maximumCompilerProcesses),"i16@0:8",true},
};
}
unsigned RTXDeviceCompilerMethodCount055(){return sizeof(entries)/sizeof(entries[0]);}
BOOL RTXInstallDeviceCompiler055(Class cls){
 Class base=objc_getClass("_MTLDevice");
 if(!cls||!base||objc_getClass(class_getName(cls))||class_getSuperclass(cls)!=base||class_getInstanceSize(base)!=712)return NO;
 Method source=class_getInstanceMethod(base,sel_registerName("newLibraryWithSource:options:error:"));
 if(!source||std::strcmp(method_getTypeEncoding(source),"@40@0:8@16@24^@32"))return NO;
 for(const auto &e:entries){Method m=class_getInstanceMethod(base,sel_registerName(e.name));if(e.inherited?(!m||std::strcmp(method_getTypeEncoding(m),e.encoding)):m!=nullptr)return NO;}
 for(const auto &e:entries)if(!class_addMethod(cls,sel_registerName(e.name),e.imp,e.encoding))return NO;
 return YES;
}
extern "C" NSDictionary *RTXCopyDeviceCompilerInfo055(){
 return [@{@"profile_queries":@(profiles.load()),@"process_limit_queries":@(processLimits.load()),@"maximum_processes":@4,@"profile":@(NSUIntegerMax),@"hardware_profile_assigned":@NO,@"system_metal_registered":@NO}copy];
}

BOOL RTXDeviceCompilerOwnsDevice055(id device){
 if(!device||class_getInstanceSize(object_getClass(device))!=712||class_getSuperclass(object_getClass(device))!=objc_getClass("_MTLDevice"))return NO;
 Method a=class_getInstanceMethod(object_getClass(device),sel_registerName("featureProfile"));
 Method b=class_getInstanceMethod(object_getClass(device),sel_registerName("maximumCompilerProcessesCount"));
 return a&&b&&method_getImplementation(a)==reinterpret_cast<IMP>(compilerProfile)&&method_getImplementation(b)==reinterpret_cast<IMP>(maximumCompilerProcesses);
}
