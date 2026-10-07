#import "owner/RTXNativeOwnedBroker188.h"
#include "owner/OwnedBroker208.hpp"
#include "owner/Lifecycle202.hpp"
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>
namespace O=RTXOwnedBroker208;namespace B=RTXBatch187;
static NSData*image=nil;static NSString*folder=nil;static std::string mode;
static bool visible=false,withdrawn=false,initialized=false;static uint64_t completed=0;static unsigned claims=0,retires=0,destroyed=0;
static std::vector<std::string>events;
static void require(bool yes){if(!yes)throw std::runtime_error("CPU model208 invariant");}
@interface ModelTransport208:NSObject<RTXCommandTransport>
@end
@implementation ModelTransport208
- (void)dealloc{require(!visible);++destroyed;events.push_back("destroy");[super dealloc];}
- (BOOL)executeRequest:(NSData*)wire libraryPayload:(NSData*)payload result:(NSData**)result completion:(uint64_t*)serial error:(NSError**)error{
 *result=nil;*serial=0;*error=nil;require(visible&&!withdrawn);events.push_back("execute");
 if(mode=="execute-throw")throw std::runtime_error("CPU injected execute failure");
 RTXProgram205::Library library;require(payload.length==4608&&RTXProgram205::decode(static_cast<const uint8_t*>(payload.bytes),512,static_cast<const uint8_t*>(payload.bytes)+512,4096,library));
 B::Plan plan;require(B::decode(static_cast<const uint8_t*>(wire.bytes),wire.length,library.programs,0x208,completed+1,plan));require(RTXProgram205::geometry(library,plan.program,plan.groups,plan.threads));
 B::Bytes out(static_cast<const uint8_t*>(wire.bytes)+B::Header,static_cast<const uint8_t*>(wire.bytes)+wire.length);
 for(unsigned i=0;i<plan.resources;++i){const auto&r=plan.resource[i];if(r.access&2)for(size_t j=0;j<r.bytes;++j)out[r.payloadOffset+j]^=uint8_t(0x5a+j%31);}
 *serial=++completed;*result=[NSData dataWithBytes:out.data()length:out.size()];
 NSString*base=[folder stringByAppendingPathComponent:[NSString stringWithFormat:@"native-model-%llu",completed]];
 for(auto pair:{std::make_pair(@"-request.bin",wire),std::make_pair(@"-payload.bin",payload),std::make_pair(@"-result.bin",*result)})require([pair.second writeToFile:[base stringByAppendingString:pair.first]options:NSDataWritingWithoutOverwriting error:nil]);
 return mode!="backend-fail";
}
@end
extern "C" int rtx_model208_initialize(const void*bytes,size_t n,const char*directory,const char*which){
 if(geteuid()!=0||initialized||!bytes||n!=5248||!directory||!which)return 1;
 RTXCatalog187::Catalog c;if(!RTXCatalog187::decode(static_cast<const uint8_t*>(bytes),n,c))return 2;
 image=[[NSData alloc]initWithBytes:bytes length:n];folder=[@(directory)copy];mode=which;initialized=true;return 0;
}
extern "C" BOOL RTXModelClaim208(id<RTXCommandTransport>*target,NSData**container,uint64_t*generation,uint64_t*serial){
 require(initialized&&visible&&!withdrawn);events.push_back("claim");++claims;if(mode=="claim-fail")return NO;
 *target=[ModelTransport208 new];*container=[image copy];*generation=0x208;*serial=0;return YES;
}
extern "C" void RTXModelRetire208(){require(!visible);events.push_back("retire");if(mode=="retire-fail")throw std::runtime_error("CPU injected retirement failure");++retires;}
extern "C" int RTXModelLifecycle208(void*,uint32_t event){
 if(event==RTXOwnedLifecycle202::Ready){require(initialized&&!visible&&!withdrawn);events.push_back("activate");visible=true;
  if(mode=="activate-throw")throw std::runtime_error("CPU injected activation failure");return mode=="activate-fail"?9:0;}
 require(event==RTXOwnedLifecycle202::Withdraw&&visible&&!withdrawn);events.push_back("withdraw");if(mode=="withdraw-fail")return 8;visible=false;withdrawn=true;return 0;
}
extern "C" int rtx_model208_report(int result,int closePermitted){@autoreleasepool{
 NSMutableArray*list=[NSMutableArray array];for(const auto&e:events)[list addObject:@(e.c_str())];
 NSDictionary*report=@{@"passed":@YES,@"model_only":@YES,@"native_hardware_opened":@NO,@"gpu_executed":@NO,@"pid":@(getpid()),@"uid":@(geteuid()),@"mode":@(mode.c_str()),@"returned":@(result),@"close_permitted":@(bool(closePermitted)),@"events":list,@"visible":@(visible),@"withdrawn":@(withdrawn),@"claims":@(claims),@"retirements":@(retires),@"destroyed":@(destroyed),@"completed":@(completed)};
 return [[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingSortedKeys error:nil]writeToFile:[folder stringByAppendingPathComponent:@"model-result208.json"]options:NSDataWritingWithoutOverwriting error:nil]?0:1;
}}
