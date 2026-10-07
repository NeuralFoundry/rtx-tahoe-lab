#import "RTXAcceleratorIdentity103.h"
#include <cstdio>
#include <cstdlib>
using namespace RTXAccelerator103;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::abort();}}while(0)
static Node decode(NSDictionary*p){Node n;RTXDecodeAcceleratorProperties103(p,n);return n;}
static bool live(NSDictionary*p,uint64_t generation){auto n=decode(p);n.registry=generation;n.isProbe=true;return liveParent(n);}
static bool published(NSDictionary*p){auto n=decode(p);n.registry=101;n.isChild=n.isAccelerator=true;return publishedChild(n);}
int main(int argc,char**argv){@autoreleasepool {
 CHECK(argc==2);NSData *data=[NSData dataWithContentsOfFile:@(argv[1])];CHECK(data!=nil);
 id raw=[NSPropertyListSerialization propertyListWithData:data options:NSPropertyListMutableContainersAndLeaves format:nullptr error:nil];
 CHECK([raw isKindOfClass:[NSArray class]]&&[raw count]==1);
 NSDictionary *captured=raw[0];CHECK([captured isKindOfClass:[NSDictionary class]]);
 uint64_t generation=[captured[@"IORegistryEntryID"] unsignedLongLongValue];
 auto prior=decode(captured);CHECK(prior.parentVersion&&prior.validMemory&&prior.memory.generation==generation);
 // Labelled CPU derivative of capture199, with 0.83.1 version/ABI fields.
 // This fixture does not claim a loaded or armed GPU.
 CHECK(!prior.root.dispatchActive&&!live(captured,generation));
 NSMutableDictionary *parent=[captured mutableCopy];parent[@"OwnedDispatchActive"]=@YES;parent[@"OwnedGraphicsActive"]=@YES;
 CHECK(live(parent,generation));
 for(NSString *key in @[@"OwnedRootABI",@"HostBufferABI",@"OwnedDataABI",@"OwnedDispatchABI",@"OwnedGraphicsABI",@"GSPResidentProgramEpoch"]){
  for(id bad in @[@NO,@YES,@0,@(-1),@1.0,@"195",[NSNull null]]){
   NSMutableDictionary*p=[parent mutableCopy];p[key]=bad;CHECK(!live(p,generation));[p release];
  }
  NSMutableDictionary*p=[parent mutableCopy];[p removeObjectForKey:key];CHECK(!live(p,generation));[p release];
 }
 for(NSString *key in @[@"OwnedRootAcknowledged",@"OwnedRootRuntimeEnabled",@"OwnedDispatchActive",@"OwnedGraphicsActive",@"GSPDmaProviderOpen",@"GSPProgramReady",@"GSPHostFencePassed",@"GSPInitDoneObserved"]){
  for(id bad in @[@NO,@0,@1,@1.0,@"true",[NSNull null]]){
   NSMutableDictionary*p=[parent mutableCopy];p[key]=bad;CHECK(!live(p,generation));[p release];
  }
  NSMutableDictionary*p=[parent mutableCopy];[p removeObjectForKey:key];CHECK(!live(p,generation));[p release];
 }
 for(id bad in @[@YES,@0,@1,@0.0,@"false",[NSNull null]]){NSMutableDictionary*p=[parent mutableCopy];p[@"OwnedGraphicsRetained"]=bad;CHECK(!live(p,generation));[p release];}
 {NSMutableDictionary*p=[parent mutableCopy];[p removeObjectForKey:@"OwnedGraphicsRetained"];CHECK(!live(p,generation));[p release];}
 NSDictionary *child=@{@"RTXMetalAcceleratorVersion":@(ChildVersion),@"RTXMetalParentProbeVersion":@(ParentVersion),@"RTXMetalParentRegistryID":@(generation),@"RTXMetalPublicationABI":@3,@"RTXMetalPublicationEpoch":@1,@"RTXMetalPublicationSession":@9,@"RTXMetalGPUReady":@YES,@"MetalPluginName":@(RTXPublication200::PluginName),@"MetalPluginClassName":@(RTXPublication200::PluginClass)};
 CHECK(published(child));
 for(NSString *key in @[@"RTXMetalParentRegistryID",@"RTXMetalPublicationABI",@"RTXMetalPublicationEpoch",@"RTXMetalPublicationSession"]){
  for(id bad in @[@NO,@YES,@0,@(-1),@1.0,@"9",[NSNull null]]){
   NSMutableDictionary*p=[child mutableCopy];p[key]=bad;CHECK(!published(p));[p release];
  }
  NSMutableDictionary*p=[child mutableCopy];[p removeObjectForKey:key];CHECK(!published(p));[p release];
 }
 for(NSString *key in @[@"RTXMetalAcceleratorVersion",@"RTXMetalParentProbeVersion",@"MetalPluginName",@"MetalPluginClassName"]){
  for(id bad in @[@"",@"0.80.0",@"0.209.0",@"0.172.0",@YES,@1,[NSNull null]]){
   NSMutableDictionary*p=[child mutableCopy];p[key]=bad;CHECK(!published(p));[p release];
  }
 }
 for(id bad in @[@NO,@1,@"true",[NSNull null]]){NSMutableDictionary*p=[child mutableCopy];p[@"RTXMetalGPUReady"]=bad;CHECK(!published(p));[p release];}
 CHECK(!published(@{})&&!live(nil,generation));
 [parent release];
 std::printf("{\"passed\":true,\"checks\":%u,\"actual_framework_types\":true,\"actual_iokit\":false,\"gpu_jobs\":0}\n",checks);
}}
