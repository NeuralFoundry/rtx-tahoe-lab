#import "RTXAcceleratorIdentity103.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
// Explicit controller primitive. The owner must establish its bundle, XPC
// endpoint and generation-bound ready record BEFORE invoking publish; withdraw
// must complete BEFORE draining that endpoint or closing the native owner.
// This utility never opens a user client, sends a GPU command or claims Metal
// enumeration. It reports only the actual property call and subsequent binding.
static bool number(const char*s,uint64_t&v){
 if(!s||!*s)return false;for(const char*p=s;*p;++p)if(*p<'0'||*p>'9')return false;
 errno=0;char*end=nullptr;v=std::strtoull(s,&end,10);return !errno&&end&&!*end&&v&&v<=INT64_MAX;
}
int main(int argc,char**argv){@autoreleasepool {
 using namespace RTXPublication200;
 if(argc!=6||geteuid()!=0)return 2;
 Request request;
 if(!std::strcmp(argv[1],"publish"))request.operation=Operation::Publish;
 else if(!std::strcmp(argv[1],"withdraw"))request.operation=Operation::Withdraw;
 else return 2;
 if(!number(argv[2],request.child)||!number(argv[3],request.parent)||!number(argv[4],request.epoch)||!number(argv[5],request.session))return 2;
 uint8_t raw[Bytes];if(!encode(request,raw,Bytes))return 2;
 io_iterator_t iterator=IO_OBJECT_NULL;
 if(IOServiceGetMatchingServices(kIOMasterPortDefault,IOServiceMatching(RTXAccelerator103::ChildClass),&iterator)!=KERN_SUCCESS)return 3;
 io_service_t selected=IO_OBJECT_NULL,entry=IO_OBJECT_NULL;unsigned matches=0;
 while((entry=IOIteratorNext(iterator))){uint64_t id=0;
  if(IORegistryEntryGetRegistryEntryID(entry,&id)==KERN_SUCCESS&&id==request.child){++matches;if(!selected){selected=entry;continue;}}
  IOObjectRelease(entry);
 }
 IOObjectRelease(iterator);
 if(matches!=1||!selected){if(selected)IOObjectRelease(selected);return 3;}
 io_registry_entry_t parent=IO_OBJECT_NULL;uint64_t generation=0;
 bool identity=IORegistryEntryGetParentEntry(selected,kIOServicePlane,&parent)==KERN_SUCCESS&&parent&&IORegistryEntryGetRegistryEntryID(parent,&generation)==KERN_SUCCESS&&generation==request.parent;
 if(parent)IOObjectRelease(parent);if(!identity){IOObjectRelease(selected);return 3;}
 NSDictionary *payload=@{@(RequestProperty):[NSData dataWithBytes:raw length:Bytes]};
 kern_return_t status=IORegistryEntrySetCFProperties(selected,(CFTypeRef)payload);
 bool verified=false;RTXAccelerator103::Binding binding;
 if(status==KERN_SUCCESS){
  if(request.operation==Operation::Publish)verified=RTXReadAcceleratorBinding103(selected,binding)&&binding.childRegistry==request.child&&binding.parentGeneration==request.parent&&binding.epoch==request.epoch&&binding.session==request.session;
  else {
   CFMutableDictionaryRef values=nullptr;
   if(IORegistryEntryCreateCFProperties(selected,&values,kCFAllocatorDefault,0)==KERN_SUCCESS&&values){
    NSDictionary *p=(NSDictionary *)values;
    verified=!p[@"RTXMetalGPUReady"]&&!p[@"MetalPluginName"]&&!p[@"MetalPluginClassName"]&&!p[@"RTXMetalPublicationEpoch"]&&!p[@"RTXMetalPublicationSession"];
   }
   if(values)CFRelease(values);
  }
 }
 IOObjectRelease(selected);
 NSDictionary *result=@{@"operation":@(argv[1]),@"child":@(request.child),@"parent":@(request.parent),@"epoch":@(request.epoch),@"session":@(request.session),@"property_returncode":@(status),@"verified":@(verified),@"actual_iokit":@YES,@"gpu_jobs":@0,@"standard_metal_enumeration":@NO};
 NSData *json=[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:nil];if(!json)return 4;
 std::fwrite(json.bytes,1,json.length,stdout);std::fputc('\n',stdout);return status==KERN_SUCCESS&&verified?0:1;
}}
